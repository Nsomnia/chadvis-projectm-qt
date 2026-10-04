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
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <deque>
#include <iterator>
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

/// Reported when a caller asks for the body of a reply this client never issued --
/// or of one whose diagnosis has already been consumed. Named rather than
/// dereferenced: a null reply is a programming error, and an empty QByteArray
/// returned for it would be indistinguishable from "the server sent nothing",
/// which is precisely the silent failure this accessor exists to delete.
QString untrackedBodyMessage() {
    return QStringLiteral(
            "no bounded response body is available for this reply: SunoClient did not "
            "issue it, or its body has already been read");
}

/// Accept-or-explain for a token we are about to install as a live bearer.
/// Returns an empty string when the token may be used, otherwise a
/// secret-free reason naming the clause that failed -- the same shape as
/// ClerkAuthClient's `noClerkCookieReason`, for the same reason: a user who
/// pastes a credential has to be told what was wrong with it.
///
/// No grace window, matching the `graceSecs=0` these sites used before (or, for
/// setToken, not having an expiry check at all): only an already-elapsed or
/// uncheckable token is refused. A token one second short of expiry still passes
/// and scheduleProactiveRefresh() re-arms for it immediately. Only Clerk's
/// bearer exchange uses kDefaultExpiryGraceSecs, because there we can ask for a
/// better token instead of keeping this one.
///
/// Two properties make this safe to log verbatim. `source` is always a literal
/// chosen at the call site, never anything derived from the credential, and no
/// branch interpolates `jwt` -- so the returned text is entirely composed of
/// this file's own string literals and cannot carry a token or cookie.
[[nodiscard]] QString unusableBearerReason(const QString& source, const QString& jwt) {
    const auto claims = auth::JwtUtils::claims(jwt);
    if (!claims) {
        return QStringLiteral("%1 is not a decodable JWT").arg(source);
    }
    if (auth::JwtUtils::hasUsableLifetime(*claims)) {
        return {};
    }
    // Not usable, so name which clause failed. Both calls below pass
    // graceSecs=0 and both read the same function, so this switch cannot claim a
    // different verdict than the check just above it.
    switch (auth::JwtUtils::expiryDefect(*claims, /*graceSecs=*/0)) {
        case auth::JwtUtils::ExpiryDefect::None:
            return {};
        case auth::JwtUtils::ExpiryDefect::Missing:
            return QStringLiteral("%1 has no \"exp\" claim, so its validity cannot be checked "
                                  "or refreshed")
                    .arg(source);
        case auth::JwtUtils::ExpiryDefect::NotIntegral:
            return QStringLiteral("%1 has an \"exp\" claim that is not an integer number of "
                                  "seconds")
                    .arg(source);
        case auth::JwtUtils::ExpiryDefect::NotPositive:
            return QStringLiteral("%1 has an \"exp\" claim that is not a positive epoch-seconds "
                                  "value")
                    .arg(source);
        case auth::JwtUtils::ExpiryDefect::Elapsed:
            return QStringLiteral("%1 expired before it could be used").arg(source);
    }
    return {};
}

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
            // Not "keeping legacy TOML values in place" any more, and the
            // difference is a security property rather than wording.
            // ConfigParsers no longer expands `suno.token`/`suno.cookie` into the
            // serializer at all, so Config::save() physically cannot write a raw
            // `__client=…` cookie to disk. The consequence is that because
            // serialize() rebuilds the whole document, the FIRST save after a
            // failed migration *deletes* the legacy secret from config.toml. The
            // credential survives in memory for this session only, and the user
            // has to paste it again after a restart. That is the correct
            // trade -- a one-year refresh cookie in a 0644 file is the thing
            // worth preventing -- but it has to be said out loud, or the user
            // finds themselves signed out with no explanation.
            LOG_ERROR("SunoClient: credential migration to keychain failed ({}) - the "
                      "credential stays in memory for this session only and will NOT "
                      "survive a restart; re-paste it once the keychain is available",
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
    // Declared out here rather than inside the BearerToken case: an unbraced
    // case cannot hold an initialized declaration, and a braced one re-indents
    // the whole arm away from its siblings. Empty means "not refused".
    QString rejectedBearerReason;
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
            // A bare JWT is the one credential we can use with no Clerk round
            // trip, so this is the only restore path with nothing downstream to
            // catch a bad token: the gate below is the whole validation. The old
            // code accepted whatever decoded, then persisted it as suno/bearer,
            // set ActiveValid and armed no refresh timer -- i.e. a token with no
            // "exp" was used until the server 401'd. Refusal is shaped exactly
            // like the Unsupported case below (same recovery, same reason log,
            // same fall-through), so a bad paste leaves the stored value in place
            // for the user to fix.
            rejectedBearerReason = unusableBearerReason(QStringLiteral("stored bearer"), value);
            if (!rejectedBearerReason.isEmpty()) {
                if (!credentialChanged) {
                    invalidateForRestore(QStringLiteral("stored bearer unusable"),
                                         /*preserve=*/false);
                }
                credentials_ = auth::Credentials{};
                bearer_ = auth::BearerToken{};
                lastActiveSessionId_.clear();
                setAuthFailureKind(auth::AuthFailureKind::NoActiveSession);
                setState(auth::AuthState::NeedsReauth);
                LOG_ERROR("SunoClient: {} (stored value preserved)",
                          rejectedBearerReason.toStdString());
                if (mode == RestoreMode::Reload) {
                    emit tokenChanged(std::string());
                }
                rejectedStoredCredential = true;
                break;
            }
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
        // Rejecting an unusable cached bearer and falling through is deliberate:
        // the cookie branch below still gets its chance, and otherwise the "no
        // active credential" reporting at the end of this function owns the
        // outcome -- exactly as it already did for an *expired* cached bearer,
        // which this gate also covers. The old test was isExpired(graceSecs=0),
        // which answers "false" for a token carrying no "exp" at all, so the
        // unusable case was applied, marked ActiveValid, and left
        // scheduleProactiveRefresh() with no timer to arm.
        const QString reason = unusableBearerReason(QStringLiteral("stored bearer"), jwt);
        if (reason.isEmpty()) {
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
        LOG_WARN("SunoClient: ignoring an unusable stored bearer: {}", reason.toStdString());
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
    // Decodable is necessary, not sufficient. The old gate asked only whether
    // the payload parsed as base64url JSON, so the literal string "aaa.bbb.ccc"
    // and any exp-less token counted as credentials -- the second half is the
    // fail-open JwtUtils::expiryEpochSecs() made possible. Refusing here, before
    // any state is touched, keeps a rejected paste a true no-op exactly as the
    // malformed-input rejection did.
    const QString reason = unusableBearerReason(QStringLiteral("pasted token"), jwt);
    if (!reason.isEmpty()) {
        LOG_WARN("SunoClient: rejected token input: {}", reason.toStdString());
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
    armBodyReader(reply);
}

void SunoClient::armBodyReader(QNetworkReply* reply) {
    if (!reply || bodyReaders_.contains(reply)) {
        return;
    }

    // JsonApi, and hardcoded rather than passed in. Every reply tracked here was
    // built by enqueueAuthenticatedRequest(), whose request already carries
    // http::RequestClass::JsonApi's transfer timeout via StudioApiHeaders::apply
    // (auth/AuthHeaders.cpp:17). Naming the class in one place is what keeps the
    // timeout and the byte cap from being two numbers that can disagree -- the
    // timeout would be 60 s for an Upload and the cap 1 MiB rather than 16, and a
    // caller that guessed wrong would get a cap the request policy never agreed to.
    auto reader =
            http::makeBodyReader(http::RequestClass::JsonApi,
                                 http::maxResponseBytes(http::RequestClass::JsonApi));
    if (!reader) {
        // Unreachable for a class cap HttpPolicy already asserts is usable, but a
        // reader we could not build must not leave the reply unbounded, so the
        // refusal is recorded and enforced rather than swallowed.
        LOG_ERROR("SunoClient: cannot bound a response body: {}",
                  http::describePolicyFailure(reader.error()));
        bodyFailures_.insert(reply, CarriedBodyFailure{reader.error(), false});
        reply->abort();
        return;
    }
    bodyReaders_.insert(reply, std::move(*reader));

    // Pumped from readyRead, not from finished(). This is the whole point: a read
    // that happens after the transfer has completed bounds what we RETAIN, but Qt
    // has already buffered the body, so a hostile origin can keep sending
    // indefinitely. Pumping as bytes arrive is what actually stops them -- the
    // one-byte overrun is detected on the read that crosses it and the reply is
    // aborted there and then.
    connect(reply, &QIODevice::readyRead, this, [this, reply]() { pumpTrackedBody(reply); });
}

void SunoClient::pumpTrackedBody(QNetworkReply* reply) {
    auto it = bodyReaders_.find(reply);
    if (it == bodyReaders_.end()) {
        return;
    }
    // The result is copied out before abort() is called: aborting emits finished(),
    // which lands in handleReplyFinished() and erases this very map entry, so
    // holding `it` across the abort would leave a dangling iterator.
    const auto drained = it->pump(*reply);
    if (drained) {
        return;
    }
    LOG_ERROR("SunoClient: aborting an oversized response: {}",
              http::describePolicyFailure(drained.error()));
    // The refusal is sticky inside the reader and is copied into bodyFailures_ by
    // removeTrackedReply(), so the handler that runs off the abort below reports
    // the limit rather than "Operation canceled".
    reply->abort();
}

void SunoClient::pruneCarriedFailures() {
    for (auto it = bodyFailures_.begin(); it != bodyFailures_.end();) {
        it = (it.key() == nullptr) ? bodyFailures_.erase(it) : std::next(it);
    }
}

void SunoClient::removeTrackedReply(QNetworkReply* reply) {
    // Order matters and is the reason this is one function. The buffered body --
    // up to 16 MiB per reply -- is released here, before the reply is dropped;
    // the sticky PolicyFailure is copied out first, because the reader holding it
    // is what is being destroyed. Without the copy the single most useful
    // diagnostic dies with the buffer, and the reply that was aborted precisely
    // so the user could be told why now reports only a cancelled operation.
    if (const auto it = bodyReaders_.find(reply); it != bodyReaders_.end()) {
        if (const auto failure = it->failure(); failure) {
            // Insert-if-absent, never overwrite. The absent case is the normal one:
            // the reader's failure has not been asked about yet. The present case is
            // the ordering recorded at length in SunoClient.hpp's
            // CarriedBodyFailure -- the handler already ran, already got this
            // sentence from the reader, and its untrack scope guard has now fired.
            // Writing here regardless is what reported the same breach twice.
            if (!bodyFailures_.contains(reply)) {
                bodyFailures_.insert(reply, CarriedBodyFailure{*failure, false});
            }
        }
        bodyReaders_.erase(it);
    }
    pruneCarriedFailures();
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
        // Drop the buffered body rather than carrying a breach nobody will ever
        // report: every one of these replies is disconnected and abandoned, so
        // there is no handler left to name a limit to. This is also the path
        // sign-out takes, and a sign-out must not leave 16 MiB per in-flight
        // reply sitting in a map that only the next untrack would empty.
        bodyReaders_.remove(reply);
        reply->abort();
        if (!weak.isNull()) {
            reply->deleteLater();
        }
    }
    bodyFailures_.clear();
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
        // Reaching this is an internal-contract violation, not a normal state:
        // JwtUtils::fromJwt() leaves expiresAt invalid only when the token has no
        // integral positive "exp", and every path into applyBearer() now gates on
        // JwtUtils::hasUsableLifetime() first. Returning early is still right --
        // scheduling from an invalid QDateTime would compute a garbage delay --
        // but it used to be the *silent* half of the fail-open, so say so.
        LOG_ERROR("SunoClient: bearer has no usable expiry; proactive refresh cannot "
                  "be scheduled and the token will be used until the server rejects it");
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
    // One untrack for every exit below, and deliberately at scope exit rather than
    // at the top of the function: the callback has to be able to call
    // readTrackedBody() while the reader still exists. Untracking first would mean
    // every successful reply was read after its buffer had already been released,
    // which returns the untracked-body error instead of the body -- the mirror
    // image of the empty-readAll() hazard, and just as silent.
    const auto untrack = qScopeGuard([this, reply]() { removeTrackedReply(reply); });
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

std::expected<QByteArray, QString> SunoClient::readTrackedBody(QNetworkReply* reply) {
    if (reply == nullptr) {
        return std::unexpected(untrackedBodyMessage());
    }

    // Take the carried breach for THIS reply, if there is one, before deciding.
    //
    // `bodyFailures_` is keyed on a raw `QNetworkReply*`, so an entry nobody
    // consumes outlives the reply it names -- and a later reply allocated at the
    // same address would inherit a breach that never happened to it. Consuming it
    // here rather than only in the untracked branch means the entry cannot survive
    // a read of its own reply under ANY ordering of `finished` delivery and the
    // untrack scope guard.
    //
    // A `delivered` entry is dropped WITHOUT being reported: that is the
    // "reported once" half of the contract, and it is why the flag exists at all
    // (see CarriedBodyFailure for the observed ordering that requires it).
    std::optional<http::PolicyFailure> carried;
    if (const auto it = bodyFailures_.find(reply); it != bodyFailures_.end()) {
        if (!it->delivered) {
            carried = it->failure;
        }
        bodyFailures_.erase(it);
    }

    if (const auto it = bodyReaders_.find(reply); it != bodyReaders_.end()) {
        // Final drain, and it is load-bearing rather than hygiene. A reply can
        // deliver its last bytes with no readyRead of its own -- a small body
        // that arrives in one chunk, or a reply that finished before the event
        // loop got round to the signal -- so a reader pumped only from readyRead
        // would hand back a short body and look like a server truncation.
        if (const auto drained = it->pump(*reply); !drained) {
            // Marked delivered BEFORE returning, because the caller is the request
            // callback and the untrack scope guard in handleReplyFinished runs the
            // instant it returns -- re-inserting the failure we are reporting right
            // now.
            bodyFailures_.insert(reply, CarriedBodyFailure{drained.error(), true});
            return std::unexpected(http::describePolicyFailureText(drained.error()));
        }
        // Unreachable while `failure_` is set only by a failing pump() and never
        // cleared -- a successful pump above proves there is none. Kept because the
        // two facts live in different translation units, and this branch reports
        // the same way either way.
        if (const auto failure = it->failure(); failure) {
            carried = *failure;
        }
        if (carried) {
            bodyFailures_.insert(reply, CarriedBodyFailure{*carried, true});
            return std::unexpected(http::describePolicyFailureText(*carried));
        }
        return it->body();
    }

    // Untracked, but a breach was recorded as the reader was torn down. `carried`
    // already holds it -- consumed from bodyFailures_ at the top of this function
    // -- so it is reported exactly once, and a second ask about a reply whose body
    // is gone gets the "not available" error rather than a repeated failure.
    if (carried) {
        return std::unexpected(http::describePolicyFailureText(*carried));
    }

    return std::unexpected(untrackedBodyMessage());
}

void SunoClient::handleJsonReply(QNetworkReply* reply,
                                  std::function<void(const QJsonDocument&)> handler) {
    if (!reply) {
        return;
    }
    // The body is read BEFORE the transport error is consulted, and that ordering
    // is the reason a user learns anything. A body that breached its cap was
    // aborted from readyRead, so reply->error() is OperationCanceledError by now
    // and errorString() says "Operation canceled" -- true, and useless. The
    // sticky PolicyFailure carried out of the reader names the limit instead, so
    // it has to be asked for first.
    auto body = readTrackedBody(reply);
    if (!body) {
        LOG_ERROR("SunoClient: {}", body.error().toStdString());
        errorOccurred.emitSignal(body.error().toStdString());
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply);
        reply->deleteLater();
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(*body);
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
            libraryFetched.emitSignal(std::vector<SunoClip>{});
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
            generationStarted.emitSignal(std::vector<SunoClip>{});
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
