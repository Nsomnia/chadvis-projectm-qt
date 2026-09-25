#pragma once
// AuthHeaders.hpp - canonical studio-api header builder.
//
// ALL future /api/* Suno calls must go through makeStudioApiHeaders() so the
// browser-impersonation contract stays in exactly one place. Note: CORS
// preflights are a browser artifact; a native client just sends the headers
// directly.

#include <QNetworkRequest>
#include <QString>

#include <string_view>

class QUrl;

namespace vc::suno::auth {

/// Chrome-on-macOS User-Agent presented to all Suno/Clerk endpoints.
inline constexpr const char* kBrowserUserAgent =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36";
inline constexpr const char* kStudioApiHost = "studio-api-prod.suno.com";

/// Immutable header set for studio-api requests; apply() stamps them onto a
/// QNetworkRequest in one shot.
struct StudioApiHeaders {
    QByteArray authorization; // "Bearer <jwt>" (empty when unauthenticated)
    QByteArray deviceId;
    QByteArray origin;
    QByteArray referer;
    QByteArray userAgent;
    QByteArray accept;
    QByteArray contentType;

    void apply(QNetworkRequest& request) const;
};

[[nodiscard]] QString normalizeCookieHeader(const QString& value);
[[nodiscard]] bool isAllowedStudioApiUrl(const QUrl& url);

[[nodiscard]] StudioApiHeaders makeStudioApiHeaders(const QString& bearerJwt,
                                                    const QString& persistedDeviceId,
                                                    std::string_view method,
                                                    const QByteArray& data);

} // namespace vc::suno::auth
