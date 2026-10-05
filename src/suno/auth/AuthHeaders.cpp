#include "AuthHeaders.hpp"

#include "suno/CapturedHosts.hpp"
#include "suno/HttpPolicy.hpp"

#include <QHttpHeaders>
#include <QUrl>

namespace vc::suno::auth {

void StudioApiHeaders::apply(QNetworkRequest& request) const {
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    // The manual redirect above is the fail-closed host check; this is the other
    // half of the policy and it has its own owner. Every /api/* call goes through
    // this function, so this is the one line that bounds all of them -- which is
    // exactly why the timeout did not live here before.
    http::applyRequestPolicy(request, http::RequestClass::JsonApi);
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
    // Delegates the *rules* to the one owner (`vc::suno::hosts`) and keeps only
    // the one fact that is specific to this function: which ROLE of host may
    // carry a bearer.
    //
    // The role check is load-bearing, not decoration. `classifyRefusal` with a
    // Production tier admits the whole Production set — media, artwork,
    // CloudFront, Clerk, the upload target — and without the role test below
    // this function would silently widen to let the studio API send
    // Authorization headers to any of them. Host list, evidence grade, scheme,
    // port and userinfo rules all come from the registry; the role is ours.
    //
    // Tier is pinned to Production on purpose: this is the production studio
    // API, and a staging URL must not be reachable by sending production
    // credentials at it. Selecting the staging tier is a separate, explicit
    // decision made by `hosts::TierPolicy`, not by a caller's URL.
    if (hosts::classifyRefusal(url, hosts::Tier::Production).has_value()) {
        return false;
    }
    const hosts::HostRule* rule = hosts::findRule(url.host().toLatin1().constData());
    return rule != nullptr && rule->role == hosts::Role::StudioApi;
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
