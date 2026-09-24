#include "SunoClient.hpp"

#include "ClipParser.hpp"
#include "SunoAuthFailure.hpp"
#include "auth/AuthHeaders.hpp"
#include "auth/CredentialStore.hpp"
#include "auth/JwtUtils.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <deque>

namespace vc::suno {

// NOTE (auth layering): this class holds NO protocol knowledge. Clerk URLs,
// header recipes and JWT decoding all live in suno/auth/. What remains here
// is scheduling policy: when to refresh (proactive timer + single 401 retry)
// and how requests queue behind authentication.

namespace {
/// Cap on the proactive-refresh delay: even for long-lived tokens, re-touch at
/// most every 55 minutes (per Aug-2026 capture guidance).
constexpr qint64 kMaxRefreshDelaySecs = 55 * 60;

CredentialStoreWorker::Outcome runCredentialStoreRequest(
        CredentialStoreWorker::Request request) {
    CredentialStoreWorker::Outcome outcome;
    outcome.legacyWasCookie = request.legacyWasCookie;
    auth::CredentialStore store;

    switch (request.operation) {
    case CredentialStoreWorker::Operation::Store: {
        if (auto stored = store.store(request.key, request.secret); stored.isErr()) {
            LOG_WARN("SunoClient: failed to persist credential record '{}' on worker: {}",
                     request.key.toStdString(), stored.error().message);
        }
        return outcome;
    }
    case CredentialStoreWorker::Operation::Remove:
        if (auto removed = store.remove(request.key); removed.isErr()) {
            LOG_WARN("SunoClient: failed to remove credential record '{}' on worker: {}",
                     request.key.toStdString(), removed.error().message);
        }
        return outcome;
    case CredentialStoreWorker::Operation::Restore:
        break;
    }

    LOG_INFO("CredentialStore: credential restore started on dedicated worker thread");
    if (auto stored = store.load(QStringLiteral("suno/default")); stored.isOk()) {
        outcome.cookie = stored.value();
    } else if (request.migrateLegacy && !request.legacyCredential.isEmpty()) {
        if (auto migrated = store.store(QStringLiteral("suno/default"),
                                        request.legacyCredential);
            migrated.isOk()) {
            outcome.cookie = request.legacyCredential;
            outcome.migratedLegacy = true;
            LOG_INFO("SunoClient: migrated legacy config.toml {} into secret storage",
                     request.legacyWasCookie ? "cookie" : "token");
        } else {
            outcome.legacyMigrationFailed = true;
            LOG_ERROR("SunoClient: credential migration to keychain failed ({}) - keeping "
                      "legacy TOML values in place",
                      auth::CredentialStore::redact(request.legacyCredential).toStdString());
        }
    }

    if (request.loadBearer) {
        if (auto stored = store.load(QStringLiteral("suno/bearer")); stored.isOk()) {
            outcome.bearer = stored.value();
        }
    }
    return outcome;
}
} // namespace

CredentialStoreWorker::CredentialStoreWorker(QObject* guiReceiver, Backend backend)
    : guiReceiver_(guiReceiver), backend_(std::move(backend)),
      workerThread_(std::make_unique<QThread>()) {
    Q_ASSERT(guiReceiver_ != nullptr);
    Q_ASSERT(static_cast<bool>(backend_));

    workerThread_->setObjectName(QStringLiteral("chadvis-credential-store"));
    worker_ = new QObject;
    worker_->moveToThread(workerThread_.get());
    QObject::connect(workerThread_.get(), &QThread::finished, worker_, &QObject::deleteLater);
    workerThread_->start();
    Q_ASSERT(workerThread_->isRunning());
}

CredentialStoreWorker::~CredentialStoreWorker() {
    if (!workerThread_) {
        return;
    }

    // A Security.framework call cannot be force-cancelled safely. The GUI can
    // remain responsive while it runs, but destruction owns and joins the
    // thread before any captured SunoClient state can disappear.
    workerThread_->requestInterruption();
    workerThread_->quit();
    if (!workerThread_->wait()) {
        LOG_ERROR("SunoClient: credential worker did not stop during shutdown");
    }
    worker_ = nullptr;
}

bool CredentialStoreWorker::requestRestore(Request request, Completion completion) {
    Q_ASSERT(guiReceiver_->thread() == QThread::currentThread());
    if (restoreInFlight_) {
        return false;
    }

    restoreInFlight_ = true;
    restoreDiscarded_ = false;
    const quint64 generation = ++restoreGeneration_;

    const bool queued = QMetaObject::invokeMethod(
            worker_,
            [this, request = std::move(request), completion = std::move(completion),
             generation]() mutable {
                Outcome outcome = backend_(request);
                QMetaObject::invokeMethod(
                        guiReceiver_,
                        [this, outcome = std::move(outcome), completion = std::move(completion),
                         generation]() mutable {
                            if (!restoreInFlight_ || generation != restoreGeneration_) {
                                return;
                            }
                            const bool apply = !restoreDiscarded_;
                            restoreInFlight_ = false;
                            restoreDiscarded_ = false;
                            if (apply && completion) {
                                completion(std::move(outcome));
                            }
                        },
                        completionConnectionType());
            },
            Qt::QueuedConnection);

    if (!queued) {
        restoreInFlight_ = false;
        restoreDiscarded_ = false;
        LOG_ERROR("SunoClient: could not queue credential restore on its worker");
    }
    return queued;
}

void CredentialStoreWorker::store(QString key, QString secret) {
    Request request;
    request.operation = Operation::Store;
    request.key = std::move(key);
    request.secret = std::move(secret);
    enqueue(std::move(request));
}

void CredentialStoreWorker::remove(QString key) {
    Request request;
    request.operation = Operation::Remove;
    request.key = std::move(key);
    enqueue(std::move(request));
}

void CredentialStoreWorker::discardPendingRestore() {
    if (restoreInFlight_) {
        restoreDiscarded_ = true;
    }
}

void CredentialStoreWorker::enqueue(Request request) {
    const bool queued = QMetaObject::invokeMethod(
            worker_, [backend = backend_, request = std::move(request)]() mutable {
                (void)backend(request);
            },
            Qt::QueuedConnection);
    if (!queued) {
        LOG_ERROR("SunoClient: could not queue credential operation on its worker");
    }
}

SunoClient::SunoClient(QString deviceId, QObject* parent)
    : QObject(parent),
      manager_(new QNetworkAccessManager(this)),
      queueTimer_(new QTimer(this)),
      clerk_(new auth::ClerkAuthClient(this)),
      deviceId_(std::move(deviceId)),
      refreshTimer_(new QTimer(this)) {
    queueTimer_->setInterval(1000);
    connect(queueTimer_, &QTimer::timeout, this, &SunoClient::processQueue);

    refreshTimer_->setSingleShot(true);
    connect(refreshTimer_, &QTimer::timeout, this,
            [this]() { ensureFreshBearer(/*force=*/true); });

    connect(clerk_, &auth::ClerkAuthClient::bearerReady, this,
            [this](const auth::BearerToken& token) { onBearerReadyInternal(token); });
    connect(clerk_, &auth::ClerkAuthClient::authFailed, this,
            [this](const QString& reason) {
                onClerkAuthFailedInternal(reason, clerk_->failureKind());
            });

    // Constructing this worker performs no keychain I/O. restoreSession() below
    // only snapshots legacy config and posts work to its private QThread.
    credentialStoreWorker_ = std::make_unique<CredentialStoreWorker>(
            this, [](CredentialStoreWorker::Request request) {
                return runCredentialStoreRequest(std::move(request));
            });
    restoreSession(RestoreMode::Startup);
}

SunoClient::~SunoClient() {
    // Join before dependent QObject/timer members are destroyed.
    credentialStoreWorker_.reset();
}

// ─────────────────────────────────────────────────────────────
// Startup: restore + migrate credentials
// ─────────────────────────────────────────────────────────────

void SunoClient::restoreSession(RestoreMode mode) {
    CredentialStoreWorker::Request request;
    request.operation = CredentialStoreWorker::Operation::Restore;
    request.loadBearer = mode == RestoreMode::Startup;

    if (mode == RestoreMode::Startup) {
        request.migrateLegacy = true;
        const auto& cfg = CONFIG.suno();
        request.legacyWasCookie = !cfg.cookie.empty();
        request.legacyCredential = QString::fromStdString(
                cfg.cookie.empty() ? cfg.token : cfg.cookie);
    }

    LOG_INFO("SunoClient: credential restore queued on dedicated worker thread");
    const bool started = credentialStoreWorker_->requestRestore(
            std::move(request),
            [this, mode](CredentialStoreWorker::Outcome result) mutable {
                applyRestoreResult(mode, std::move(result));
            });
    if (!started && credentialStoreWorker_->isRestoreInFlight()) {
        LOG_INFO("SunoClient: credential restore request coalesced with in-flight read");
    }
}

void SunoClient::applyRestoreResult(RestoreMode mode,
                                    CredentialStoreWorker::Outcome result) {
    if (result.migratedLegacy) {
        auto& cfg = CONFIG.suno();
        cfg.cookie.clear();
        cfg.token.clear();
        std::ignore = CONFIG.save(CONFIG.configPath());
    }
    if (result.legacyMigrationFailed) {
        // The worker already logged the actionable, secret-free failure. Keep
        // the legacy values untouched and continue in signed-out state.
        setState(auth::AuthState::Disconnected);
        authWaiters_.clear();
        return;
    }

    bool rejectedStoredCredential = false;
    if (result.cookie.has_value() && !result.cookie->isEmpty()) {
        const QString value = *result.cookie;
        const auto classification = auth::classifyStoredCredential(value);
        switch (classification.shape) {
        case auth::StoredCredentialShape::BearerToken:
            credentials_.cookieHeader.clear();
            lastActiveSessionId_.clear();
            if (value != bearer_.jwt) {
                applyBearer(auth::JwtUtils::fromJwt(value));
            } else {
                setAuthFailureKind(auth::AuthFailureKind::None);
            }
            break;
        case auth::StoredCredentialShape::ClerkCookieHeader:
            setAuthFailureKind(auth::AuthFailureKind::None);
            if (value != credentials_.cookieHeader) {
                credentials_ = auth::Credentials{value};
                lastActiveSessionId_.clear();
                bearer_ = auth::BearerToken{};
                setState(auth::AuthState::NeedsReauth);
                if (mode == RestoreMode::Reload) {
                    emit tokenChanged(std::string());
                }
            }
            break;
        case auth::StoredCredentialShape::Unsupported:
            // Never reinterpret an unknown opaque value as a Cookie header.
            // Keep the persisted value for correction, but remove any older
            // in-memory credential so it cannot be sent on a later request.
            credentials_ = auth::Credentials{};
            bearer_ = auth::BearerToken{};
            lastActiveSessionId_.clear();
            touchInFlight_ = false;
            dropPendingAuthWork(QStringLiteral("unsupported stored credential shape"));
            setAuthFailureKind(classification.failureKind);
            setState(auth::AuthState::NeedsReauth);
            LOG_ERROR("SunoClient: {} (stored value preserved)",
                      classification.safeDiagnostic.toStdString());
            if (mode == RestoreMode::Reload) {
                emit tokenChanged(std::string());
            }
            rejectedStoredCredential = true;
            break;
        case auth::StoredCredentialShape::Empty:
            break;
        }
    }

    // Fast path: a persisted bearer that is still genuinely unexpired.
    if (result.bearer.has_value()) {
        const QString& jwt = *result.bearer;
        auto claims = auth::JwtUtils::claims(jwt);
        if (claims && !auth::JwtUtils::isExpired(*claims, /*graceSecs=*/0)) {
            applyBearer(auth::JwtUtils::fromJwt(jwt));
            flushAuthWaiters();
            return;
        }
    }

    if (rejectedStoredCredential) {
        return;
    }

    if (!credentials_.cookieHeader.isEmpty()) {
        LOG_INFO("SunoClient: cookie restored from secret storage; fetching bearer");
        ensureFreshBearer(/*force=*/true);
        return;
    }

    if (mode == RestoreMode::Startup) {
        LOG_INFO("SunoClient: credential restore completed; no active session (signed out)");
        setState(auth::AuthState::Disconnected);
        authWaiters_.clear();
    }
}

// ─────────────────────────────────────────────────────────────
// Credential injection / queries
// ─────────────────────────────────────────────────────────────

bool SunoClient::isAuthenticated() const {
    if (!bearer_.jwt.isEmpty()) {
        return true;
    }
    // An unresolved restore is unknown, never a claim of authentication.
    if (credentialStoreWorker_ && credentialStoreWorker_->isRestoreInFlight()) {
        return false;
    }
    return !credentials_.cookieHeader.isEmpty();
}

bool SunoClient::hasCredentials() const {
    return !credentials_.cookieHeader.isEmpty();
}

void SunoClient::setCookie(const std::string& cookie) {
    const QString value = QString::fromStdString(cookie);
    if (value == credentials_.cookieHeader) {
        return;
    }

    credentialStoreWorker_->discardPendingRestore();
    credentials_ = auth::Credentials{value};
    lastActiveSessionId_.clear();
    bearer_ = auth::BearerToken{};
    setState(value.isEmpty() ? auth::AuthState::Disconnected
                             : auth::AuthState::NeedsReauth);

    if (value.isEmpty()) {
        credentialStoreWorker_->remove(QStringLiteral("suno/default"));
    } else {
        credentialStoreWorker_->store(QStringLiteral("suno/default"), value);
    }
    emit tokenChanged(std::string());

    if (!value.isEmpty()) {
        ensureFreshBearer(/*force=*/true);
    }
}

void SunoClient::setToken(const std::string& token) {
    const QString jwt = QString::fromStdString(token);
    if (!auth::JwtUtils::claims(jwt)) {
        LOG_WARN("SunoClient: rejected malformed token input");
        return;
    }
    credentialStoreWorker_->discardPendingRestore();
    applyBearer(auth::JwtUtils::fromJwt(jwt));
}

void SunoClient::reloadStoredCredentials() {
    // This method is called on the GUI thread by SunoLibraryManager. Queue the
    // read; a concurrent startup/reload is deliberately coalesced.
    restoreSession(RestoreMode::Reload);
}

void SunoClient::clearLocalCredentials() {
    refreshTimer_->stop();
    touchInFlight_ = false;
    retryQueue_.clear();
    authWaiters_.clear();
    credentialStoreWorker_->discardPendingRestore();

    // Disconnect and retire the old client so a late network reply cannot
    // reinstall a bearer after local sign-out.
    QObject::disconnect(clerk_, nullptr, this, nullptr);
    clerk_->deleteLater();
    clerk_ = new auth::ClerkAuthClient(this);
    connect(clerk_, &auth::ClerkAuthClient::bearerReady, this,
            [this](const auth::BearerToken& token) { onBearerReadyInternal(token); });
    connect(clerk_, &auth::ClerkAuthClient::authFailed, this,
            [this](const QString& reason) {
                onClerkAuthFailedInternal(reason, clerk_->failureKind());
            });

    credentials_ = auth::Credentials{};
    bearer_ = auth::BearerToken{};
    lastActiveSessionId_.clear();
    setAuthFailureKind(auth::AuthFailureKind::None);

    for (const QString& key : {QStringLiteral("suno/default"), QStringLiteral("suno/bearer")}) {
        credentialStoreWorker_->remove(key);
    }

    setState(auth::AuthState::Disconnected);
    tokenChanged.emitSignal(std::string());
}

// ─────────────────────────────────────────────────────────────
// Auth state machine
// ─────────────────────────────────────────────────────────────

void SunoClient::setState(auth::AuthState state) {
    if (authState_ == state) {
        return;
    }
    authState_ = state;
    LOG_INFO("SunoClient: auth state -> {}",
             state == auth::AuthState::ActiveValid     ? "ActiveValid"
             : state == auth::AuthState::NeedsReauth   ? "NeedsReauth"
                                                       : "Disconnected");
    emit authStateChanged();
}

void SunoClient::setAuthFailureKind(auth::AuthFailureKind kind) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (authFailureKind_.exchange(kind, std::memory_order_acq_rel) == kind) {
        return;
    }
    emit authFailureKindChanged();
}

void SunoClient::applyBearer(const auth::BearerToken& token) {
    bearer_ = token;
    setAuthFailureKind(auth::AuthFailureKind::None);

    // Session id rides in the standard Clerk "sid" claim.
    if (auto claims = auth::JwtUtils::claims(token.jwt)) {
        lastActiveSessionId_ = auth::JwtUtils::claimString(*claims, "sid");
    }

    credentialStoreWorker_->store(QStringLiteral("suno/bearer"), token.jwt);

    scheduleProactiveRefresh();
    setState(auth::AuthState::ActiveValid);
    emit tokenChanged(token.jwt.toStdString());
}

void SunoClient::scheduleProactiveRefresh() {
    if (!bearer_.expiresAt.isValid()) {
        return;
    }
    qint64 delaySecs =
            QDateTime::currentDateTimeUtc().secsTo(bearer_.expiresAt) -
            auth::BearerToken::kExpirySoonSecs;
    delaySecs = qBound<qint64>(0, delaySecs, kMaxRefreshDelaySecs);
    refreshTimer_->start(static_cast<int>(delaySecs * 1000) + 1);
}

void SunoClient::ensureFreshBearer(bool force) {
    if (credentialStoreWorker_->isRestoreInFlight()) {
        return; // the queued GUI completion owns the next auth transition
    }
    if (!force && !bearer_.jwt.isEmpty()) {
        return; // have something usable; the 401 path handles staleness
    }
    if (!hasCredentials() || touchInFlight_) {
        return;
    }
    touchInFlight_ = true;
    setAuthFailureKind(auth::AuthFailureKind::None);
    if (!lastActiveSessionId_.isEmpty()) {
        clerk_->touch(credentials_, lastActiveSessionId_);
    } else {
        clerk_->fetchBearer(credentials_);
    }
}

void SunoClient::onBearerReadyInternal(const auth::BearerToken& token) {
    touchInFlight_ = false;
    applyBearer(token);

    // Replay requests intercepted by the uniform 401 handler...
    std::deque<PendingRequest> retries;
    retries.swap(retryQueue_);
    while (!retries.empty()) {
        PendingRequest pending = std::move(retries.front());
        retries.pop_front();
        enqueueRequest(std::move(pending.request), std::move(pending.method),
                       std::move(pending.data), std::move(pending.callback),
                       /*retriedAuth=*/true);
    }
    flushAuthWaiters();
}

void SunoClient::onClerkAuthFailedInternal(const QString& reason,
                                           auth::AuthFailureKind kind) {
    touchInFlight_ = false;
    setAuthFailureKind(kind);
    LOG_ERROR("SunoClient: clerk auth exchange failed: {}", reason.toStdString());
    dropPendingAuthWork(reason);
    setState(auth::AuthState::NeedsReauth);
    emit errorOccurred(reason.toStdString());
    emit needsReauth(); // user keeps their cookies; they just need fresh ones
}

void SunoClient::flushAuthWaiters() {
    std::vector<std::function<void()>> waiters;
    waiters.swap(authWaiters_);
    for (auto& proceed : waiters) {
        proceed();
    }
}

void SunoClient::dropPendingAuthWork(const QString& reason) {
    retryQueue_.clear();
    authWaiters_.clear();
    std::ignore = reason;
}

// ─────────────────────────────────────────────────────────────
// Request plumbing
// ─────────────────────────────────────────────────────────────

void SunoClient::withValidToken(std::function<void()> proceed) {
    if (!bearer_.jwt.isEmpty()) {
        proceed();
        return;
    }
    if (credentialStoreWorker_->isRestoreInFlight()) {
        authWaiters_.push_back(std::move(proceed));
        return;
    }
    if (!hasCredentials()) {
        proceed();
        return;
    }
    authWaiters_.push_back(std::move(proceed));
    ensureFreshBearer();
}

void SunoClient::enqueueAuthenticatedRequest(const QString& endpoint,
                                             const std::string& method,
                                             const QByteArray& data,
                                             std::function<void(QNetworkReply*)> callback) {
    withValidToken([this, endpoint, method, data, callback = std::move(callback)]() mutable {
        enqueueRequest(createAuthenticatedRequest(endpoint), method, data,
                       std::move(callback));
    });
}

QNetworkRequest SunoClient::createAuthenticatedRequest(const QString& endpoint) {
    QUrl url = endpoint.startsWith(QStringLiteral("http")) ? QUrl(endpoint)
                                                           : QUrl(API_BASE + endpoint);
    QNetworkRequest request(url);
    // Single canonical header recipe for ALL studio-api traffic.
    auth::makeStudioApiHeaders(bearer_.jwt, deviceId_).apply(request);
    return request;
}

void SunoClient::enqueueRequest(QNetworkRequest req, const std::string& method,
                                QByteArray data,
                                std::function<void(QNetworkReply*)> callback,
                                bool retriedAuth) {
    requestQueue_.push_back({std::move(req), method, std::move(data),
                             std::move(callback), retriedAuth});
    if (!queueTimer_->isActive()) {
        queueTimer_->start();
    }
}

void SunoClient::processQueue() {
    if (requestQueue_.empty()) {
        queueTimer_->stop();
        return;
    }

    PendingRequest pending = std::move(requestQueue_.front());
    requestQueue_.pop_front();

    QNetworkReply* reply;
    if (pending.method == "POST") {
        reply = manager_->post(pending.request, pending.data);
    } else {
        reply = manager_->get(pending.request);
    }

    // Route through a member handler so 401 interception happens uniformly.
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, pending = std::move(pending)]() mutable {
                handleReplyFinished(reply, std::move(pending));
            });

    if (requestQueue_.empty()) {
        queueTimer_->stop();
    }
}

void SunoClient::handleReplyFinished(QNetworkReply* reply, PendingRequest&& pending) {
    const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // UNIFORM 401 handling: exactly one silent refresh+retry per request.
    if (status == 401 && !pending.retriedAuth && hasCredentials()) {
        LOG_WARN("SunoClient: 401 on {} - refreshing bearer, retrying once",
                 pending.request.url().toString().toStdString());
        reply->deleteLater();
        bearer_ = auth::BearerToken{}; // force touch over fetch
        pending.retriedAuth = true;
        retryQueue_.push_back(std::move(pending));
        ensureFreshBearer(/*force=*/true);
        return;
    }

    pending.callback(reply);
}

void SunoClient::handleJsonReply(QNetworkReply* reply,
                                 std::function<void(const QJsonDocument&)> handler) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply);
        return;
    }
    handler(QJsonDocument::fromJson(reply->readAll()));
}

void SunoClient::handleNetworkError(QNetworkReply* reply) {
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    std::string err = reply->errorString().toStdString();
    if (isAuthFailure(httpStatus, reply->errorString())) {
        err = "Unauthorized: Token expired";
        bearer_.jwt.clear();
        setAuthFailureKind(auth::AuthFailureKind::RejectedCredential);
        setState(auth::AuthState::NeedsReauth);
        emit needsReauth();
    }
    errorOccurred.emitSignal(err);
    LOG_ERROR("SunoClient API Error: {}", err);
}

// ─────────────────────────────────────────────────────────────
// API surface
// ─────────────────────────────────────────────────────────────

void SunoClient::fetchLibraryPage(std::optional<QString> cursor, int limit,
                                  const QString& searchText) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }

    // Request body per the CAPTURED /api/feed/v3 contract (T1, Aug 2026):
    // disliked/trashed are STRING "True"/"False"; presence filters are
    // objects; cursor is null/omitted on the first page.
    QJsonObject fromStudioProject;
    fromStudioProject["presence"] = QStringLiteral("False");
    QJsonObject stem;
    stem["presence"] = QStringLiteral("False");
    QJsonObject workspace;
    workspace["presence"] = QStringLiteral("True");
    workspace["workspaceId"] = QStringLiteral("default");

    QJsonObject filters;
    filters["disliked"] = QStringLiteral("False");
    filters["trashed"] = QStringLiteral("False");
    filters["fromStudioProject"] = fromStudioProject;
    filters["stem"] = stem;
    filters["stemComplement"] = QStringLiteral("False");
    filters["workspace"] = workspace;
    if (!searchText.isEmpty()) {
        filters["searchText"] = searchText;
    }

    QJsonObject body;
    body["cursor"] = cursor.has_value() ? QJsonValue(*cursor) : QJsonValue::Null;
    body["limit"] = limit;
    body["filters"] = filters;

    withValidToken([this, body = std::move(body)]() {
        enqueueRequest(createAuthenticatedRequest(qstr(vc::suno::endpoints::LIBRARY_FEED)),
                       "POST", QJsonDocument(body).toJson(),
                       [this](QNetworkReply* reply) { onLibraryReply(reply); });
    });
}

void SunoClient::onLibraryReply(QNetworkReply* reply) {
    handleJsonReply(reply, [this](const QJsonDocument& doc) {
        auto page = ClipParser::parseFeedEnvelope(doc.object());
        if (!page) {
            LOG_ERROR("SunoClient: feed envelope parse failed: {}",
                      page.error().toStdString());
            libraryFetched.emitSignal({});
            return;
        }

        nextCursor_ = page->nextCursor;
        hasMore_ = page->hasMore;

        std::vector<SunoClip> clips(page->clips.cbegin(), page->clips.cend());
        libraryFetched.emitSignal(clips);
    });
}

void SunoClient::generate(const std::string& prompt, const std::string& tags,
                          bool makeInstrumental, const std::string& model) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }

    withValidToken([this, prompt, tags, makeInstrumental, model]() {
        QJsonObject body;
        body["gpt_description_prompt"] = QString::fromStdString(prompt);
        body["prompt"] = ""; // Used for custom lyrics
        body["tags"] = QString::fromStdString(tags);
        body["mv"] = QString::fromStdString(model);
        body["make_instrumental"] = makeInstrumental;
        body["continue_clip_id"] = QJsonValue::Null;
        body["continue_at"] = QJsonValue::Null;

        QJsonDocument doc(body);
        enqueueRequest(createAuthenticatedRequest(qstr(vc::suno::endpoints::GENERATE)),
                       "POST", doc.toJson(),
                       [this](QNetworkReply* reply) { onGenerateReply(reply); });
    });
}

void SunoClient::onGenerateReply(QNetworkReply* reply) {
    handleJsonReply(reply, [this](const QJsonDocument& doc) {
        // Tolerant parse: accept {"clips": [...]} or a bare array, via the
        // canonical ClipParser (same field coverage as the library feed).
        QJsonArray array;
        if (doc.isObject()) {
            const QJsonValue clips = doc.object().value(QStringLiteral("clips"));
            if (clips.isArray()) {
                array = clips.toArray();
            }
        } else if (doc.isArray()) {
            array = doc.array();
        }

        auto parsed = ClipParser::parseClipArray(array);
        if (!parsed) {
            LOG_ERROR("SunoClient: generation reply parse failed: {}",
                      parsed.error().toStdString());
            generationStarted.emitSignal({});
            return;
        }
        std::vector<SunoClip> clips(parsed->cbegin(), parsed->cend());
        for (auto& clip : clips) {
            clip.status = clip.status.empty() ? "pending" : clip.status;
        }
        generationStarted.emitSignal(std::move(clips));
    });
}

void SunoClient::fetchAlignedLyrics(const std::string& clipId) {
    if (!isAuthenticated()) return;
    withValidToken([this, clipId]() {
        QString url = qstr(vc::suno::endpoints::ALIGNED_LYRICS)
                              .replace("{}", QString::fromStdString(clipId));
        enqueueRequest(createAuthenticatedRequest(url), "GET", {},
                       [this, clipId](QNetworkReply* reply) {
                           handleJsonReply(reply, [this, clipId](const QJsonDocument& doc) {
                               // Forward the body as compact JSON; consumers re-parse it.
                               alignedLyricsFetched.emitSignal(
                                       clipId, doc.toJson(QJsonDocument::Compact).toStdString());
                           });
                       });
    });
}

void SunoClient::initiateWavConversion(const std::string& clipId) {
    if (!isAuthenticated()) return;
    cancelledPolls_.remove(QString::fromStdString(clipId));
    withValidToken([this, clipId]() {
        QString url = qstr(vc::suno::endpoints::CONVERT_WAV)
                              .replace("{}", QString::fromStdString(clipId));
        enqueueAuthenticatedRequest(url, "POST", {}, [this, clipId](QNetworkReply* reply) {
            onWavConversionInitiated(clipId, reply);
        });
    });
}

void SunoClient::onWavConversionInitiated(const std::string& clipId, QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) { // 2xx incl. 202 Accepted
        QTimer::singleShot(2000, this, [this, clipId]() { pollWavFile(clipId, 60); });
    }
}

void SunoClient::cancelPoll(const std::string& clipId) {
    cancelledPolls_.insert(QString::fromStdString(clipId));
}

void SunoClient::pollWavFile(const std::string& clipId, int maxAttempts) {
    if (!isAuthenticated() || maxAttempts <= 0) return;
    const QString id = QString::fromStdString(clipId);
    if (cancelledPolls_.contains(id)) {
        cancelledPolls_.remove(id);
        LOG_INFO("SunoClient: wav polling cancelled for {}", clipId);
        return;
    }
    QString url = qstr(vc::suno::endpoints::WAV_FILE).replace("{}", id);
    enqueueAuthenticatedRequest(url, "GET", {}, [this, id, maxAttempts](QNetworkReply* reply) {
        reply->deleteLater();
        if (cancelledPolls_.contains(id)) {
            cancelledPolls_.remove(id);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() == QNetworkReply::NoError &&
            doc.object().value("wav_file_url").isString()) {
            wavConversionReady.emitSignal(
                    id.toStdString(),
                    doc.object()["wav_file_url"].toString().toStdString());
        } else {
            QTimer::singleShot(2000, this,
                               [this, id, maxAttempts]() { pollWavFile(id.toStdString(), maxAttempts - 1); });
        }
    });
}

} // namespace vc::suno
