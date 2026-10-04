#include "ClerkAuthClient.hpp"

#include "AuthHeaders.hpp"
#include "JwtUtils.hpp"
#include "core/Logger.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1StringView>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <array>
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

/// Why a bearer we decoded is still unusable, as secret-free prose. Same shape
/// as noClerkCookieReason(): the caller must be able to tell which part of a
/// credential was wrong, and no branch may quote any part of the credential.
/// The 2026-08-25 capture shows Clerk issuing exp = iat + 3600, so every case
/// here is a protocol mismatch rather than something to keep using until the
/// server 401s -- which is exactly what the old `exp <= 0` fail-open did.
QString unusableBearerReason(JwtUtils::ExpiryDefect defect) {
    switch (defect) {
        case JwtUtils::ExpiryDefect::Missing:
            return QStringLiteral("Clerk returned a bearer token with no \"exp\" claim, so its "
                                  "validity cannot be checked or refreshed");
        case JwtUtils::ExpiryDefect::NotIntegral:
            return QStringLiteral("Clerk returned a bearer token whose \"exp\" claim is not an "
                                  "integer number of seconds");
        case JwtUtils::ExpiryDefect::NotPositive:
            return QStringLiteral("Clerk returned a bearer token whose \"exp\" claim is not a "
                                  "positive epoch-seconds value");
        case JwtUtils::ExpiryDefect::Elapsed:
            return expiredBearerReason();
        case JwtUtils::ExpiryDefect::None:
            break;
    }
    return {};
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
    // One classification, two questions answered. The 300 s grace is
    // isExpired()'s historical default: a token inside it is stale, not dead,
    // and must not be the one we keep -- but it is still a *known* lifetime.
    // The fail-open this replaces was isExpired()'s `if (exp <= 0) return
    // false;`, which read a missing "exp" (expiryEpochSecs() reports 0 for it)
    // as "never expires" and installed the token with no refresh timer armed.
    // Anything that is not exactly ExpiryDefect::None is now refused, and the
    // reason names which clause failed.
    const JwtUtils::ExpiryDefect defect =
            JwtUtils::expiryDefect(*claims, JwtUtils::kDefaultExpiryGraceSecs);
    if (defect != JwtUtils::ExpiryDefect::None) {
        return std::unexpected(EnvelopeParseError{
                AuthFailureKind::ProtocolMismatch,
                unusableBearerReason(defect),
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

/// The three Clerk cookie families relevant to the captured sign-in flow,
/// longest first. Clerk **instance-key-suffixes** the name it writes, so family
/// membership is a prefix test and never equality: the 2026-08-25 capture puts
/// `__client_Jnxw-muT` and `__client_uat_Jnxw-muT` on the wire, and
/// `docs/suno_api/OAUTH_REDIRECT_ANALYSIS.md` names the families as "`__client`
/// and its instance-key-suffixed variant" (likewise `__client_uat` and
/// `__session`) while warning that the suffix is not a fixed, universal
/// frontend key -- so nothing here may hard-code one. An empty suffix (the bare
/// `__client`) is the same family and must match too.
///
/// The order is longest-first for unambiguous reasoning rather than for
/// matching: `__client` is itself a prefix of `__client_uat`, so testing it
/// first would file the non-secret session-presence cookie under the refresh
/// family.
constexpr std::array<QLatin1StringView, 3> kClerkCookieFamilies{
        QLatin1StringView("__client_uat"),
        QLatin1StringView("__client"),
        QLatin1StringView("__session"),
};

/// Bounds on the "which names were actually seen" diagnostic. A cookie *name* is
/// a fixed identifier and is not a secret (its *value* is), so quoting names is
/// what separates "wrong cookie" from "no cookie at all" -- but a name arrives
/// from a user paste, so it is untrusted text that must not reach a log verbatim.
constexpr qsizetype kMaxReportedNames = 8;
constexpr qsizetype kMaxReportedNameLength = 64;

/// True for a cookie name that is safe to reproduce in a diagnostic: non-empty,
/// bounded, and drawn from the conventional cookie-name character set only. A
/// jar of real cookies always passes; arbitrary pasted text usually does not,
/// and is then reported as a count instead of a string.
[[nodiscard]] bool isReportableCookieName(const QString& name) {
    if (name.isEmpty() || name.size() > kMaxReportedNameLength) {
        return false;
    }
    for (const QChar c : name) {
        const bool allowed = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                             (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                             (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                             c == QLatin1Char('_') || c == QLatin1Char('-') ||
                             c == QLatin1Char('.') || c == QLatin1Char('$');
        if (!allowed) {
            return false;
        }
    }
    return true;
}

QString expectedCredentialReason() {
    return QStringLiteral("expected a JWT bearer token or a Cookie header containing "
                          "__client, __client_uat, or __session, each with or without "
                          "Clerk's instance-key suffix");
}

/// The wrong-cookie diagnostic: a syntactically valid cookie header that carries
/// none of the three Clerk families. Names the names it saw, because "you
/// pasted the wrong cookie" and "you pasted no cookie header at all" are
/// different user mistakes with different fixes.
QString noClerkCookieReason(const QStringList& seenNames, qsizetype withheldCount) {
    QString reason = QStringLiteral("cookie-pair header carrying no Clerk cookie out of "
                                    "__client, __client_uat, or __session");
    if (seenNames.isEmpty()) {
        reason += QStringLiteral("; no conventional cookie name could be reported");
    } else {
        reason += QStringLiteral("; saw cookie name(s): %1")
                          .arg(seenNames.join(QStringLiteral(", ")));
    }
    if (withheldCount > 0) {
        reason += QStringLiteral("; %1 further name(s) withheld").arg(withheldCount);
    }
    return reason;
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
    QStringList seenNames;
    qsizetype withheldNameCount = 0;
    const QStringList parts = cookieHeader.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString pair = part.trimmed();
        const qsizetype separator = pair.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        hasNameValuePair = true;
        const QString name = pair.left(separator).trimmed();
        for (const QLatin1StringView family : kClerkCookieFamilies) {
            if (name.startsWith(family)) {
                hasClerkCookie = true;
                break;
            }
        }

        if (seenNames.size() < kMaxReportedNames && isReportableCookieName(name)) {
            seenNames << name;
        } else {
            ++withheldNameCount;
        }
    }

    if (hasNameValuePair && hasClerkCookie) {
        return {StoredCredentialShape::ClerkCookieHeader,
                AuthFailureKind::None,
                QStringLiteral("stored credential shape: Clerk cookie header; accepted")};
    }
    if (!hasNameValuePair) {
        // Syntactic JWT acceptance only -- this function classifies shape, and
        // an exp-less token is still recognisably a JWT. Whether it may actually
        // be used is decided where it is installed, via
        // JwtUtils::hasUsableLifetime(): SunoClient::setToken for a paste and
        // SunoClient::applyRestoreResult for a stored one. Do not fold the
        // lifetime check in here; that would make "shape" mean "usable" and
        // change StoredCredentialShape::BearerToken's documented meaning.
        if (JwtUtils::claims(normalized).has_value()) {
            return {StoredCredentialShape::BearerToken,
                    AuthFailureKind::None,
                    QStringLiteral("stored credential shape: JWT bearer token; accepted")};
        }
    }

    const QString detectedShape =
            hasNameValuePair
                    ? noClerkCookieReason(seenNames, withheldNameCount)
                    : QStringLiteral("unrecognized non-cookie value");
    return {StoredCredentialShape::Unsupported,
            AuthFailureKind::NoActiveSession,
            QStringLiteral("stored credential shape: %1; no active session; %2")
                    .arg(detectedShape, expectedCredentialReason())};
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
