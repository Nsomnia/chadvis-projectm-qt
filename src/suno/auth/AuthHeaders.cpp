#include "AuthHeaders.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

namespace vc::suno::auth {
namespace {

/// Build the Browser-Token payload value.
///
/// LEAD-CAPTURE-NOTE: exact inner recipe undocumented, replace after fresh
/// capture. Current best knowledge: base64 of compact JSON containing the
/// request timestamp in epoch milliseconds, wrapped as {"token":"<b64>"}.
QByteArray buildBrowserToken() {
    const QJsonObject inner{{"timestamp",
                             static_cast<qint64>(QDateTime::currentMSecsSinceEpoch())}};
    const QByteArray b64 =
            QJsonDocument(inner).toJson(QJsonDocument::Compact).toBase64();
    const QJsonObject outer{{"token", QString::fromLatin1(b64)}};
    return QJsonDocument(outer).toJson(QJsonDocument::Compact);
}

} // namespace

void StudioApiHeaders::apply(QNetworkRequest& request) const {
    if (!authorization.isEmpty()) {
        request.setRawHeader("Authorization", authorization);
    }
    if (!deviceId.isEmpty()) {
        request.setRawHeader("Device-Id", deviceId);
    }
    if (!browserToken.isEmpty()) {
        // LEAD-CAPTURE-NOTE: header name itself unverified post-capture.
        request.setRawHeader("Browser-Token", browserToken);
    }
    request.setRawHeader("Origin", origin);
    request.setRawHeader("Referer", referer);
    request.setRawHeader("User-Agent", userAgent);
    request.setRawHeader("Accept", accept);
    if (!contentType.isEmpty()) {
        request.setRawHeader("Content-Type", contentType);
    }
}

StudioApiHeaders makeStudioApiHeaders(const QString& bearerJwt,
                                      const QString& persistedDeviceId) {
    StudioApiHeaders h;
    h.authorization = bearerJwt.isEmpty()
                              ? QByteArray()
                              : QByteArray("Bearer ") + bearerJwt.toUtf8();
    h.browserToken = buildBrowserToken();
    h.deviceId = persistedDeviceId.toUtf8();
    h.origin = QByteArrayLiteral("https://suno.com");
    h.referer = QByteArrayLiteral("https://suno.com/");
    h.userAgent = QByteArray(kBrowserUserAgent);
    h.accept = QByteArrayLiteral("application/json,text/plain,*/*");
    h.contentType = QByteArrayLiteral("application/json");
    return h;
}

} // namespace vc::suno::auth
