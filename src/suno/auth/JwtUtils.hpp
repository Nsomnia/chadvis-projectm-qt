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
    /// Grace window isExpired() has always applied: a token inside it is not
    /// dead, it is stale, and the caller should prefer a fresh one.
    static constexpr qint64 kDefaultExpiryGraceSecs = 300;

    /// Why the "exp" claim cannot be used to schedule against a token.
    ///
    /// This enum exists because "absent" and "expired" are different failures
    /// that must not share one representation. expiryEpochSecs() reports 0 for
    /// both a missing claim and epoch zero, and the old isExpired() read that
    /// 0 as `if (exp <= 0) return false;` -- i.e. **never expires**. That fail-open
    /// is what let an exp-less token be installed as a live credential with no
    /// refresh timer ever armed, so every rejection now names its own defect.
    enum class ExpiryDefect {
        None,        ///< Present, integral, positive, and still in the future.
        Missing,     ///< No "exp" claim at all.
        /// Present, but not a whole number of seconds inside `qint64`'s range.
        ///
        /// Deliberately one bucket for four different wrongnesses: not a number
        /// at all (a quoted string, a bool, null), a fractional number, or a
        /// number too large or non-finite to narrow to `qint64` — NaN, ±inf and
        /// 1e300 all land here. They share a fix (refuse) and a remedy (ask for
        /// a new token), so splitting them would add enum values no caller
        /// branches on. The narrowing cast is bounded *before* it happens, which
        /// is why the out-of-range cases are a refusal rather than undefined
        /// behaviour.
        NotIntegral,
        NotPositive, ///< Integral, but <= 0, so not an epoch-seconds instant.
        Elapsed,     ///< Integral and positive, but already reached.
    };

    /// Split "header.payload.signature", base64url-decode the payload segment
    /// (missing padding and the '-'/'_' alphabet both handled) and parse it as
    /// a JSON object. Returns a descriptive error for any malformed input
    /// instead of crashing.
    [[nodiscard]] static std::expected<QJsonObject, QString> claims(const QString& jwt);

    /// Value of the "exp" claim in epoch seconds; 0 when absent/unparseable.
    ///
    /// Lossy on purpose, and only safe where a *usable instant* is wanted
    /// (fromJwt()). Because 0 also means "no such claim", this must never be
    /// used to decide whether a token may be accepted -- use
    /// hasUsableLifetime() or expiryDefect() for that.
    [[nodiscard]] static qint64 expiryEpochSecs(const QJsonObject& claims);

    /// Classify the "exp" claim. `graceSecs` moves the Elapsed boundary forward
    /// exactly as isExpired() always has (0 = already reached, the default 300 =
    /// stale), so switching isExpired() onto this changes no behaviour.
    [[nodiscard]] static ExpiryDefect expiryDefect(const QJsonObject& claims,
                                                   qint64 graceSecs = kDefaultExpiryGraceSecs);

    /// True when the claims carry an expiry worth scheduling against: "exp"
    /// present, integral, positive, and in the future now.
    ///
    /// This is the accept/reject gate, with no grace window on purpose: a token
    /// one second from expiry is usable now and is not this predicate's
    /// business to refuse. Callers that want the "prefer a fresher one" window
    /// use expiryDefect() with kDefaultExpiryGraceSecs instead.
    ///
    /// A token failing this cannot be refreshed, so accepting it would mean "use
    /// it until the server 401s" rather than "use it for the rest of its
    /// lifetime".
    [[nodiscard]] static bool hasUsableLifetime(const QJsonObject& claims);

    /// True when "exp" exists and now + graceSecs has reached it. A token
    /// without a usable "exp" is not "not yet expired" -- it is unusable, and
    /// hasUsableLifetime() is what reports that. This query stays a pure
    /// question about elapsed time so it cannot be mistaken for acceptance.
    [[nodiscard]] static bool isExpired(const QJsonObject& claims,
                                        qint64 graceSecs = kDefaultExpiryGraceSecs);

    /// Tolerant string lookup. Claim keys frequently carry vendor prefixes
    /// such as "suno.com/claims/user_id": an exact key match wins first, then
    /// any key whose path ends with "/name". Empty string when not found.
    [[nodiscard]] static QString claimString(const QJsonObject& claims, std::string_view name);

    /// Convenience: decode a JWT into a BearerToken with the exp-derived
    /// expiry filled in. Never fails -- a token without exp just has an
    /// invalid QDateTime expiry. Decoding is not acceptance: gate on
    /// hasUsableLifetime() before treating the result as a live credential.
    [[nodiscard]] static BearerToken fromJwt(const QString& jwt);
};

} // namespace vc::suno::auth
