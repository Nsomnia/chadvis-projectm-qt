#pragma once
// JwtUtils.hpp - decode-only JWT helpers.
//
// We are a client: Clerk signs and verifies tokens on its side, so there is
// deliberately NO signature verification here. These helpers only unpack the
// payload so expiry and claim data can drive refresh scheduling.

#include "AuthTypes.hpp"

#include <QJsonObject>
#include <QString>
#include <expected>
#include <string_view>

namespace vc::suno::auth {

class JwtUtils {
public:
    /// Split "header.payload.signature", base64url-decode the payload segment
    /// (missing padding and the '-'/'_' alphabet both handled) and parse it as
    /// a JSON object. Returns a descriptive error for any malformed input
    /// instead of crashing.
    [[nodiscard]] static std::expected<QJsonObject, QString> claims(const QString& jwt);

    /// Value of the "exp" claim in epoch seconds; 0 when absent/unparseable.
    [[nodiscard]] static qint64 expiryEpochSecs(const QJsonObject& claims);

    /// True when "exp" exists and now + graceSecs has reached it. A token
    /// without "exp" is never considered expired by this check alone.
    [[nodiscard]] static bool isExpired(const QJsonObject& claims, qint64 graceSecs = 300);

    /// Tolerant string lookup. Claim keys frequently carry vendor prefixes
    /// such as "suno.com/claims/user_id": an exact key match wins first, then
    /// any key whose path ends with "/name". Empty string when not found.
    [[nodiscard]] static QString claimString(const QJsonObject& claims, std::string_view name);

    /// Convenience: decode a JWT into a BearerToken with the exp-derived
    /// expiry filled in. Never fails - a token without exp just has an
    /// invalid QDateTime expiry.
    [[nodiscard]] static BearerToken fromJwt(const QString& jwt);
};

} // namespace vc::suno::auth
