#include "JwtUtils.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTimeZone>

#include <cmath>
#include <limits>
#include <optional>

namespace vc::suno::auth {
namespace {

const QString kExpClaim = QStringLiteral("exp");

/// base64url -> bytes. Qt's decoder wants the standard alphabet and explicit
/// padding, so translate the URL-safe characters and re-pad before decoding.
/// AbortOnBase64DecodingErrors keeps garbage input a clean error, not silence.
std::expected<QByteArray, QString> decodeBase64UrlSegment(const QString& segment) {
    QByteArray translated = segment.toLatin1();
    translated.replace('-', '+');
    translated.replace('_', '/');
    if (const int remainder = translated.size() % 4; remainder != 0) {
        translated.append(QByteArray(4 - remainder, '='));
    }
    const QByteArray decoded = QByteArray::fromBase64(
            translated, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.isNull() && !translated.isEmpty()) {
        return std::unexpected(QStringLiteral("JWT payload segment is not valid base64url"));
    }
    return decoded;
}

/// Strict read of the "exp" claim, kept separate from expiryEpochSecs() on
/// purpose. That accessor's 0 sentinel folds "absent", "not a number" and
/// "epoch zero" into one value, which is fine for fromJwt() (it wants an
/// instant) and unsafe for every accept/reject decision. Nullopt here means
/// "no integral epoch-seconds value"; the caller separates absent from
/// malformed so each rejection can say which one happened.
std::optional<qint64> integralExpSecs(const QJsonObject& claims) {
    const QJsonValue value = claims.value(kExpClaim);
    // isDouble() is the JSON-number test, so a quoted "1712345678", a bool,
    // null, an array or an object is rejected here rather than coerced. Qt's
    // parser stores both 1712345678 and 1712345678.0 as Double, which is why
    // the whole-number check below is not redundant with this one.
    if (!value.isDouble()) {
        return std::nullopt;
    }

    const double seconds = value.toDouble();
    // Bound the value BEFORE narrowing: the payload is remote JSON, so 1e300,
    // -1e300 and (if a QJsonValue were ever built programmatically) NaN and
    // infinities all reach here, and casting any of them to qint64 is
    // undefined behaviour. 2^63 is the first double above qint64::max(), and
    // -2^63 is exactly qint64::min(), hence >= on the high bound and < on the
    // low one.
    if (!std::isfinite(seconds) ||
        seconds < static_cast<double>(std::numeric_limits<qint64>::min()) ||
        seconds >= -static_cast<double>(std::numeric_limits<qint64>::min())) {
        return std::nullopt;
    }

    const qint64 epochSecs = static_cast<qint64>(seconds);
    if (static_cast<double>(epochSecs) != seconds) {
        return std::nullopt; // fractional: not a whole number of seconds
    }
    return epochSecs;
}

} // namespace

std::expected<QJsonObject, QString> JwtUtils::claims(const QString& jwt) {
    const QStringList parts = jwt.split('.');
    if (parts.size() != 3 || parts[0].isEmpty() || parts[1].isEmpty() || parts[2].isEmpty()) {
        return std::unexpected(QStringLiteral("JWT must have 3 non-empty dot-separated segments"));
    }

    auto payload = decodeBase64UrlSegment(parts[1]);
    if (!payload) {
        return std::unexpected(payload.error());
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(*payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::unexpected(
                QStringLiteral("JWT payload is not a JSON object: %1").arg(parseError.errorString()));
    }
    return doc.object();
}

qint64 JwtUtils::expiryEpochSecs(const QJsonObject& claims) {
    const std::optional<qint64> epochSecs = integralExpSecs(claims);
    return epochSecs && *epochSecs > 0 ? *epochSecs : 0;
}

JwtUtils::ExpiryDefect JwtUtils::expiryDefect(const QJsonObject& claims, qint64 graceSecs) {
    if (!claims.contains(kExpClaim)) {
        return ExpiryDefect::Missing;
    }
    const std::optional<qint64> epochSecs = integralExpSecs(claims);
    if (!epochSecs) {
        return ExpiryDefect::NotIntegral;
    }
    if (*epochSecs <= 0) {
        return ExpiryDefect::NotPositive;
    }
    if (QDateTime::currentSecsSinceEpoch() + graceSecs >= *epochSecs) {
        return ExpiryDefect::Elapsed;
    }
    return ExpiryDefect::None;
}

bool JwtUtils::hasUsableLifetime(const QJsonObject& claims) {
    // Grace 0, deliberately, so this is the strict absolute question the name
    // promises: is the expiry already reached? Callers that want the
    // "prefer a fresher one" window (Clerk's bearer exchange) ask
    // expiryDefect()/isExpired() with kDefaultExpiryGraceSecs instead. A token
    // one second from expiry still passes here, which is right: it is usable
    // *now* and SunoClient::scheduleProactiveRefresh() re-arms for it in 1 ms.
    return expiryDefect(claims, /*graceSecs=*/0) == ExpiryDefect::None;
}

bool JwtUtils::isExpired(const QJsonObject& claims, qint64 graceSecs) {
    // Only Elapsed counts. Missing/NotIntegral/NotPositive are *unknown*
    // lifetimes, not infinite ones: the old `if (exp <= 0) return false;` read
    // them as "never expires" and that fail-open is the defect this class was
    // introduced to remove. Callers that accept a token must ask
    // hasUsableLifetime(); isExpired() answers the narrower question it always
    // claimed to answer.
    return expiryDefect(claims, graceSecs) == ExpiryDefect::Elapsed;
}

QString JwtUtils::claimString(const QJsonObject& claims, std::string_view name) {
    const QString exact = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    if (claims.contains(exact)) {
        return claims.value(exact).toString();
    }
    // Tolerant fallback: match the tail of slash-prefixed vendor claims.
    const QString suffix = QStringLiteral("/") + exact;
    for (auto it = claims.begin(); it != claims.end(); ++it) {
        if (it.key().endsWith(suffix)) {
            return it.value().toString();
        }
    }
    return {};
}

BearerToken JwtUtils::fromJwt(const QString& jwt) {
    BearerToken token;
    token.jwt = jwt;
    if (auto decoded = claims(jwt)) {
        if (const qint64 exp = expiryEpochSecs(*decoded); exp > 0) {
            token.expiresAt = QDateTime::fromSecsSinceEpoch(exp, QTimeZone::UTC);
        }
    }
    return token;
}

} // namespace vc::suno::auth
