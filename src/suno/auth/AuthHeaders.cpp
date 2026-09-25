#include "AuthHeaders.hpp"

#include <QHttpHeaders>
#include <QUrl>

namespace vc::suno::auth {

void StudioApiHeaders::apply(QNetworkRequest& request) const {
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    QHttpHeaders headers = request.headers();
    if (authorization.isEmpty()) {
        headers.removeAll("Authorization");
    } else {
        headers.replaceOrAppend("Authorization", authorization);
    }
    if (deviceId.isEmpty()) {
        headers.removeAll("Device-Id");
    } else {
        headers.replaceOrAppend("Device-Id", deviceId);
    }
    headers.replaceOrAppend("Origin", origin);
    headers.replaceOrAppend("Referer", referer);
    headers.replaceOrAppend("User-Agent", userAgent);
    headers.replaceOrAppend("Accept", accept);
    if (contentType.isEmpty()) {
        headers.removeAll("Content-Type");
    } else {
        headers.replaceOrAppend("Content-Type", contentType);
    }
    request.setHeaders(headers);
}

QString normalizeCookieHeader(const QString& value) {
    QString normalized = value.trimmed();
    if (normalized.startsWith(QStringLiteral("Cookie:"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(7).trimmed();
    }
    return normalized;
}

bool isAllowedStudioApiUrl(const QUrl& url) {
    return url.isValid() && !url.isRelative() &&
           QString::compare(url.scheme(), QStringLiteral("https"),
                            Qt::CaseInsensitive) == 0 &&
           QString::compare(url.host(), QString::fromLatin1(kStudioApiHost),
                            Qt::CaseInsensitive) == 0 &&
           (url.port() < 0 || url.port() == 443) && url.userInfo().isEmpty();
}

StudioApiHeaders makeStudioApiHeaders(const QString& bearerJwt,
                                      const QString& persistedDeviceId,
                                      std::string_view method,
                                      const QByteArray& data) {
    StudioApiHeaders h;
    h.authorization = bearerJwt.isEmpty()
                              ? QByteArray()
                              : QByteArray("Bearer ") + bearerJwt.toUtf8();
    h.deviceId = persistedDeviceId.toUtf8();
    h.origin = QByteArrayLiteral("https://suno.com");
    h.referer = QByteArrayLiteral("https://suno.com/");
    h.userAgent = QByteArray(kBrowserUserAgent);
    h.accept = QByteArrayLiteral("*/*");
    h.contentType = method == "POST" && !data.isEmpty()
                            ? QByteArrayLiteral("application/json")
                            : QByteArray();
    return h;
}

} // namespace vc::suno::auth
