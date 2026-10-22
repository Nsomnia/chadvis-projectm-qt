/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <array>
#include <cstdint>
#include <expected>

namespace vc::suno {

/// The AES-GCM decryption of one license blob.
///
/// The blob is `nonce(12) || ciphertext || tag(16)`, the key is the SHA-256 of
/// the session token, and the AAD is the clip id. The tag is **verified**,
/// and that is the whole point of the function: the server wraps the content
/// key under a token-derived key, so a wrong token, a wrong clip id or a
/// corrupted blob must be a failure and never a wrong-but-decryptable answer.
/// Every caller turns that failure into a reason the user can read, never
/// into a file.
///
/// The primitive is AES-GCM as WebCrypto implements it, because the server's
/// own client is a browser: same key size, same nonce length, same tag.
[[nodiscard]] std::expected<QByteArray, QString>
gcmDecrypt(const QByteArray& blob, const QByteArray& key32, const QByteArray& aad);

/// AES-CTR over a buffer. `counter` is the initial 16-byte counter block and
/// advances by one per 16-byte block as a 128-bit big-endian integer -- the
/// same counter the site's own client builds by adding a block index to the
/// license's counter seed.
///
/// The operation is its own inverse, so this both encrypts and decrypts.
[[nodiscard]] QByteArray
ctrCrypt(const QByteArray& data, const QByteArray& key16, const QByteArray& counter16);

/// AES-CTR across chunk boundaries, for a body that is streamed rather than
/// held in memory. The counter lives in the object, so `process()` may be
/// fed any chunk size -- including a chunk that ends in the middle of a
/// 16-byte block.
class CtrStream {
public:
    CtrStream() = default;
    CtrStream(const QByteArray& key16, const QByteArray& counter16);

    [[nodiscard]] bool valid() const { return valid_; }

    /// Decrypts the next chunk. Returns the clear bytes for everything that
    /// could be completed; a trailing partial block is held back until more
    /// arrives or `finish()` is called.
    [[nodiscard]] QByteArray process(const QByteArray& data);

    /// Flushes the held-back partial block with the last keystream bytes.
    [[nodiscard]] QByteArray finish();

private:
    /// One 16-byte counter block, as the site's client counts it.
    using Block = std::array<std::uint8_t, 16>;

    bool valid_{false};
    QByteArray key_;
    Block counter_{};
    /// Clear bytes that belong to the next block.
    QByteArray carry_;
};

} // namespace vc::suno
