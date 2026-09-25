#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <QString>

#include <array>
#include <cstddef>
#include <expected>

namespace vc::suno::auth::oauth {

/// One short-lived, single-use callback transaction.
struct OAuthTransaction {
    QString state;
    QString nonce;
    QString pkceVerifier;
    QString expectedPath;
    quint16 boundPort = 0;
    QDateTime deadlineUtc;

    [[nodiscard]] static std::expected<OAuthTransaction, QString>
    make(const QString& expectedPath, quint16 boundPort, QDateTime deadlineUtc) {
        if (expectedPath.isEmpty() || !expectedPath.startsWith(QLatin1Char('/')) ||
            expectedPath.size() > 2048) {
            return std::unexpected(QStringLiteral("OAuth transaction path is invalid"));
        }
        for (const QChar c : expectedPath) {
            if (c.unicode() <= 0x20 || c.unicode() == 0x7f || c == QLatin1Char('?') ||
                c == QLatin1Char('#') || c == QLatin1Char('\\')) {
                return std::unexpected(QStringLiteral("OAuth transaction path is invalid"));
            }
        }
        if (boundPort == 0) {
            return std::unexpected(QStringLiteral("OAuth transaction port is invalid"));
        }
        deadlineUtc = deadlineUtc.toUTC();
        if (!deadlineUtc.isValid() || deadlineUtc <= QDateTime::currentDateTimeUtc()) {
            return std::unexpected(QStringLiteral("OAuth transaction deadline is invalid"));
        }

        QRandomGenerator* rng = QRandomGenerator::system();
        if (!rng) {
            return std::unexpected(QStringLiteral("Secure randomness is unavailable"));
        }

        std::array<quint32, 8> stateWords{};
        std::array<quint32, 8> nonceWords{};
        std::array<quint32, 16> verifierWords{};
        rng->fillRange(stateWords.data(), static_cast<qsizetype>(stateWords.size()));
        rng->fillRange(nonceWords.data(), static_cast<qsizetype>(nonceWords.size()));
        rng->fillRange(verifierWords.data(), static_cast<qsizetype>(verifierWords.size()));

        const auto encode = [](const auto& words) {
            return QString::fromLatin1(QByteArray::fromRawData(
                reinterpret_cast<const char*>(words.data()),
                static_cast<qsizetype>(words.size() * sizeof(quint32)))
                .toBase64(QByteArray::Base64UrlEncoding |
                          QByteArray::OmitTrailingEquals));
        };

        return OAuthTransaction{
            .state = encode(stateWords),
            .nonce = encode(nonceWords),
            .pkceVerifier = encode(verifierWords),
            .expectedPath = expectedPath,
            .boundPort = boundPort,
            .deadlineUtc = deadlineUtc,
        };
    }

    [[nodiscard]] static QString s256Challenge(const QString& verifier) {
        if (verifier.isEmpty()) return {};
        const QByteArray digest = QCryptographicHash::hash(
            verifier.toUtf8(), QCryptographicHash::Sha256);
        return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding |
                                                  QByteArray::OmitTrailingEquals));
    }
};

} // namespace vc::suno::auth::oauth
