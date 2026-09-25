#include "ClerkAuthClient.hpp"

#include "AuthHeaders.hpp"
#include "JwtUtils.hpp"
#include "core/Logger.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <expected>
#include <optional>
#include <utility>

namespace vc::suno::auth {
namespace {

struct EnvelopeParseError {
    AuthFailureKind kind;
    QString reason;
};

struct ParsedEnvelope {
    BearerToken bearer;
    QString sessionId;
};

struct TokenCandidate {
    QString jwt;
    QString sessionId;
};

QString noActiveSessionReason() {
    return QStringLiteral("no active session; stored credential is incomplete or signed out");
}

QString rejectedCredentialReason() {
    return QStringLiteral("stored credential was rejected by Clerk");
}

QString unexpectedResponseShapeReason() {
    return QStringLiteral("unexpected Clerk response shape");
}

QString expiredBearerReason() {
    return QStringLiteral("Clerk returned an expired bearer token");
}

QString undecodableBearerReason() {
    return QStringLiteral("Clerk returned a bearer token that could not be decoded");
}

EnvelopeParseError malformedResponse() {
    return {AuthFailureKind::MalformedResponse, unexpectedResponseShapeReason()};
}

std::optional<TokenCandidate> tokenFromSessions(const QJsonArray& sessions,
                                                 const QString& activeSessionId) {
    if (activeSessionId.isEmpty()) {
        return std::nullopt;
    }

    for (const QJsonValue& entry : sessions) {
        if (!entry.isObject()) {
            continue;
        }

        const QJsonObject session = entry.toObject();
        if (session["id"].toString() != activeSessionId) {
            continue;
        }
        const QString jwt = session["last_active_token"].toObject()["jwt"].toString();
        if (!jwt.isEmpty()) {
            return TokenCandidate{jwt, activeSessionId};
        }
    }

    return std::nullopt;
}

std::expected<BearerToken, EnvelopeParseError> decodeUsableBearer(const QString& jwt) {
    const auto claims = JwtUtils::claims(jwt);
    if (!claims) {
        return std::unexpected(EnvelopeParseError{
                AuthFailureKind::ProtocolMismatch,
                undecodableBearerReason(),
        });
    }
    if (JwtUtils::isExpired(*claims)) {
        return std::unexpected(EnvelopeParseError{
                AuthFailureKind::ProtocolMismatch,
                expiredBearerReason(),
        });
    }
    return JwtUtils::fromJwt(jwt);
}

/// Parse the captured session envelopes and require an exact active-session
/// selector match before accepting any bearer token.
std::expected<ParsedEnvelope, EnvelopeParseError> parseClientEnvelope(const QByteArray& body) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(malformedResponse());
    }

    const QJsonObject root = doc.object();
    const QJsonValue responseValue = root["response"];
    const QJsonValue clientValue = root["client"];
    if (!responseValue.isObject() && !clientValue.isObject()) {
        return std::unexpected(malformedResponse());
    }

    const QJsonObject response = responseValue.toObject();
    const QJsonObject client = clientValue.toObject();
    const QString responseSessionId =
            response["last_active_session_id"].toString();
    const QString clientSessionId = client["last_active_session_id"].toString();
    const QString activeSessionId = !responseSessionId.isEmpty()
            ? responseSessionId
            : clientSessionId;

    std::optional<TokenCandidate> candidate =
            tokenFromSessions(response["sessions"].toArray(), activeSessionId);
    if (!candidate.has_value()) {
        candidate = tokenFromSessions(client["sessions"].toArray(), activeSessionId);
    }

    if (!candidate.has_value()) {
        return std::unexpected(EnvelopeParseError{
                AuthFailureKind::NoActiveSession,
                noActiveSessionReason(),
        });
    }

    auto bearer = decodeUsableBearer(candidate->jwt);
    if (!bearer) {
        return std::unexpected(bearer.error());
    }

    return ParsedEnvelope{*bearer, candidate->sessionId};
}

} // namespace

StoredCredentialClassification classifyStoredCredential(const QString& value) {
    const QString normalized = normalizeCookieHeader(value);
    if (normalized.isEmpty()) {
        return {StoredCredentialShape::Empty,
                AuthFailureKind::None,
                QStringLiteral("stored credential shape: empty value; no active session")};
    }

    const QString cookieHeader = normalized;
    bool hasNameValuePair = false;
    bool hasClerkCookie = false;
    const QStringList parts = cookieHeader.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString pair = part.trimmed();
        const qsizetype separator = pair.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        hasNameValuePair = true;
        const QString name = pair.left(separator).trimmed();
        if (name == QLatin1String("__client") ||
            name == QLatin1String("__client_uat")) {
            hasClerkCookie = true;
        }
    }

    if (hasNameValuePair && hasClerkCookie) {
        return {StoredCredentialShape::ClerkCookieHeader,
                AuthFailureKind::None,
                QStringLiteral("stored credential shape: Clerk cookie header; accepted")};
    }
    if (!hasNameValuePair) {
        // Use exactly the same syntactic JWT acceptance as token restoration.
        if (JwtUtils::claims(normalized).has_value()) {
            return {StoredCredentialShape::BearerToken,
                    AuthFailureKind::None,
                    QStringLiteral("stored credential shape: JWT bearer token; accepted")};
        }
    }

    const QString detectedShape = hasNameValuePair
            ? QStringLiteral("cookie-pair header without __client or __client_uat")
            : QStringLiteral("unrecognized non-cookie value");
    return {StoredCredentialShape::Unsupported,
            AuthFailureKind::NoActiveSession,
            QStringLiteral("stored credential shape: %1; no active session; expected a JWT bearer token or a Cookie header containing __client or __client_uat")
                    .arg(detectedShape)};
}

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
    failureKind_ = AuthFailureKind::None;
    Credentials normalized{normalizeCookieHeader(creds.cookieHeader)};
    if (normalized.cookieHeader.isEmpty()) {
        emitFailure(AuthFailureKind::NoActiveSession, noActiveSessionReason());
        return;
    }
    startClientFetch(CallContext{std::move(normalized), {}});
}

void ClerkAuthClient::touch(const Credentials& creds, const QString& sessionId) {
    failureKind_ = AuthFailureKind::None;
    Credentials normalized{normalizeCookieHeader(creds.cookieHeader)};
    if (normalized.cookieHeader.isEmpty()) {
        emitFailure(AuthFailureKind::NoActiveSession, noActiveSessionReason());
        return;
    }
    if (sessionId.isEmpty()) {
        emitFailure(AuthFailureKind::ProtocolMismatch,
                    QStringLiteral("touch request requires a session id"));
        return;
    }
    startTouch(CallContext{std::move(normalized), sessionId});
}

// ---------------------------------------------------------------------------
// Request plumbing
// ---------------------------------------------------------------------------

QNetworkRequest ClerkAuthClient::makeRequest(const QUrl& url, const Credentials& creds,
                                             bool isPost) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    const QString cookie = normalizeCookieHeader(creds.cookieHeader);
    if (!cookie.isEmpty()) {
        request.setRawHeader("Cookie", cookie.toUtf8());
    }
    request.setRawHeader("Origin", "https://suno.com");
    request.setRawHeader("Referer", "https://suno.com/");
    request.setRawHeader("User-Agent", kBrowserUserAgent);
    request.setRawHeader("Accept", "*/*");
    if (isPost) {
        request.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    }
    return request;
}

static QString sessionUrl(const QString& sessionId, const QString& action) {
    const QString encodedSessionId =
            QString::fromUtf8(QUrl::toPercentEncoding(sessionId));
    return QStringLiteral("%1/client/sessions/%2/%3")
            .arg(ClerkAuthClient::AUTH_BASE, encodedSessionId, action);
}

void ClerkAuthClient::startClientFetch(CallContext ctx) {
    const QString url = QStringLiteral("%1/client").arg(AUTH_BASE);
    QNetworkReply* reply = nam_->get(makeRequest(QUrl(url), ctx.creds, false));

    inflight_.push_back(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleReply(reply); });
}

void ClerkAuthClient::startTouch(CallContext ctx) {
    const QString url = sessionUrl(ctx.sessionId, QStringLiteral("touch"));
    QNetworkReply* reply = nam_->post(
            makeRequest(QUrl(url), ctx.creds, true), QByteArrayLiteral("intent=focus"));

    inflight_.push_back(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleReply(reply); });
}

// ---------------------------------------------------------------------------
// Reply handling
// ---------------------------------------------------------------------------

AuthFailureKind ClerkAuthClient::classifyHttpFailure(int status) noexcept {
    return status == 401 || status == 403 ? AuthFailureKind::RejectedCredential
                                           : AuthFailureKind::ProtocolMismatch;
}

QString ClerkAuthClient::httpFailureReason(int status) {
    return classifyHttpFailure(status) == AuthFailureKind::RejectedCredential
                   ? rejectedCredentialReason()
                   : QStringLiteral("Clerk request failed with an unexpected HTTP status");
}

void ClerkAuthClient::emitFailure(AuthFailureKind kind, const QString& reason) {
    failureKind_ = kind;
    switch (kind) {
    case AuthFailureKind::NoActiveSession:
        LOG_ERROR("ClerkAuthClient: no active session; stored credential is incomplete or signed out");
        break;
    case AuthFailureKind::RejectedCredential:
        LOG_ERROR("ClerkAuthClient: stored credential was rejected by Clerk");
        break;
    case AuthFailureKind::ProtocolMismatch:
        LOG_ERROR("ClerkAuthClient: {}", reason.toStdString());
        break;
    case AuthFailureKind::MalformedResponse:
        LOG_ERROR("ClerkAuthClient: unexpected Clerk response shape");
        break;
    case AuthFailureKind::None:
        Q_ASSERT(false);
        break;
    }
    emit authFailed(reason);
}

void ClerkAuthClient::handleReply(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    inflight_.removeOne(reply);

    if (ok) {
        handleEnvelopeBody(body, {});
        return;
    }

    emitFailure(classifyHttpFailure(status), httpFailureReason(status));
}

void ClerkAuthClient::handleEnvelopeBody(const QByteArray& body, const CallContext&) {
    auto envelope = parseClientEnvelope(body);
    if (!envelope) {
        emitFailure(envelope.error().kind, envelope.error().reason);
        return;
    }

    lastObservedSessionId_ = envelope->sessionId;
    failureKind_ = AuthFailureKind::None;
    emit bearerReady(envelope->bearer);
}

} // namespace vc::suno::auth
