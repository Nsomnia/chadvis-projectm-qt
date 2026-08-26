#include "ClerkAuthClient.hpp"

#include "AuthHeaders.hpp"
#include "JwtUtils.hpp"
#include "core/Logger.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QUrl>
#include <expected>

namespace vc::suno::auth {
namespace {

std::expected<ClerkClientInfo, QString> parseClientEnvelope(const QByteArray& body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject resp = doc.object()["response"].toObject();
    if (resp.isEmpty()) {
        return std::unexpected(QStringLiteral("Clerk envelope has no 'response' object"));
    }

    ClerkClientInfo info;
    info.clientId = resp["id"].toString();
    info.lastActiveSessionId = resp["last_active_session_id"].toString();

    const QJsonArray sessions = resp["sessions"].toArray();
    for (const auto& entry : sessions) {
        const QJsonObject sessionObj = entry.toObject();
        ClerkSession session;
        session.sessionId = sessionObj["id"].toString();
        session.lastActiveToken =
                JwtUtils::fromJwt(sessionObj["last_active_token"].toObject()["jwt"].toString());
        if (!session.sessionId.isEmpty()) {
            info.sessions.push_back(session);
        }
    }

    if (info.sessions.isEmpty()) {
        return std::unexpected(QStringLiteral("Clerk envelope contained no usable sessions"));
    }
    return info;
}

/// Token of the last-active session (falling back to the first one).
std::expected<BearerToken, QString> bearerFromClientInfo(const ClerkClientInfo& info) {
    const ClerkSession* preferred = info.preferredSession();
    if (preferred == nullptr || preferred->lastActiveToken.isEmpty()) {
        return std::unexpected(QStringLiteral("No active bearer token in Clerk client envelope"));
    }
    return preferred->lastActiveToken;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ClerkAuthClient::ClerkAuthClient(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

ClerkAuthClient::~ClerkAuthClient() {
    abortInflight();
}

void ClerkAuthClient::abortInflight() {
    for (const auto& weak : inflight_) {
        if (QNetworkReply* reply = weak.data()) {
            reply->disconnect(this);
            reply->abort();
        }
    }
    inflight_.clear();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ClerkAuthClient::fetchBearer(const Credentials& creds) {
    startClientFetch(CallContext{creds, /*sessionId=*/{}, /*allowFallback=*/true});
}

void ClerkAuthClient::touch(const Credentials& creds, const QString& sessionId) {
    if (sessionId.isEmpty()) {
        emit authFailed(QStringLiteral("touch requires a session id"));
        return;
    }
    startTouch(CallContext{creds, sessionId, /*allowFallback=*/true});
}

// ---------------------------------------------------------------------------
// Request plumbing
// ---------------------------------------------------------------------------

QNetworkRequest ClerkAuthClient::makeRequest(const QUrl& url, const Credentials& creds,
                                             bool isPost) {
    QNetworkRequest request(url);
    request.setRawHeader("Cookie", creds.cookieHeader.toUtf8());
    request.setRawHeader("Origin", "https://suno.com");
    request.setRawHeader("Referer", "https://suno.com/");
    request.setRawHeader("User-Agent", kBrowserUserAgent);
    if (isPost) {
        // Clerk expects form-encoded POSTs; an empty body with this content
        // type is exactly what the browser sends for touch.
        request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    }
    return request;
}

QString clerkQuerySuffix() {
    return QStringLiteral("__clerk_api_version=%1&_clerk_js_version=%2")
            .arg(ClerkAuthClient::CLERK_API_VERSION, ClerkAuthClient::CLERK_JS_VERSION);
}

void ClerkAuthClient::startClientFetch(CallContext ctx) {
    const QString url = QStringLiteral("%1/client?%2").arg(AUTH_BASE, clerkQuerySuffix());
    QNetworkReply* reply = nam_->get(makeRequest(QUrl(url), ctx.creds, /*isPost=*/false));

    inflight_.push_back(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, ctx = std::move(ctx)]() mutable { handleReply(reply, std::move(ctx)); });
}

void ClerkAuthClient::startTouch(CallContext ctx) {
    const QString url = QStringLiteral("%1/client/sessions/%2/touch?%3")
                                .arg(AUTH_BASE, ctx.sessionId, clerkQuerySuffix());
    QNetworkReply* reply =
            nam_->post(makeRequest(QUrl(url), ctx.creds, /*isPost=*/true), QByteArray());

    inflight_.push_back(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, ctx = std::move(ctx)]() mutable { handleReply(reply, std::move(ctx)); });
}

void ClerkAuthClient::startLegacyFallback(CallContext ctx) {
    // Legacy prototype path - kept as a logged fallback ONLY. If this fires
    // regularly the captured contract has drifted and needs re-capture.
    LOG_WARN("ClerkAuthClient: fallback fired, needs fresh capture");

    const QString url = QStringLiteral("%1/v1/client/sessions/%2/client"
                                       "?_is_native=true&_clerk_js_version=%3")
                                .arg(LEGACY_BASE, ctx.sessionId, CLERK_JS_VERSION);
    QNetworkReply* reply =
            nam_->post(makeRequest(QUrl(url), ctx.creds, /*isPost=*/true), QByteArray());

    inflight_.push_back(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        inflight_.removeOne(reply);
        handleLegacyBody(reply->readAll());
    });
}

// ---------------------------------------------------------------------------
// Reply handling
// ---------------------------------------------------------------------------

void ClerkAuthClient::handleReply(QNetworkReply* reply, CallContext ctx) {
    reply->deleteLater();
    inflight_.removeOne(reply);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    const QByteArray body = reply->readAll();

    if (ok) {
        handleEnvelopeBody(body, ctx);
        return;
    }

    const QString reason = QStringLiteral("HTTP %1: %2")
                                   .arg(status)
                                   .arg(reply->errorString());
    LOG_ERROR("ClerkAuthClient: primary request failed ({})", reason.toStdString());

    // At most ONE fallback attempt per request; only when we know a session id.
    const QString sid = !ctx.sessionId.isEmpty() ? ctx.sessionId : lastKnownSessionId_;
    if (ctx.allowFallback && !sid.isEmpty()) {
        ctx.sessionId = sid;
        startLegacyFallback(std::move(ctx));
        return;
    }
    emit authFailed(reason);
}

void ClerkAuthClient::handleEnvelopeBody(const QByteArray& body, const CallContext& ctx) {
    auto envelope = parseClientEnvelope(body);
    if (!envelope) {
        LOG_ERROR("ClerkAuthClient: envelope parse failed: {}",
                  envelope.error().toStdString());
        emit authFailed(envelope.error());
        return;
    }

    lastKnownSessionId_ = envelope->lastActiveSessionId.isEmpty()
                                  ? envelope->sessions.first().sessionId
                                  : envelope->lastActiveSessionId;

    auto bearer = bearerFromClientInfo(*envelope);
    if (!bearer) {
        emit authFailed(bearer.error());
        return;
    }
    emit bearerReady(*bearer);
}

void ClerkAuthClient::handleLegacyBody(const QByteArray& body) {
    // Legacy shape: fresh JWT at response.jwt, sometimes at top-level "jwt".
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject root = doc.object();
    QString jwt = root["response"].toObject()["jwt"].toString();
    if (jwt.isEmpty()) {
        jwt = root["jwt"].toString();
    }
    if (jwt.isEmpty()) {
        LOG_ERROR("ClerkAuthClient: legacy fallback returned no jwt");
        emit authFailed(QStringLiteral("legacy fallback returned no jwt"));
        return;
    }
    LOG_INFO("ClerkAuthClient: legacy fallback produced a bearer token");
    emit bearerReady(JwtUtils::fromJwt(jwt));
}

} // namespace vc::suno::auth
