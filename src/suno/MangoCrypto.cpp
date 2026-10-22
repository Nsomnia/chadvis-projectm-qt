/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#include "suno/MangoCrypto.hpp"

#include <algorithm>
#include <cstring>

#if CHADVIS_HAVE_AVAES_INIT
extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}
#endif

namespace vc::suno {
namespace {

constexpr std::size_t kBlockBytes = 16;

using Block = std::array<std::uint8_t, 16>;

#if CHADVIS_HAVE_AVAES_INIT

constexpr std::size_t kNonceBytes = 12;
constexpr std::size_t kTagBytes = 16;

void incrementCounter(Block& counter) {
    // A 128-bit big-endian increment, so a counter seed whose low bytes are
    // all ones carries into the high half exactly as the site's BigInt
    // arithmetic does.
    for (std::size_t i = kBlockBytes; i-- > 0;) {
        if (++counter[i] != 0) {
            break;
        }
    }
}

void incrementLow32(Block& counter) {
    // GCM's inc32: only the rightmost 32 bits move.
    for (std::size_t i = kBlockBytes; i-- > kBlockBytes - 4;) {
        if (++counter[i] != 0) {
            break;
        }
    }
}

void writeBigEndian64(std::uint64_t value, std::uint8_t* out) {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (56 - 8 * i)) & 0xFFu);
    }
}


/// AES in the encryption direction, which is the only direction GCM and CTR
/// need: GCM's keystream is E(K, counter), its tag seed is E(K, J0), and
/// CTR's keystream is E(K, counter). Nothing here ever decrypts an AES
/// block, which is why the direction flag is fixed at "encrypt".
class AesEncryptor {
public:
    explicit AesEncryptor(const QByteArray& key) {
        if (key.size() != 16 && key.size() != 32) {
            return;
        }
        ctx_ = av_aes_alloc();
        if (!ctx_) {
            return;
        }
        if (av_aes_init(ctx_,
                        reinterpret_cast<const std::uint8_t*>(key.constData()),
                        static_cast<int>(key.size()) * 8,
                        0) < 0) {
            av_free(ctx_);
            ctx_ = nullptr;
        }
    }

    ~AesEncryptor() {
        if (ctx_) {
            av_free(ctx_);
        }
    }

    AesEncryptor(const AesEncryptor&) = delete;
    AesEncryptor& operator=(const AesEncryptor&) = delete;

    [[nodiscard]] bool valid() const { return ctx_ != nullptr; }

    /// Encrypts `count` independent blocks (ECB): every 16-byte block of
    /// `src` is encrypted on its own, which is exactly what a counter-mode
    /// keystream is.
    void encryptBlocks(const std::uint8_t* src, std::uint8_t* dst, int count) const {
        av_aes_crypt(ctx_, dst, src, count, nullptr, 0);
    }

private:
    struct AVAES* ctx_{nullptr};
};

/// GF(2^128) multiplication (NIST SP 800-38D, Algorithm 1).
///
/// Bit i of X is the i-th bit counted from the LEFT, and the reduction
/// constant R = 11100001 || 0^120 lands in the first byte.
void ghashMultiply(Block& z, const Block& x, Block v) {
    // The multiplicand is copied because the caller multiplies the
    // accumulator in place: z and x are the same object there, and zeroing z
    // first would zero the multiplicand with it.
    const Block multiplicand = x;
    z.fill(0);
    for (int i = 0; i < 128; ++i) {
        if ((multiplicand[static_cast<std::size_t>(i / 8)] >> (7 - (i % 8))) & 1u) {
            for (std::size_t j = 0; j < kBlockBytes; ++j) {
                z[j] ^= v[j];
            }
        }
        const bool carry = (v[kBlockBytes - 1] & 1u) != 0;
        for (std::size_t j = kBlockBytes; j-- > 1;) {
            v[j] = static_cast<std::uint8_t>((v[j] >> 1) | ((v[j - 1] & 1u) << 7));
        }
        v[0] = static_cast<std::uint8_t>(v[0] >> 1);
        if (carry) {
            v[0] ^= 0xE1u;
        }
    }
}

/// GHASH over the AAD and then the ciphertext, with the spec's length block.
///
/// The two are absorbed SEPARATELY: a partial AAD block is zero-padded to a
/// block boundary before the ciphertext starts. Absorbing them as one
/// concatenated buffer is a different function -- the clip id is 36 bytes, so
/// it is *always* a partial block, and the difference is a wrong tag on every
/// real license.
Block ghash(const Block& h, const QByteArray& aad, const QByteArray& ciphertext) {
    Block y{};
    const auto absorb = [&](const QByteArray& part) {
        const std::size_t fullBlocks = part.size() / kBlockBytes;
        for (std::size_t i = 0; i < fullBlocks; ++i) {
            for (std::size_t j = 0; j < kBlockBytes; ++j) {
                y[j] ^= static_cast<std::uint8_t>(
                    part.at(static_cast<qsizetype>(i * kBlockBytes + j)));
            }
            ghashMultiply(y, y, h);
        }
        if (part.size() % kBlockBytes != 0) {
            // The trailing partial block, zero-padded.
            for (std::size_t j = 0; j < part.size() % kBlockBytes; ++j) {
                y[j] ^= static_cast<std::uint8_t>(
                    part.at(static_cast<qsizetype>(fullBlocks * kBlockBytes + j)));
            }
            ghashMultiply(y, y, h);
        }
    };
    absorb(aad);
    absorb(ciphertext);

    // The length block: the bit lengths of the AAD and of the ciphertext,
    // each 64 bits, big-endian.
    Block lengths{};
    writeBigEndian64(static_cast<std::uint64_t>(aad.size()) * 8, lengths.data());
    writeBigEndian64(static_cast<std::uint64_t>(ciphertext.size()) * 8, lengths.data() + 8);
    for (std::size_t j = 0; j < kBlockBytes; ++j) {
        y[j] ^= lengths[j];
    }
    ghashMultiply(y, y, h);
    return y;
}

#endif // CHADVIS_HAVE_AVAES_INIT

} // namespace

#if CHADVIS_HAVE_AVAES_INIT

std::expected<QByteArray, QString>
gcmDecrypt(const QByteArray& blob, const QByteArray& key32, const QByteArray& aad) {
    if (key32.size() != 16 && key32.size() != 32) {
        return std::unexpected(
            QStringLiteral("The playback key is %1 bytes; AES-GCM needs 16 or 32.")
                .arg(key32.size()));
    }
    if (blob.size() < static_cast<qsizetype>(kNonceBytes + kTagBytes)) {
        return std::unexpected(
            QStringLiteral("The playback license is %1 bytes; it cannot hold a nonce and a tag.")
                .arg(blob.size()));
    }

    const QByteArray nonce = blob.left(static_cast<qsizetype>(kNonceBytes));
    const QByteArray ciphertext =
        blob.mid(static_cast<qsizetype>(kNonceBytes),
                 static_cast<qsizetype>(blob.size() - kNonceBytes - kTagBytes));
    const QByteArray tag = blob.right(static_cast<qsizetype>(kTagBytes));

    AesEncryptor aes(key32);
    if (!aes.valid()) {
        return std::unexpected(QStringLiteral("This build has no working AES implementation."));
    }

    // H = E(K, 0^128)
    Block h{};
    aes.encryptBlocks(h.data(), h.data(), 1);

    // J0 = nonce || 0^31 || 1 for a 96-bit nonce; the keystream starts at
    // inc32(J0).
    Block j0{};
    std::memcpy(j0.data(), nonce.constData(), kNonceBytes);
    j0[kBlockBytes - 1] = 1;
    Block mask{};
    aes.encryptBlocks(j0.data(), mask.data(), 1);

    // The tag is GHASH(A || C) XOR E(K, J0).
    const Block hash = ghash(h, aad, ciphertext);
    std::uint8_t difference = 0;
    for (std::size_t j = 0; j < kTagBytes; ++j) {
        difference |= static_cast<std::uint8_t>(hash[j] ^ mask[j]) ^
                      static_cast<std::uint8_t>(tag.at(static_cast<qsizetype>(j)));
    }
    if (difference != 0) {
        // A wrong token, a wrong clip id, or a corrupted blob. There is no
        // "mostly right" answer here, and the caller must not go looking for
        // one.
        return std::unexpected(
            QStringLiteral("The playback license did not authenticate for this clip."));
    }

    // GCTR(inc32(J0), C)
    Block counter = j0;
    incrementLow32(counter);
    QByteArray plain(ciphertext.size(), Qt::Uninitialized);
    const auto* src = reinterpret_cast<const std::uint8_t*>(ciphertext.constData());
    auto* dst = reinterpret_cast<std::uint8_t*>(plain.data());
    const std::size_t total = static_cast<std::size_t>(ciphertext.size());
    std::size_t offset = 0;
    while (offset < total) {
        Block stream{};
        aes.encryptBlocks(counter.data(), stream.data(), 1);
        const std::size_t take = std::min(kBlockBytes, total - offset);
        for (std::size_t i = 0; i < take; ++i) {
            dst[offset + i] = static_cast<std::uint8_t>(src[offset + i] ^ stream[i]);
        }
        offset += take;
        incrementLow32(counter);
    }
    return plain;
}

QByteArray ctrCrypt(const QByteArray& data, const QByteArray& key16, const QByteArray& counter16) {
    if (key16.size() != 16 || counter16.size() != static_cast<qsizetype>(kBlockBytes)) {
        return {};
    }
    AesEncryptor aes(key16);
    if (!aes.valid()) {
        return {};
    }
    QByteArray out = data;
    Block counter{};
    std::memcpy(counter.data(), counter16.constData(), kBlockBytes);
    const auto* src = reinterpret_cast<const std::uint8_t*>(data.constData());
    auto* dst = reinterpret_cast<std::uint8_t*>(out.data());
    const std::size_t total = static_cast<std::size_t>(data.size());
    std::size_t offset = 0;
    while (offset < total) {
        Block stream{};
        aes.encryptBlocks(counter.data(), stream.data(), 1);
        const std::size_t take = std::min(kBlockBytes, total - offset);
        for (std::size_t i = 0; i < take; ++i) {
            dst[offset + i] = static_cast<std::uint8_t>(src[offset + i] ^ stream[i]);
        }
        offset += take;
        incrementCounter(counter);
    }
    return out;
}

CtrStream::CtrStream(const QByteArray& key16, const QByteArray& counter16) {
    if (key16.size() != 16 || counter16.size() != static_cast<qsizetype>(kBlockBytes)) {
        return;
    }
    key_ = key16;
    std::memcpy(counter_.data(), counter16.constData(), kBlockBytes);
    valid_ = true;
}

QByteArray CtrStream::process(const QByteArray& data) {
    if (!valid_) {
        return {};
    }
    const QByteArray input = carry_ + data;
    const std::size_t whole = (input.size() / kBlockBytes) * kBlockBytes;
    QByteArray counterBytes(static_cast<qsizetype>(kBlockBytes), Qt::Uninitialized);
    std::memcpy(counterBytes.data(), counter_.data(), kBlockBytes);
    QByteArray out = ctrCrypt(input.left(static_cast<qsizetype>(whole)), key_, counterBytes);
    for (std::size_t i = 0; i < whole / kBlockBytes; ++i) {
        incrementCounter(counter_);
    }
    carry_ = input.right(static_cast<qsizetype>(input.size() - whole));
    return out;
}

QByteArray CtrStream::finish() {
    if (!valid_ || carry_.isEmpty()) {
        carry_.clear();
        return {};
    }
    QByteArray counterBytes(static_cast<qsizetype>(kBlockBytes), Qt::Uninitialized);
    std::memcpy(counterBytes.data(), counter_.data(), kBlockBytes);
    QByteArray out = ctrCrypt(carry_, key_, counterBytes);
    carry_.clear();
    return out;
}

#else // !CHADVIS_HAVE_AVAES_INIT

std::expected<QByteArray, QString>
gcmDecrypt(const QByteArray&, const QByteArray&, const QByteArray&) {
    return std::unexpected(
        QStringLiteral("This build has no AES support, so encrypted playback is unavailable."));
}

QByteArray ctrCrypt(const QByteArray&, const QByteArray&, const QByteArray&) {
    return {};
}

CtrStream::CtrStream(const QByteArray&, const QByteArray&) {}
QByteArray CtrStream::process(const QByteArray&) { return {}; }
QByteArray CtrStream::finish() { return {}; }

#endif // CHADVIS_HAVE_AVAES_INIT

} // namespace vc::suno
