#pragma once
// AuthTypes.hpp - value types shared across the Suno auth subsystem.
// Pure data with trivial queries only; BearerToken is declared as a Qt
// metatype so it can cross queued signal/slot connections.

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

namespace vc::suno::auth {

/// Lifecycle of the auth module as seen by the rest of the app.
enum class AuthState {
    Disconnected, ///< No credentials on file / nothing attempted yet.
    ActiveValid,  ///< Holding a bearer token believed to be unexpired.
    NeedsReauth   ///< Last auth exchange failed; user must re-capture cookies.
};

/// A Clerk-issued JWT plus its decoded expiry (UTC).
struct BearerToken {
    /// Refresh proactively when the token is within this margin of expiring.
    static constexpr qint64 kExpirySoonSecs = 5 * 60;

    QString jwt;
    QDateTime expiresAt{}; // always UTC

    [[nodiscard]] bool isEmpty() const { return jwt.isEmpty(); }

    /// True when the token will expire within kExpirySoonSecs (or already has).
    [[nodiscard]] bool isExpiringSoon() const {
        return expiresAt.isValid() &&
               expiresAt <= QDateTime::currentDateTimeUtc().addSecs(kExpirySoonSecs);
    }
};

/// One Clerk session belonging to the captured browser client.
struct ClerkSession {
    QString sessionId;
    BearerToken lastActiveToken;
};

/// Top-level Clerk "client" resource (payload of the GET /v1/client envelope).
struct ClerkClientInfo {
    QString clientId;
    QString lastActiveSessionId;
    QList<ClerkSession> sessions;

    /// Session matching lastActiveSessionId, else the first session, else null.
    [[nodiscard]] const ClerkSession* preferredSession() const {
        for (const auto& s : sessions) {
            if (s.sessionId == lastActiveSessionId) return &s;
        }
        return sessions.isEmpty() ? nullptr : &sessions.first();
    }
};

/// Raw credential material captured from the logged-in browser session.
///
/// The Cookie header string is sent to auth.suno.com verbatim: the root
/// credential cookies (__client / __client_uat and their suffixed variants)
/// must not be re-parsed or re-serialized, so we keep exactly what was
/// captured and treat it as an opaque blob.
struct Credentials {
    QString cookieHeader;
};

} // namespace vc::suno::auth

Q_DECLARE_METATYPE(vc::suno::auth::BearerToken)
