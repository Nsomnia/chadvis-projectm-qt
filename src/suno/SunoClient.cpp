#include "SunoClient.hpp"

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
} // namespace

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
            [this](const QString& reason) { onClerkAuthFailedInternal(reason); });

    restoreSession();
}

SunoClient::~SunoClient() = default;

// ─────────────────────────────────────────────────────────────
// Startup: restore + migrate credentials
// ─────────────────────────────────────────────────────────────

void SunoClient::restoreSession() {
    auth::CredentialStore store;

    if (auto stored = store.load("suno/default"); stored.isOk()) {
        credentials_.cookieHeader = stored.value();
    } else {
        migrateLegacyConfigCredentials(store); // may fill credentials_
    }

    // Fast path: a persisted bearer that is still genuinely unexpired.
    if (auto storedBearer = store.load("suno/bearer"); storedBearer.isOk()) {
        const auto& jwt = storedBearer.value();
        auto claims = auth::JwtUtils::claims(jwt);
        if (claims && !auth::JwtUtils::isExpired(*claims, /*graceSecs=*/0)) {
            applyBearer(auth::JwtUtils::fromJwt(jwt));
            return;
        }
    }

    if (!credentials_.cookieHeader.isEmpty()) {
        LOG_INFO("SunoClient: cookie restored from secret storage; fetching bearer");
        ensureFreshBearer(/*force=*/true);
    } else {
        setState(auth::AuthState::Disconnected);
    }
}

void SunoClient::migrateLegacyConfigCredentials(auth::CredentialStore& store) {
    auto& cfg = CONFIG.suno();
    const bool hasCookie = !cfg.cookie.empty();
    const bool hasToken = !cfg.token.empty();
    if (!hasCookie && !hasToken) {
        return;
    }

    const QString legacy = QString::fromStdString(hasCookie ? cfg.cookie : cfg.token);
    auto result = store.store("suno/default", legacy);
    if (result.isErr()) {
        LOG_ERROR("SunoClient: credential migration to keychain failed ({}) - keeping "
                  "legacy TOML values in place",
                  auth::CredentialStore::redact(legacy).toStdString());
        return;
    }

    LOG_INFO("SunoClient: migrated legacy config.toml {} into secret storage",
             hasCookie ? "cookie" : "token");
    cfg.cookie.clear();
    cfg.token.clear();
    std::ignore = CONFIG.save(CONFIG.configPath());
    credentials_.cookieHeader = legacy;
}

// ─────────────────────────────────────────────────────────────
// Credential injection / queries
// ─────────────────────────────────────────────────────────────

bool SunoClient::isAuthenticated() const {
    return !bearer_.jwt.isEmpty() || !credentials_.cookieHeader.isEmpty();
}

bool SunoClient::hasCredentials() const {
    return !credentials_.cookieHeader.isEmpty();
}

void SunoClient::setCookie(const std::string& cookie) {
    const QString value = QString::fromStdString(cookie);
    if (value == credentials_.cookieHeader) {
        return;
    }
    credentials_ = auth::Credentials{value};
    lastActiveSessionId_.clear();
    bearer_ = auth::BearerToken{};
    setState(value.isEmpty() ? auth::AuthState::Disconnected
                             : auth::AuthState::NeedsReauth);

    auth::CredentialStore store;
    if (value.isEmpty()) {
        std::ignore = store.remove("suno/default");
    } else if (auto result = store.store("suno/default", value); result.isErr()) {
        LOG_ERROR("SunoClient: failed to persist cookie ({})",
                  auth::CredentialStore::redact(value).toStdString());
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
    applyBearer(auth::JwtUtils::fromJwt(jwt));
}

void SunoClient::reloadStoredCredentials() {
    auth::CredentialStore store;
    auto stored = store.load("suno/default");
    if (stored.isErr() || stored.value().isEmpty()) {
        return;
    }
    const QString& value = stored.value();
    if (value == credentials_.cookieHeader || value == bearer_.jwt) {
        return; // nothing changed
    }
    // A pasted value may be either a raw JWT or a cookie header.
    if (auth::JwtUtils::claims(value).has_value()) {
        applyBearer(auth::JwtUtils::fromJwt(value));
    } else {
        setCookie(value.toStdString());
    }
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

void SunoClient::applyBearer(const auth::BearerToken& token) {
    bearer_ = token;

    // Session id rides in the standard Clerk "sid" claim.
    if (auto claims = auth::JwtUtils::claims(token.jwt)) {
        lastActiveSessionId_ = auth::JwtUtils::claimString(*claims, "sid");
    }

    auth::CredentialStore store;
    if (auto result = store.store("suno/bearer", token.jwt); result.isErr()) {
        LOG_WARN("SunoClient: could not persist bearer token ({})",
                 auth::CredentialStore::redact(token.jwt).toStdString());
    }

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
    if (!force && !bearer_.jwt.isEmpty()) {
        return; // have something usable; the 401 path handles staleness
    }
    if (!hasCredentials() || touchInFlight_) {
        return;
    }
    touchInFlight_ = true;
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

void SunoClient::onClerkAuthFailedInternal(const QString& reason) {
    touchInFlight_ = false;
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
    if (!bearer_.jwt.isEmpty() || !hasCredentials()) {
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
        setState(auth::AuthState::NeedsReauth);
        emit needsReauth();
    }
    errorOccurred.emitSignal(err);
    LOG_ERROR("SunoClient API Error: {}", err);
}

// ─────────────────────────────────────────────────────────────
// API surface
// ─────────────────────────────────────────────────────────────

void SunoClient::fetchLibrary(int page) {
    if (!isAuthenticated()) {
        errorOccurred.emitSignal("Not authenticated");
        return;
    }
    withValidToken([this, page]() {
        QString url = qstr(vc::suno::endpoints::LIBRARY) +
                      QString("?hide_disliked=true&hide_gen_stems=true&hide_studio_"
                              "clips=true&page=%1")
                              .arg(page - 1);
        enqueueRequest(createAuthenticatedRequest(url), "GET", {},
                       [this](QNetworkReply* reply) { onLibraryReply(reply); });
    });
}

void SunoClient::onLibraryReply(QNetworkReply* reply) {
    handleJsonReply(reply, [this](const QJsonDocument& doc) {
        QJsonArray array;
        if (doc.isObject() && doc.object().contains("clips")) {
            array = doc.object()["clips"].toArray();
        } else if (doc.isArray()) {
            array = doc.array();
        }
        libraryFetched.emitSignal(parseClipArray(array));
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
        // Tolerant parse: accept {"clips": [...]} or a bare array.
        QJsonArray array;
        if (doc.isObject() && doc.object().contains("clips")) {
            array = doc.object()["clips"].toArray();
        } else if (doc.isArray()) {
            array = doc.array();
        }

        std::vector<SunoClip> clips;
        for (const auto& item : array) {
            const QJsonObject obj = item.toObject();
            SunoClip clip;
            clip.id = obj["id"].toString().toStdString();
            clip.status = "pending";
            if (!clip.id.empty()) clips.push_back(clip);
        }
        generationStarted.emitSignal(clips);
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

std::vector<SunoClip> SunoClient::parseClipArray(const QJsonArray& array) {
    std::vector<SunoClip> clips;
    clips.reserve(array.size());
    for (const auto& item : array) {
        QJsonObject obj = item.toObject();
        SunoClip clip;
        clip.id = obj["id"].toString().toStdString();
        clip.title = obj["title"].toString().toStdString();
        if (clip.title.empty()) clip.title = obj["name"].toString().toStdString();
        clip.audio_url = obj["audio_url"].toString().toStdString();
        clip.image_url = obj["image_url"].toString().toStdString();
        clip.status = obj["status"].toString().toStdString();
        QJsonObject meta = obj["metadata"].toObject();
        clip.metadata.prompt = meta["prompt"].toString().toStdString();
        clip.metadata.tags = meta["tags"].toString().toStdString();
        clips.push_back(clip);
    }
    return clips;
}

} // namespace vc::suno
