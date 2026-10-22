/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#include "suno/MangoLicense.hpp"

#include <QCryptographicHash>
#include <QJsonValue>

#include "suno/MangoCrypto.hpp"

namespace vc::suno {

std::expected<ContentKey, QString>
unwrapContentKey(const QJsonObject& response, const QByteArray& bearer, const QString& clipId) {
    // The endpoint answers with the fields at the top level; a proxy that
    // wraps them in "data" is tolerated because the site's own client reads
    // both shapes.
    const QJsonObject data = response.contains(QStringLiteral("data"))
                                 ? response.value(QStringLiteral("data")).toObject()
                                 : response;

    const QString keyText = data.value(QStringLiteral("key")).toString();
    const QString ivText = data.value(QStringLiteral("iv")).toString();
    const QString guestToken = data.value(QStringLiteral("glt")).toString();
    if (keyText.isEmpty() || ivText.isEmpty()) {
        return std::unexpected(
            QStringLiteral("The playback license has no key or counter in it."));
    }

    const QByteArray keyBlob = QByteArray::fromBase64(keyText.toLatin1());
    const QByteArray ivBlob = QByteArray::fromBase64(ivText.toLatin1());
    if (keyBlob.isEmpty() || ivBlob.isEmpty()) {
        return std::unexpected(QStringLiteral("The playback license is not valid base64."));
    }

    const QByteArray aad = clipId.toUtf8();
    // The bearer first: that is the token the request carried, so it is the
    // one the server wrapped with. The guest token is the fallback for an
    // anonymous request, and it is the only one present in that case.
    for (const QByteArray& token : {bearer, guestToken.toLatin1()}) {
        if (token.isEmpty()) {
            continue;
        }
        QCryptographicHash sha(QCryptographicHash::Sha256);
        sha.addData(token);
        const QByteArray digest = sha.result();
        const auto key = gcmDecrypt(keyBlob, digest, aad);
        if (!key) {
            continue;
        }
        const auto counter = gcmDecrypt(ivBlob, digest, aad);
        if (!counter) {
            continue;
        }
        if (key->size() != 16 || counter->size() != 16) {
            return std::unexpected(
                QStringLiteral("The playback license is %1/%2 bytes; a content key is 16 and a "
                               "counter is 16.")
                    .arg(key->size())
                    .arg(counter->size()));
        }
        return ContentKey{*key, *counter};
    }

    return std::unexpected(
        QStringLiteral("The playback license did not authenticate for this clip."));
}

} // namespace vc::suno
