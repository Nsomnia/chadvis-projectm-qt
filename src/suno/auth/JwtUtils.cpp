#include "JwtUtils.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>

namespace vc::suno::auth {
namespace {

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
    if (!claims.contains(QStringLiteral("exp"))) {
        return 0;
    }
    const qint64 exp = claims.value(QStringLiteral("exp")).toInteger(-1);
    return exp > 0 ? exp : 0;
}

bool JwtUtils::isExpired(const QJsonObject& claims, qint64 graceSecs) {
    const qint64 exp = expiryEpochSecs(claims);
    if (exp <= 0) {
        return false;
    }
    const qint64 nowPlusGrace = QDateTime::currentSecsSinceEpoch() + graceSecs;
    return nowPlusGrace >= exp;
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

} // namespace vc::suno::auth
