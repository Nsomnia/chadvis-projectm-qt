/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <expected>

namespace vc::suno {

/// The key material for one clip's encrypted media.
///
/// The server hands out a content key and a counter; the body is one
/// AES-128-CTR stream under them, and the counter advances per 16-byte block
/// from the first.
struct ContentKey {
    /// The 16-byte AES-128 content key.
    QByteArray key;
    /// The 16-byte initial CTR counter block.
    QByteArray counter;
};

/// Unwraps a content-rights response into the key material.
///
/// The server wraps the key under `SHA-256` of the token the request carried:
/// the bearer when the request was authenticated, and the guest token in the
/// response when it was not. Both are tried, in that order, because the
/// response does not say which one it used -- the GCM tag decides, and a
/// mismatch is a refusal rather than a wrong answer.
///
/// The AAD is the clip id, so a license cannot be moved to another clip: the
/// same blob unwrapped for a different id fails to authenticate.
[[nodiscard]] std::expected<ContentKey, QString>
unwrapContentKey(const QJsonObject& response, const QByteArray& bearer, const QString& clipId);

} // namespace vc::suno
