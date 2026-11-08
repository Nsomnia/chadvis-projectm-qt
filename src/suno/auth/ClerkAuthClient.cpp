#include "ClerkAuthClient.hpp"

#include "AuthHeaders.hpp"
#include "JwtUtils.hpp"
#include "core/Logger.hpp"
#include "suno/HttpPolicy.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1StringView>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimeZone>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cmath>
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
    CredentialExpiry expiry;
};

struct TokenCandidate {
    QString jwt;
    QString sessionId;
    QJsonObject session;
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

/// Bounded Clerk `errors[0].code` map (T0341). Clerk's code space is open and
/// its sibling `long_message` is written for a human reading the raw response,
/// so only codes whose meaning this client can state without quoting the body
/// get a fixed sentence. Everything else keeps the HTTP-status sentence, which
/// is why this table is the whole allowlist rather than a prefix match.
struct ClerkErrorCodeSentence {
    const char* code;
    const char* sentence;
};

constexpr std::array<ClerkErrorCodeSentence, 8> kClerkErrorCodeSentences{{
        {"session_not_found",
         "Clerk no longer recognises the stored sign-in session; sign in again"},
        {"client_not_found",
         "Clerk no longer recognises this client; restart the app and sign in again"},
        {"resource_not_found",
         "Clerk could not find the stored sign-in resource; sign in again"},
        {"form_identifier_not_found",
         "Clerk does not recognise the stored account identifier; sign in again"},
        {"form_password_incorrect",
         "stored credential was rejected by Clerk; sign in again"},
        {"verification_failed",
         "Clerk could not verify the stored credential; sign in again"},
        {"requires_captcha",
         "Clerk requires an interactive sign-in before it will accept this account"},
        {"captcha_missing_token",
         "Clerk requires an interactive sign-in before it will accept this account"},
}};

/// Sentence for a known `errors[0].code`, or empty when the code is absent,
/// unknown, or the body is not the Clerk error envelope.
QString clerkErrorCodeSentence(const QByteArray& body) {
    if (body.isEmpty()) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        return {};
    }
    const QJsonArray errors = doc.object().value(QStringLiteral("errors")).toArray();
    if (errors.isEmpty()) {
        return {};
    }
    const QString code = errors.at(0).toObject().value(QStringLiteral("code")).toString();
    if (code.isEmpty()) {
        return {};
    }
    for (const ClerkErrorCodeSentence& entry : kClerkErrorCodeSentences) {
        if (code == QLatin1StringView(entry.code)) {
            return QString::fromLatin1(entry.sentence);
        }
    }
    return {};
}

/// A server-supplied wait is a hint, not a promise: cap it so a wedged origin
/// cannot turn a status line into a sentence about hours, and floor it at one
/// second so a wait already in the past reads as "now", not "in 0 seconds".
/// Seconds are shown, not acted on -- this client has no timer that retries a
/// Clerk exchange.
constexpr std::int64_t kMaxDisplayedRetryAfterSecs = 86'400;

QString rateLimitedReason(int status, const std::optional<std::int64_t> retryAfterSecs) {
    if (!retryAfterSecs.has_value()) {
        return QStringLiteral("Clerk is rate limiting sign-in requests (HTTP %1); try again shortly")
                .arg(status);
    }
    const std::int64_t seconds =
            std::clamp<std::int64_t>(*retryAfterSecs, 1, kMaxDisplayedRetryAfterSecs);
    const QString unit = seconds == 1 ? QStringLiteral("second") : QStringLiteral("seconds");
    return QStringLiteral("Clerk is rate limiting sign-in requests (HTTP %1); try again in %2 %3")
            .arg(status)
            .arg(seconds)
            .arg(unit);
}

/// T0340: the tokens route names no session in its body, so the only in-body
/// statement of which session a minted bearer belongs to is its "sid" claim.
/// A claimant that disagrees with the session the request named handed us
/// somebody else's bearer. Secret-free on purpose: a session id is an account
/// identifier and neither value may reach a log or a reason string.
QString mintedBearerSessionMismatchReason() {
    return QStringLiteral("Clerk minted a bearer token for a session other than the one "
                          "requested");
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

/// Upper epoch-millisecond bound that QDateTime can represent (9999-12-31).
/// A value past this is out of range, not a date to clamp or guess.
constexpr qint64 kMaxEpochMs = 253402300799999LL;

/// Epoch-milliseconds in JSON as either a number (the GET `/v1/client`
/// capture) or a string (the `touch` capture: `expire_at`/`abandon_at` are
/// "epoch-ms strings, not numbers"). Missing, non-numeric, non-integral
/// strings, non-positive, or out-of-range values are `nullopt` -- a
/// fabricated date must never justify an advisory and never reject an
/// otherwise valid credential.
std::optional<QDateTime> epochMsToDateTime(const QJsonValue& value) {
    qint64 ms = 0;
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number <= 0.0 ||
            number > static_cast<double>(kMaxEpochMs)) {
            return std::nullopt;
        }
        ms = static_cast<qint64>(number);
    } else if (value.isString()) {
        const QString text = value.toString().trimmed();
        bool ok = false;
        const qint64 parsed = text.toLongLong(&ok);
        if (!ok || parsed <= 0 || parsed > kMaxEpochMs ||
            text.contains(QLatin1Char('.')) || text.contains(QLatin1Char('e')) ||
            text.contains(QLatin1Char('E'))) {
            return std::nullopt;
        }
        ms = parsed;
    } else {
        return std::nullopt;
    }
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc());
    if (!dateTime.isValid()) {
        return std::nullopt;
    }
    return dateTime;
}

/// `cookie_expires_at` appears at envelope root, inside `response`, or inside
/// `client` depending on which capture is read (LOGIN-CAPTURE-FINALIZATION.md
/// vs TOUCH-TOKENS-SHAPES.md). Check all three and keep the earliest valid
/// value: for an advisory the first death is the only safe date to name.
std::optional<QDateTime> cookieExpiryFromEnvelope(const QJsonObject& root,
                                                  const QJsonObject& response,
                                                  const QJsonObject& client) {
    std::optional<QDateTime> earliest;
    for (const QJsonValue value :
         {root.value(QStringLiteral("cookie_expires_at")),
          response.value(QStringLiteral("cookie_expires_at")),
          client.value(QStringLiteral("cookie_expires_at"))}) {
        const auto dateTime = epochMsToDateTime(value);
        if (dateTime.has_value() && (!earliest.has_value() || *dateTime < *earliest)) {
            earliest = *dateTime;
        }
    }
    return earliest;
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
            return TokenCandidate{jwt, activeSessionId, session};
        }
    }

    return std::nullopt;
}

/// Append every distinct session id either half of the envelope names. Entries
/// without an id name no session and are not counted: the fallback below must
/// be able to name the session it adopts. The split halves are deduplicated
/// because the captured envelopes mirror the same client object twice, so one
/// session legitimately appears in both.
void appendSessionIds(QStringList& ids, const QJsonArray& sessions) {
    for (const QJsonValue& entry : sessions) {
        const QString id = entry.toObject()["id"].toString();
        if (!id.isEmpty() && !ids.contains(id)) {
            ids << id;
        }
    }
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
/// selector match before accepting any bearer token. An envelope that names no
/// selector is accepted only when it names exactly one session (T0342).
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

    // T0342: the web client reads `last_active_session_id ?? sessions?.[0]?.id`,
    // and so do we -- but only when the envelope names exactly one session. An
    // account with several sessions and no selector still refuses, because the
    // choice of which bearer to install would be a guess.
    if (!candidate.has_value() && activeSessionId.isEmpty()) {
        QStringList namedSessionIds;
        appendSessionIds(namedSessionIds, response["sessions"].toArray());
        appendSessionIds(namedSessionIds, client["sessions"].toArray());
        if (namedSessionIds.size() == 1) {
            const QString soleSessionId = namedSessionIds.first();
            candidate = tokenFromSessions(response["sessions"].toArray(), soleSessionId);
            if (!candidate.has_value()) {
                candidate = tokenFromSessions(client["sessions"].toArray(), soleSessionId);
            }
        }
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

    CredentialExpiry expiry;
    expiry.cookieExpiresAt = cookieExpiryFromEnvelope(root, response, client);
    expiry.sessionAbandonAt =
            epochMsToDateTime(candidate->session.value(QStringLiteral("abandon_at")));
    expiry.sessionExpiresAt =
            epochMsToDateTime(candidate->session.value(QStringLiteral("expire_at")));

    return ParsedEnvelope{*bearer, candidate->sessionId, expiry};
}

/// Parse the tokens route's `{ "jwt": ... }` body. Unlike the envelope
/// parser this has no session to select: the caller asked about one
/// session, and the JWT's own `sid` claim is what SunoClient reads back
/// out of it. Extra members are ignored, not rejected: the capture shows
/// exactly one member today, and a future additive member must not turn a
/// working mint into a shape error.
struct ParsedTokensBody {
    BearerToken bearer;
    /// The token's own "sid" claim, or empty when Clerk omitted it.
    QString sessionId;
};

std::expected<ParsedTokensBody, EnvelopeParseError> parseTokensBody(const QByteArray& body)
{
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(malformedResponse());
    }
    const QString jwt = doc.object()[QStringLiteral("jwt")].toString();
    if (jwt.isEmpty()) {
        return std::unexpected(malformedResponse());
    }
    auto bearer = decodeUsableBearer(jwt);
    if (!bearer) {
        return std::unexpected(bearer.error());
    }
    // Decoding already succeeded, so this cannot fail in practice; the guard
    // keeps a missing sid claim from being read out of an absent object.
    const auto claims = JwtUtils::claims(jwt);
    return ParsedTokensBody{*bearer,
                            claims ? JwtUtils::claimString(*claims, "sid") : QString()};
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

const char* authFailureKindId(AuthFailureKind kind) noexcept {
    switch (kind) {
        case AuthFailureKind::None:
            return "none";
        case AuthFailureKind::NoActiveSession:
            return "noSession";
        case AuthFailureKind::RejectedCredential:
            return "rejected";
        case AuthFailureKind::ProtocolMismatch:
            return "protocol";
        case AuthFailureKind::MalformedResponse:
            return "malformed";
    }
    return "none";
}

const char* authFailureMessage(AuthFailureKind kind) noexcept {
    switch (kind) {
        case AuthFailureKind::NoActiveSession:
            return "No active Suno session was found. Paste a valid bearer token or "
                   "the complete Clerk cookie header in Account settings.";
        case AuthFailureKind::RejectedCredential:
            return "Suno rejected the stored credential. Paste a fresh bearer token or "
                   "complete Clerk cookie header in Account settings.";
        case AuthFailureKind::ProtocolMismatch:
            return "Suno sign-in hit an unexpected authentication response. Try again; "
                   "if it persists, refresh the captured sign-in flow.";
        case AuthFailureKind::MalformedResponse:
            return "Suno sign-in returned an unreadable authentication response. Try "
                   "again; if it persists, refresh the captured sign-in flow.";
        case AuthFailureKind::None:
            break;
    }
    return "";
}

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

std::optional<QString> reimportAdvisory(const CredentialExpiry& expiry, QDateTime now) {
    if (!now.isValid()) {
        now = QDateTime::currentDateTimeUtc();
    }
    std::optional<QDateTime> earliest;
    for (const auto& dateTime :
         {expiry.cookieExpiresAt, expiry.sessionAbandonAt, expiry.sessionExpiresAt}) {
        if (dateTime.has_value() && (!earliest.has_value() || *dateTime < *earliest)) {
            earliest = *dateTime;
        }
    }
    if (!earliest.has_value()) {
        return std::nullopt;
    }
    if (*earliest > now.addDays(30)) {
        return std::nullopt;
    }
    const QString dateText =
            QLocale(QLocale::English).toString(*earliest, QStringLiteral("d MMMM yyyy"));
    return QStringLiteral("Re-import your Suno session before %1.").arg(dateText);
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
    // A reader holds a QByteArray of at most 1 MiB; draining the map here rather
    // than leaving it to the reply's deletion is what keeps a torn-down client
    // from holding that much until the last reply dies.
    readers_.clear();
    // Contexts share the readers' lifecycle: a torn-down client must not
    // keep per-reply state for replies it just aborted.
    contexts_.clear();
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
    CallContext ctx{std::move(normalized), sessionId, CallContext::Kind::Touch};
    startTouch(std::move(ctx));
}

void ClerkAuthClient::mintBearerViaTokens(const Credentials& creds, const QString& sessionId) {
    failureKind_ = AuthFailureKind::None;
    Credentials normalized{normalizeCookieHeader(creds.cookieHeader)};
    if (normalized.cookieHeader.isEmpty()) {
        emitFailure(AuthFailureKind::NoActiveSession, noActiveSessionReason());
        return;
    }
    if (sessionId.isEmpty()) {
        emitFailure(AuthFailureKind::ProtocolMismatch,
                    QStringLiteral("tokens request requires a session id"));
        return;
    }
    CallContext ctx{std::move(normalized), sessionId, CallContext::Kind::Tokens};
    startTokens(std::move(ctx));
}

// ---------------------------------------------------------------------------
// Request plumbing
// ---------------------------------------------------------------------------

QNetworkRequest ClerkAuthClient::makeRequest(const QUrl& url, const Credentials& creds,
                                             bool isPost) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    // The credential exchange is the one request whose hang is worst: a wedged
    // Clerk call leaves the user with no session and no way to tell whether the
    // client is working. 30 s of silence is dead; there is no version of this
    // response that legitimately stalls that long.
    http::applyRequestPolicy(request, http::RequestClass::Auth);
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
    ctx.kind = CallContext::Kind::FetchBearer;
    (void)startBounded(
            makeRequest(QUrl(QStringLiteral("%1/client").arg(AUTH_BASE)), ctx.creds, false),
            std::nullopt, std::move(ctx));
}

void ClerkAuthClient::startTouch(CallContext ctx) {
    ctx.kind = CallContext::Kind::Touch;
    (void)startBounded(
            makeRequest(QUrl(sessionUrl(ctx.sessionId, QStringLiteral("touch"))), ctx.creds, true),
            QByteArrayLiteral("intent=focus"), std::move(ctx));
}

void ClerkAuthClient::startTokens(CallContext ctx) {
    ctx.kind = CallContext::Kind::Tokens;
    // Captured shape (2026-09-24): POST, form-urlencoded, EMPTY body. The
    // empty QByteArray is has_value on purpose: nullopt would send a GET.
    (void)startBounded(
            makeRequest(QUrl(sessionUrl(ctx.sessionId, QStringLiteral("tokens"))), ctx.creds, true),
            QByteArray{}, std::move(ctx));
}

QNetworkReply* ClerkAuthClient::startBounded(const QNetworkRequest& request,
                                             const std::optional<QByteArray>& postBody,
                                             CallContext ctx) {
    auto reader = http::makeBodyReader(http::RequestClass::Auth,
                                       http::maxResponseBytes(http::RequestClass::Auth));
    if (!reader) {
        // Unreachable with the current constant cap, but a body we cannot bound
        // must not be requested at all -- that is the whole defect this closes,
        // so it is reported rather than assumed away.
        emitFailure(AuthFailureKind::ProtocolMismatch,
                    http::describePolicyFailureText(reader.error()));
        return nullptr;
    }

    QNetworkReply* reply =
            postBody.has_value() ? nam_->post(request, *postBody) : nam_->get(request);
    inflight_.push_back(reply);
    readers_.insert(reply, *reader);
    contexts_.insert(reply, std::move(ctx));

    // Drain on readyRead, not at finished(). This is the difference between a cap
    // and a post-hoc check: an origin streaming forever is aborted one chunk past
    // the cap, so the bytes never arrive. Draining only in finished() would cap
    // what we *retain* while Qt went on buffering all of it.
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { pumpBody(reply); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { handleReply(reply); });
    return reply;
}

void ClerkAuthClient::pumpBody(QNetworkReply* reply) {
    const auto it = readers_.find(reply);
    if (it == readers_.end()) {
        return;
    }
    if (auto drained = it->pump(*reply); !drained) {
        LOG_ERROR("ClerkAuthClient: {}", http::describePolicyFailure(drained.error()));
        // abort() re-enters through finished(), which is where the user-facing
        // failure is emitted -- once. BodyReader::failure() is sticky precisely so
        // that the aborted reply, now reading as drained, still reports the real
        // limit instead of looking like an ordinary cancellation.
        reply->abort();
    }
}

// ---------------------------------------------------------------------------
// Reply handling
// ---------------------------------------------------------------------------

AuthFailureKind ClerkAuthClient::classifyHttpFailure(int status) noexcept {
    return status == 401 || status == 403 ? AuthFailureKind::RejectedCredential
                                           : AuthFailureKind::ProtocolMismatch;
}

QString ClerkAuthClient::httpFailureReason(int status) {
    if (classifyHttpFailure(status) == AuthFailureKind::RejectedCredential) {
        return rejectedCredentialReason();
    }
    // A rate limit or an origin outage is not a credential verdict, and saying
    // "unexpected HTTP status" made a wait look like a bug. The classification
    // stays ProtocolMismatch: a distinct transient kind would have to be added
    // to every AuthFailureKind vocabulary map in the tree, which is a wider
    // change than the sentence.
    if (status == 429 || status == 430) {
        return rateLimitedReason(status, std::nullopt);
    }
    if (status >= 500 && status <= 599) {
        return QStringLiteral("Clerk is temporarily unavailable (HTTP %1); try again shortly")
                .arg(status);
    }
    return QStringLiteral("Clerk request failed with an unexpected HTTP status");
}

QString ClerkAuthClient::failureReason(int status, const QByteArray& body,
                                       const QByteArray& retryAfterHeader) {
    // A 429's own wait is the most actionable thing the origin said, so it wins
    // over the code table: "try again in 30 seconds" beats any static sentence.
    if (status == 429 || status == 430) {
        return rateLimitedReason(
                status, http::parseRetryAfter(retryAfterHeader,
                                              QDateTime::currentSecsSinceEpoch()));
    }
    if (const QString mapped = clerkErrorCodeSentence(body); !mapped.isEmpty()) {
        return mapped;
    }
    return httpFailureReason(status);
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
    // Read before deleteLater(): the header is the origin's own instruction for
    // when a retry is welcome, and it only exists on the live reply.
    const QByteArray retryAfterHeader = reply->rawHeader("Retry-After");

    // No readAll() anywhere on this path -- an unbounded read of a credential
    // response is exactly the hazard the policy now closes.
    QByteArray body;
    std::optional<http::PolicyFailure> capFailure;
    if (const auto it = readers_.find(reply); it != readers_.end()) {
        if (const auto drained = it->pump(*reply); !drained) {
            capFailure = drained.error();
        }
        if (!capFailure && it->failure().has_value()) {
            // The breach happened on an earlier readyRead and the reply was
            // aborted; report the limit it actually crossed.
            capFailure = *it->failure();
        }
        body = it->body();
        readers_.erase(it);
    } else {
        // No reader, so no usable cap for this body. Never fall back to an
        // unbounded read to "get something"; say so instead.
        capFailure = http::PolicyFailure{http::PolicyError::InvalidByteLimit,
                                         http::RequestClass::Auth, 0, 0};
    }

    reply->deleteLater();
    inflight_.removeOne(reply);
    // The route this reply belongs to decides which body parser runs. A
    // missing entry defaults to the envelope path, which is the shape the
    // two envelope routes share; tokens replies always carry theirs.
    const CallContext ctx = contexts_.take(reply);

    if (capFailure.has_value()) {
        // Protocol mismatch, not a credential problem: an origin answering with
        // more than its own cap is not something a re-sign-in fixes.
        emitFailure(AuthFailureKind::ProtocolMismatch,
                    http::describePolicyFailureText(*capFailure));
        return;
    }

    if (ok) {
        if (ctx.kind == CallContext::Kind::Tokens) {
            handleTokensBody(body, ctx);
        } else {
            handleEnvelopeBody(body, ctx);
        }
        return;
    }

    emitFailure(classifyHttpFailure(status), failureReason(status, body, retryAfterHeader));
}

void ClerkAuthClient::handleTokensBody(const QByteArray& body, const CallContext& ctx) {
    auto tokens = parseTokensBody(body);
    if (!tokens) {
        emitFailure(tokens.error().kind, tokens.error().reason);
        return;
    }

    // T0340: bind the minted bearer to the session that minted it. The tokens
    // body names no session, so an "sid" claim that disagrees with the session
    // this request named means the exchange handed back a bearer belonging to
    // some other session -- refusing is the only correct answer, because using
    // it would mean acting as a session the caller never asked about.
    if (!tokens->sessionId.isEmpty() && tokens->sessionId != ctx.sessionId) {
        emitFailure(AuthFailureKind::ProtocolMismatch, mintedBearerSessionMismatchReason());
        return;
    }

    // An absent "sid" stays tolerated (the route itself named the session), but
    // it must not wipe a session an earlier envelope already named. Only when
    // nothing has been observed yet does the request's own session id stand in
    // for the claim -- it is what SunoClient's touch fallback needs if a later
    // exchange has to name one.
    if (!tokens->sessionId.isEmpty()) {
        lastObservedSessionId_ = tokens->sessionId;
    } else if (lastObservedSessionId_.isEmpty()) {
        lastObservedSessionId_ = ctx.sessionId;
    }
    failureKind_ = AuthFailureKind::None;
    emit bearerReady(tokens->bearer);
}

void ClerkAuthClient::handleEnvelopeBody(const QByteArray& body, const CallContext&) {
    auto envelope = parseClientEnvelope(body);
    if (!envelope) {
        emitFailure(envelope.error().kind, envelope.error().reason);
        return;
    }

    // Advisory input only: even an advisory-grade envelope must not gate the
    // request, so this is stored before the bearer is emitted and read later
    // by whoever surfaces the advisory.
    lastObservedSessionId_ = envelope->sessionId;
    lastCredentialExpiry_ = envelope->expiry;
    failureKind_ = AuthFailureKind::None;
    emit bearerReady(envelope->bearer);
}

} // namespace vc::suno::auth
