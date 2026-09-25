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
#include <QUrl>

#include <algorithm>
#include <deque>
#include <utility>

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
        const QString migratedCredential =
                request.legacyWasCookie
                        ? auth::normalizeCookieHeader(request.legacyCredential)
                        : request.legacyCredential.trimmed();
        if (auto migrated = store.store(QStringLiteral("suno/default"),
                                        migratedCredential);
            migrated.isOk()) {
            outcome.cookie = migratedCredential;
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
        if (completion) {
            restoreCompletions_.push_back(std::move(completion));
        }
        return false;
    }

    restoreInFlight_ = true;
    restoreDiscarded_ = false;
    const quint64 generation = ++restoreGeneration_;
    if (completion) {
        restoreCompletions_.push_back(std::move(completion));
    }

    const bool queued = QMetaObject::invokeMethod(
            worker_,
            [this, request = std::move(request), generation]() mutable {
                Outcome outcome = backend_(request);
                QMetaObject::invokeMethod(
                        guiReceiver_,
                        [this, outcome = std::move(outcome), generation]() mutable {
                            if (!restoreInFlight_ || generation != restoreGeneration_) {
                                return;
                            }
                            const bool apply = !restoreDiscarded_;
                            restoreInFlight_ = false;
                            restoreDiscarded_ = false;
                            auto completions = std::move(restoreCompletions_);
                            restoreCompletions_.clear();
                            for (std::size_t index = 0; index < completions.size(); ++index) {
                                if (!completions[index]) {
                                    continue;
                                }
                                Outcome completionOutcome = outcome;
                                completionOutcome.restoreDiscarded = !apply;
                                completionOutcome.reusedResult = index > 0;
                                completions[index](std::move(completionOutcome));
                            }
                        },
                        completionConnectionType());
            },
            Qt::QueuedConnection);

    if (!queued) {
        restoreInFlight_ = false;
        restoreDiscarded_ = false;
        auto completions = std::move(restoreCompletions_);
        restoreCompletions_.clear();
        LOG_ERROR("SunoClient: could not queue credential restore on its worker");
        for (auto& completion : completions) {
            if (!completion) {
                continue;
            }
            Outcome outcome;
            outcome.restoreDiscarded = true;
            completion(std::move(outcome));
        }
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

SunoClient::SunoClient(QString deviceId,
                     QObject* parent,
                     CredentialStoreWorker::Backend credentialStoreBackend,
                     ReplyFactory replyFactory)
    : QObject(parent),
      manager_(new QNetworkAccessManager(this)),
      replyFactory_(std::move(replyFactory)),
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
    auto credentialBackend =
            credentialStoreBackend
                    ? std::move(credentialStoreBackend)
                    : [](CredentialStoreWorker::Request request) {
                          return runCredentialStoreRequest(std::move(request));
                      };
    credentialStoreWorker_ = std::make_unique<CredentialStoreWorker>(
            this, std::move(credentialBackend));
    restoreSession(RestoreMode::Startup);
}

SunoClient::~SunoClient() {
    requestQueue_.clear();
    retryQueue_.clear();
    authWaiters_.clear();
    abortTrackedReplies();
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

    const quint64 restoreEpoch = requestEpoch_;
    LOG_INFO("SunoClient: credential restore queued on dedicated worker thread");
    const bool started = credentialStoreWorker_->requestRestore(
            std::move(request),
            [this, mode, restoreEpoch](CredentialStoreWorker::Outcome result) mutable {
                if (restoreEpoch != requestEpoch_) {
                    if (refreshAfterRestore_ && hasCredentials() &&
                        authState_ == auth::AuthState::NeedsReauth) {
                        refreshAfterRestore_ = false;
                        ensureFreshBearer(/*force=*/true);
                    }
                    emit credentialRestoreCompleted();
                    return;
                }
                if (!result.restoreDiscarded && !result.reusedResult) {
                    applyRestoreResult(mode, std::move(result));
                }
                emit credentialChanged();
                emit credentialRestoreCompleted();
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
        dropPendingAuthWork(QStringLiteral("credential migration failed"));
        setState(auth::AuthState::Disconnected);
        return;
    }

    bool rejectedStoredCredential = false;
    bool storedCookieChanged = false;
    const bool storedCookieCleared =
            !result.cookie.has_value() || result.cookie->isEmpty();
    bool credentialChanged = false;
    std::vector<AuthWaiter> restoreWaiters;
    const auto invalidateForRestore = [&](const QString& reason, bool preserve) {
        if (preserve) {
            restoreWaiters.swap(authWaiters_);
        }
        const quint64 epochBefore = requestEpoch_;
        dropPendingAuthWork(reason);
        credentialChanged = true;
        const quint64 expectedEpoch =
                epochBefore == std::numeric_limits<quint64>::max() ? 1 : epochBefore + 1;
        if (preserve && requestEpoch_ == expectedEpoch) {
            for (AuthWaiter& waiter : restoreWaiters) {
                waiter.epoch = requestEpoch_;
                authWaiters_.push_back(std::move(waiter));
            }
            restoreWaiters.clear();
        }
    };
    if (result.cookie.has_value() && !result.cookie->isEmpty()) {
        const QString storedValue = *result.cookie;
        const QString value = auth::normalizeCookieHeader(storedValue);
        const auto classification = auth::classifyStoredCredential(value);
        credentialChanged = value != configuredCredential_;
        if (credentialChanged) {
            invalidateForRestore(
                    QStringLiteral("stored credential changed"),
                    classification.shape != auth::StoredCredentialShape::Unsupported);
        }
        configuredCredential_ = value;
        if (value != storedValue &&
            (classification.shape == auth::StoredCredentialShape::BearerToken ||
             classification.shape == auth::StoredCredentialShape::ClerkCookieHeader)) {
            credentialStoreWorker_->store(QStringLiteral("suno/default"), value);
        }
        switch (classification.shape) {
        case auth::StoredCredentialShape::BearerToken:
            credentials_.cookieHeader.clear();
            lastActiveSessionId_.clear();
            if (value != bearer_.jwt) {
                if (!credentialChanged) {
                    invalidateForRestore(QStringLiteral("stored bearer changed"), true);
                }
                applyBearer(auth::JwtUtils::fromJwt(value));
            } else {
                setAuthFailureKind(auth::AuthFailureKind::None);
            }
            break;
        case auth::StoredCredentialShape::ClerkCookieHeader:
            setAuthFailureKind(auth::AuthFailureKind::None);
            if (value != credentials_.cookieHeader) {
                if (!credentialChanged) {
                    invalidateForRestore(QStringLiteral("stored cookie changed"), true);
                }
                storedCookieChanged = true;
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
            if (!credentialChanged) {
                invalidateForRestore(QStringLiteral("unsupported stored credential shape"), false);
            }
            credentials_ = auth::Credentials{};
            bearer_ = auth::BearerToken{};
            lastActiveSessionId_.clear();
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

    if (!storedCookieChanged && result.bearer.has_value()) {
        const QString jwt = auth::normalizeCookieHeader(*result.bearer);
        auto claims = auth::JwtUtils::claims(jwt);
        if (claims && !auth::JwtUtils::isExpired(*claims, /*graceSecs=*/0)) {
            if (storedCookieCleared) {
                if (!credentialChanged &&
                    (configuredCredential_ != jwt || !credentials_.cookieHeader.isEmpty() ||
                     jwt != bearer_.jwt)) {
                    invalidateForRestore(QStringLiteral("stored credential changed"), true);
                }
                configuredCredential_ = jwt;
                credentials_.cookieHeader.clear();
                lastActiveSessionId_.clear();
            } else if (!credentialChanged && jwt != bearer_.jwt) {
                invalidateForRestore(QStringLiteral("stored bearer changed"), true);
            }
            applyBearer(auth::JwtUtils::fromJwt(jwt));
            flushAuthWaiters();
            return;
        }
    }

    if (storedCookieCleared &&
        (!credentials_.cookieHeader.isEmpty() || !bearer_.jwt.isEmpty() ||
         !configuredCredential_.isEmpty())) {
        if (!credentialChanged) {
            dropPendingAuthWork(QStringLiteral("stored credential cleared"));
        }
        configuredCredential_.clear();
        credentials_ = auth::Credentials{};
        bearer_ = auth::BearerToken{};
        lastActiveSessionId_.clear();
        setState(auth::AuthState::Disconnected);
        if (mode == RestoreMode::Reload) {
            emit tokenChanged(std::string());
        }
        return;
    }

    if (rejectedStoredCredential) {
        return;
    }

    if (!credentials_.cookieHeader.isEmpty()) {
        LOG_INFO("SunoClient: cookie restored from secret storage; fetching bearer");
        ensureFreshBearer(/*force=*/true);
        return;
    }

    if (bearer_.jwt.isEmpty()) {
        dropPendingAuthWork(QStringLiteral("no active credential"));
    }
    if (mode == RestoreMode::Startup) {
        LOG_INFO("SunoClient: credential restore completed; no active session (signed out)");
        setState(auth::AuthState::Disconnected);
    }
}

// ─────────────────────────────────────────────────────────────
// Credential injection / queries
// ─────────────────────────────────────────────────────────────

bool SunoClient::isAuthenticated() const {
    if (authState_ == auth::AuthState::ActiveValid && !bearer_.jwt.isEmpty()) {
        return true;
    }
    if (authState_ == auth::AuthState::NeedsReauth && touchInFlight_ &&
        !credentials_.cookieHeader.isEmpty()) {
        return true;
    }
    return false;
}

bool SunoClient::hasCredentials() const {
    return !credentials_.cookieHeader.isEmpty();
}

void SunoClient::setCookie(const std::string& cookie) {
    const QString value = auth::normalizeCookieHeader(QString::fromStdString(cookie));
    if (value == credentials_.cookieHeader && value == configuredCredential_) {
        return;
    }

    credentialStoreWorker_->discardPendingRestore();
    const bool restoreInFlight = credentialStoreWorker_->isRestoreInFlight();
    dropPendingAuthWork(value.isEmpty() ? QStringLiteral("credential cleared")
                                        : QStringLiteral("credential replaced"));
    refreshAfterRestore_ = restoreInFlight && !value.isEmpty();
    configuredCredential_ = value;
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
    emit credentialChanged();
    if (!refreshAfterRestore_) {
        emit credentialRestoreCompleted();
    }

    if (!value.isEmpty()) {
        ensureFreshBearer(/*force=*/true);
    }
}

void SunoClient::setToken(const std::string& token) {
    const QString jwt = auth::normalizeCookieHeader(QString::fromStdString(token));
    if (!auth::JwtUtils::claims(jwt)) {
        LOG_WARN("SunoClient: rejected malformed token input");
        return;
    }
    const bool restoreInFlight = credentialStoreWorker_->isRestoreInFlight();
    credentialStoreWorker_->discardPendingRestore();
    if (jwt != bearer_.jwt || jwt != configuredCredential_) {
        dropPendingAuthWork(QStringLiteral("credential replaced"));
    }
    configuredCredential_ = jwt;
    applyBearer(auth::JwtUtils::fromJwt(jwt));
    emit credentialChanged();
    if (!restoreInFlight) {
        emit credentialRestoreCompleted();
    }
}

void SunoClient::reloadStoredCredentials() {
    // This method is called on the GUI thread by SunoLibraryManager. Queue the
    // read; a concurrent startup/reload is deliberately coalesced.
    restoreSession(RestoreMode::Reload);
}

void SunoClient::clearLocalCredentials() {
    refreshTimer_->stop();
    credentialStoreWorker_->discardPendingRestore();
    dropPendingAuthWork(QStringLiteral("local sign-out"));

    credentials_ = auth::Credentials{};
    configuredCredential_.clear();
    bearer_ = auth::BearerToken{};
    lastActiveSessionId_.clear();
    setAuthFailureKind(auth::AuthFailureKind::None);

    for (const QString& key : {QStringLiteral("suno/default"), QStringLiteral("suno/bearer")}) {
        credentialStoreWorker_->remove(key);
    }

    setState(auth::AuthState::Disconnected);
    tokenChanged.emitSignal(std::string());
    emit credentialChanged();
    emit credentialRestoreCompleted();
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

void SunoClient::invalidateRequestEpoch(const QString& reason) {
    requestEpoch_ = requestEpoch_ == std::numeric_limits<quint64>::max() ? 1
                                                                       : requestEpoch_ + 1;
    queueTimer_->stop();
    requestQueue_.clear();
    retryQueue_.clear();
    authWaiters_.clear();
    refreshAfterRestore_ = false;
    abortTrackedReplies();
    emit credentialInvalidated();
    std::ignore = reason;
}

void SunoClient::resetClerkClient() {
    touchInFlight_ = false;
    authExchangeEpoch_ = 0;
    if (clerk_) {
        QObject::disconnect(clerk_, nullptr, this, nullptr);
        clerk_->deleteLater();
    }
    clerk_ = new auth::ClerkAuthClient(this);
    connect(clerk_, &auth::ClerkAuthClient::bearerReady, this,
            [this](const auth::BearerToken& token) { onBearerReadyInternal(token); });
    connect(clerk_, &auth::ClerkAuthClient::authFailed, this,
            [this](const QString& reason) {
                onClerkAuthFailedInternal(reason, clerk_->failureKind());
            });
}

bool SunoClient::isTrackedReply(const QNetworkReply* reply) const {
    return std::any_of(activeReplies_.cbegin(), activeReplies_.cend(),
                       [reply](const QPointer<QNetworkReply>& tracked) {
                           return tracked.data() == reply;
                       });
}

void SunoClient::trackReply(QNetworkReply* reply) {
    if (reply) {
        activeReplies_.push_back(QPointer<QNetworkReply>(reply));
    }
}

void SunoClient::removeTrackedReply(QNetworkReply* reply) {
    activeReplies_.erase(
            std::remove_if(activeReplies_.begin(), activeReplies_.end(),
                           [reply](const QPointer<QNetworkReply>& tracked) {
                               return tracked.data() == reply || tracked.isNull();
                           }),
            activeReplies_.end());
}

void SunoClient::abortTrackedReplies() {
    std::vector<QPointer<QNetworkReply>> replies;
    replies.swap(activeReplies_);
    for (const QPointer<QNetworkReply>& weak : replies) {
        QNetworkReply* reply = weak.data();
        if (!reply) {
            continue;
        }
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        if (!weak.isNull()) {
            reply->deleteLater();
        }
    }
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
    authExchangeEpoch_ = requestEpoch_;
    setAuthFailureKind(auth::AuthFailureKind::None);
    if (!lastActiveSessionId_.isEmpty()) {
        clerk_->touch(credentials_, lastActiveSessionId_);
    } else {
        clerk_->fetchBearer(credentials_);
    }
}

void SunoClient::onBearerReadyInternal(const auth::BearerToken& token) {
    if (authExchangeEpoch_ == 0 || authExchangeEpoch_ != requestEpoch_) {
        return;
    }
    touchInFlight_ = false;
    authExchangeEpoch_ = 0;
    applyBearer(token);

    std::deque<PendingRequest> retries;
    retries.swap(retryQueue_);
    while (!retries.empty()) {
        PendingRequest pending = std::move(retries.front());
        retries.pop_front();
        if (pending.epoch != requestEpoch_) {
            continue;
        }
        auth::makeStudioApiHeaders(bearer_.jwt, deviceId_, pending.method, pending.data)
                .apply(pending.request);
        enqueueRequest(std::move(pending.request), std::move(pending.method),
                       std::move(pending.data), std::move(pending.callback),
                       /*retriedAuth=*/true, pending.epoch);
    }
    flushAuthWaiters();
}

void SunoClient::onClerkAuthFailedInternal(const QString& reason,
                                           auth::AuthFailureKind kind) {
    bearer_ = auth::BearerToken{};
    touchInFlight_ = false;
    setAuthFailureKind(kind);
    LOG_ERROR("SunoClient: clerk auth exchange failed: {}", reason.toStdString());
    dropPendingAuthWork(reason);
    setState(auth::AuthState::NeedsReauth);
    emit errorOccurred(reason.toStdString());
    emit needsReauth();
}

void SunoClient::flushAuthWaiters() {
    std::vector<AuthWaiter> waiters;
    waiters.swap(authWaiters_);
    for (AuthWaiter& waiter : waiters) {
        if (waiter.epoch == requestEpoch_ && waiter.proceed) {
            waiter.proceed();
        }
    }
}

void SunoClient::dropPendingAuthWork(const QString& reason) {
    refreshTimer_->stop();
    const quint64 epochBefore = requestEpoch_;
    invalidateRequestEpoch(reason);
    const quint64 expectedEpoch =
            epochBefore == std::numeric_limits<quint64>::max() ? 1 : epochBefore + 1;
    if (requestEpoch_ == expectedEpoch) {
        resetClerkClient();
    }
}

// ─────────────────────────────────────────────────────────────
// Request plumbing
// ─────────────────────────────────────────────────────────────

void SunoClient::withValidToken(std::function<void()> proceed) {
    const quint64 epoch = requestEpoch_;
    if (authState_ == auth::AuthState::ActiveValid && !bearer_.jwt.isEmpty()) {
        proceed();
        return;
    }
    if (credentialStoreWorker_->isRestoreInFlight()) {
        authWaiters_.push_back({epoch, std::move(proceed)});
        return;
    }
    if (!hasCredentials() || authState_ == auth::AuthState::Disconnected ||
        (authState_ == auth::AuthState::NeedsReauth && !touchInFlight_)) {
        if (!hasCredentials()) {
            setState(auth::AuthState::Disconnected);
        }
        LOG_WARN("SunoClient: blocked authenticated request without a credential");
        errorOccurred.emitSignal("Not authenticated");
        return;
    }
    authWaiters_.push_back({epoch, std::move(proceed)});
    ensureFreshBearer();
}

void SunoClient::enqueueAuthenticatedRequest(const QString& endpoint,
                                             const std::string& method,
                                             const QByteArray& data,
                                             std::function<void(QNetworkReply*)> callback,
                                             bool retryOnUnauthorized) {
    if (method != "GET" && method != "POST") {
        rejectAuthenticatedRequest(QStringLiteral("unsupported authenticated HTTP method"));
        return;
    }

    const auto url = resolveStudioApiUrl(endpoint);
    if (!url) {
        rejectAuthenticatedRequest(
                QStringLiteral("authenticated request blocked outside the captured Studio host"));
        return;
    }

    withValidToken([this, url = *url, method, data,
                    callback = std::move(callback), retryOnUnauthorized]() mutable {
        auto request = createAuthenticatedRequest(url, method, data);
        if (!request) {
            rejectAuthenticatedRequest(QStringLiteral("authenticated request has no bearer"));
            return;
        }
        enqueueRequest(std::move(*request), method, std::move(data),
                       std::move(callback), false, requestEpoch_, retryOnUnauthorized);
    });
}

std::optional<QUrl> SunoClient::resolveStudioApiUrl(const QString& endpoint) const {
    QUrl url(endpoint);
    if (url.isRelative()) {
        if (endpoint.startsWith(QStringLiteral("//")) ||
            !endpoint.startsWith(QStringLiteral("/"))) {
            return std::nullopt;
        }
        url = QUrl(API_BASE + endpoint);
    }
    if (!auth::isAllowedStudioApiUrl(url)) {
        return std::nullopt;
    }
    return url;
}

std::optional<QNetworkRequest> SunoClient::createAuthenticatedRequest(
        const QUrl& url, const std::string& method, const QByteArray& data) {
    if (authState_ != auth::AuthState::ActiveValid || bearer_.jwt.isEmpty() ||
        !auth::isAllowedStudioApiUrl(url)) {
        return std::nullopt;
    }
    QNetworkRequest request(url);
    auth::makeStudioApiHeaders(bearer_.jwt, deviceId_, method, data).apply(request);
    return request;
}

void SunoClient::rejectAuthenticatedRequest(const QString& reason) {
    LOG_ERROR("SunoClient: {}", reason.toStdString());
    errorOccurred.emitSignal(reason.toStdString());
}

void SunoClient::enqueueRequest(QNetworkRequest req, const std::string& method,
                                QByteArray data,
                                std::function<void(QNetworkReply*)> callback,
                                bool retriedAuth,
                                quint64 epoch,
                                bool retryOnUnauthorized) {
    const quint64 pendingEpoch = epoch == 0 ? requestEpoch_ : epoch;
    if (pendingEpoch != requestEpoch_ ||
        authState_ != auth::AuthState::ActiveValid || bearer_.jwt.isEmpty() ||
        !auth::isAllowedStudioApiUrl(req.url()) ||
        (method != "GET" && method != "POST")) {
        rejectAuthenticatedRequest(QStringLiteral("authenticated request blocked before send"));
        return;
    }
    requestQueue_.push_back({std::move(req), method, std::move(data),
                             std::move(callback), retriedAuth,
                             retryOnUnauthorized, pendingEpoch});
    if (!queueTimer_->isActive()) {
        queueTimer_->start();
    }
}

void SunoClient::processQueue() {
    while (!requestQueue_.empty() && requestQueue_.front().epoch != requestEpoch_) {
        requestQueue_.pop_front();
    }
    if (requestQueue_.empty()) {
        queueTimer_->stop();
        return;
    }

    PendingRequest pending = std::move(requestQueue_.front());
    requestQueue_.pop_front();
    if (pending.epoch != requestEpoch_ ||
        authState_ != auth::AuthState::ActiveValid || bearer_.jwt.isEmpty()) {
        if (requestQueue_.empty()) {
            queueTimer_->stop();
        }
        return;
    }

    QNetworkReply* reply = nullptr;
    if (replyFactory_) {
        reply = replyFactory_(pending.request, pending.method, pending.data);
    } else if (pending.method == "POST") {
        reply = manager_->post(pending.request, pending.data);
    } else {
        reply = manager_->get(pending.request);
    }

    if (!reply) {
        rejectAuthenticatedRequest(QStringLiteral("authenticated request could not be started"));
        if (requestQueue_.empty()) {
            queueTimer_->stop();
        }
        return;
    }
    if (pending.epoch != requestEpoch_ ||
        authState_ != auth::AuthState::ActiveValid || bearer_.jwt.isEmpty()) {
        reply->deleteLater();
        if (requestQueue_.empty()) {
            queueTimer_->stop();
        }
        return;
    }

    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, pending = std::move(pending)]() mutable {
                handleReplyFinished(reply, std::move(pending));
            });

    if (requestQueue_.empty()) {
        queueTimer_->stop();
    }
}

void SunoClient::handleReplyFinished(QNetworkReply* reply, PendingRequest&& pending) {
    if (!reply) {
        return;
    }
    const bool tracked = isTrackedReply(reply);
    removeTrackedReply(reply);
    if (!tracked || pending.epoch != requestEpoch_ ||
        authState_ != auth::AuthState::ActiveValid) {
        reply->deleteLater();
        return;
    }

    const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) {
        if (pending.retryOnUnauthorized && !pending.retriedAuth && hasCredentials()) {
            LOG_WARN("SunoClient: 401 on {} - refreshing bearer, retrying once",
                     pending.request.url().toString().toStdString());
            reply->deleteLater();
            bearer_ = auth::BearerToken{};
            pending.retriedAuth = true;
            retryQueue_.push_back(std::move(pending));
            ensureFreshBearer(/*force=*/true);
            return;
        }
        handleNetworkError(reply);
        reply->deleteLater();
        return;
    }

    if (!pending.callback) {
        reply->deleteLater();
        return;
    }
    pending.callback(reply);
}

void SunoClient::handleJsonReply(QNetworkReply* reply,
                                  std::function<void(const QJsonDocument&)> handler) {
    if (!reply) {
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply);
        reply->deleteLater();
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    handler(document);
}

void SunoClient::handleNetworkError(QNetworkReply* reply) {
    if (!reply) {
        return;
    }
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    std::string err = reply->errorString().toStdString();
    if (isAuthFailure(httpStatus, reply->errorString())) {
        err = "Unauthorized: Token expired";
        bearer_ = auth::BearerToken{};
        dropPendingAuthWork(QStringLiteral("Studio authentication lost"));
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
    Q_UNUSED(searchText);

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

    QJsonObject body;
    body["cursor"] = cursor.has_value() ? QJsonValue(*cursor) : QJsonValue::Null;
    body["limit"] = limit;
    body["filters"] = filters;

    enqueueAuthenticatedRequest(
            qstr(vc::suno::endpoints::LIBRARY_FEED), "POST",
            QJsonDocument(body).toJson(),
            [this](QNetworkReply* reply) { onLibraryReply(reply); });
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

void SunoClient::generate(const std::string&, const std::string&,
                          bool, const std::string&) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }

    errorOccurred.emitSignal(
            "Generation is unavailable until the captured CAPTCHA token flow is supported");
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
    const QString url = qstr(vc::suno::endpoints::ALIGNED_LYRICS)
                                .replace("{}", QString::fromStdString(clipId));
    enqueueAuthenticatedRequest(
            url, "GET", {}, [this, clipId](QNetworkReply* reply) {
                handleJsonReply(reply, [this, clipId](const QJsonDocument& doc) {
                    alignedLyricsFetched.emitSignal(
                            clipId, doc.toJson(QJsonDocument::Compact).toStdString());
                });
            });
}

void SunoClient::initiateWavConversion(const std::string&) {
    errorOccurred.emitSignal("WAV conversion is unavailable from capture-backed contracts");
}

void SunoClient::onWavConversionInitiated(const std::string&, QNetworkReply* reply) {
    reply->deleteLater();
}

void SunoClient::cancelPoll(const std::string& clipId) {
    cancelledPolls_.insert(QString::fromStdString(clipId));
}

void SunoClient::pollWavFile(const std::string&, int) {
    errorOccurred.emitSignal("WAV conversion is unavailable from capture-backed contracts");
}

} // namespace vc::suno
