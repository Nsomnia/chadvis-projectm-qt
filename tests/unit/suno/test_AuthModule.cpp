#include <QtTest>
#include "core/Logger.hpp"
#include "suno/auth/AuthHeaders.hpp"
#include "suno/auth/AuthTypes.hpp"
#include "suno/auth/CredentialStore.hpp"
#include "suno/auth/JwtUtils.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimeZone>

using namespace vc::suno::auth;

namespace {

/// base64url (unpadded) of a compact JSON object - mirrors what Clerk emits.
QString b64url(const QJsonObject& obj) {
    const QByteArray encoded = QJsonDocument(obj).toJson(QJsonDocument::Compact)
                                       .toBase64(QByteArray::Base64UrlEncoding |
                                                 QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(encoded);
}

/// Build a syntactically valid, unsigned JWT for decoder tests.
QString makeJwt(const QJsonObject& payload) {
    return b64url({{"alg", "HS256"}, {"typ", "JWT"}}) + "." + b64url(payload) + ".sig";
}

constexpr qint64 kFutureExp = 4102444800; // 2100-01-01, safely unexpired

} // namespace

class TestAuthModule : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // The file backend logs a plaintext-fallback warning on first use;
        // make sure a logger exists so that call is well-defined.
        vc::Logger::init("test_auth_module", false);
        tempRoot_ = std::make_unique<QTemporaryDir>();
        QVERIFY(tempRoot_->isValid());
    }

    // ------------------------------------------------------------------
    // JwtUtils
    // ------------------------------------------------------------------

    void jwtValidRoundtrip() {
        const QJsonObject payload{
                {"exp", static_cast<qint64>(kFutureExp)},
                {"sid", "sess_abc"},
                {"suno.com/claims/user_id", "usr_42"},
        };
        auto claims = JwtUtils::claims(makeJwt(payload));
        QVERIFY(claims.has_value());

        QCOMPARE(JwtUtils::expiryEpochSecs(*claims), kFutureExp);
        QCOMPARE(claims->value("sid").toString(), QStringLiteral("sess_abc"));
        // Exact slash-claim key...
        QCOMPARE(JwtUtils::claimString(*claims, "suno.com/claims/user_id"),
                 QStringLiteral("usr_42"));
        // ...and tolerant lookup without the vendor prefix.
        QCOMPARE(JwtUtils::claimString(*claims, "user_id"), QStringLiteral("usr_42"));

        QVERIFY(!JwtUtils::isExpired(*claims));
        QVERIFY(!JwtUtils::isExpired(*claims, 300));

        BearerToken token;
        token.jwt = makeJwt(payload);
        token.expiresAt = QDateTime::fromSecsSinceEpoch(kFutureExp, QTimeZone::UTC);
        QVERIFY(!token.isExpiringSoon());
    }

    void jwtMalformedInputs() {
        // Not enough / empty segments.
        QVERIFY(!JwtUtils::claims(QStringLiteral("")).has_value());
        QVERIFY(!JwtUtils::claims(QStringLiteral("onlyone")).has_value());
        QVERIFY(!JwtUtils::claims(QStringLiteral("two.segments")).has_value());
        QVERIFY(!JwtUtils::claims(QStringLiteral(".payload.sig")).has_value());
        // Invalid base64url in the payload segment.
        QVERIFY(!JwtUtils::claims(QStringLiteral("hdr.!!!!not-base64!!!!.sig")).has_value());
        // Valid base64 but not a JSON object (array).
        QVERIFY(!JwtUtils::claims(QStringLiteral("hdr.WzEsMl0.sig")).has_value()); // "[1,2]"
        // Valid JSON object but garbage after it.
        QVERIFY(!JwtUtils::claims(QStringLiteral("hdr.e30gZ2FyYmFnZQ.sig")).has_value());

        // None of the above may crash; exp helpers stay defensive too.
        QCOMPARE(JwtUtils::expiryEpochSecs(QJsonObject{}), 0);
        QVERIFY(!JwtUtils::isExpired(QJsonObject{}));
    }

    void jwtUnpaddedBase64Url() {
        // Roundtrip through unpadded base64url (the common JWT wire format).
        auto claims = JwtUtils::claims(
                makeJwt({{"exp", static_cast<qint64>(kFutureExp)}, {"k", "v"}}));
        QVERIFY(claims.has_value());
        QCOMPARE(claims->value("k").toString(), QStringLiteral("v"));
    }

    void jwtExpirySemantics() {
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        auto expiredClaims = JwtUtils::claims(makeJwt({{"exp", now - 60}}));
        QVERIFY(expiredClaims.has_value());
        QVERIFY(JwtUtils::isExpired(*expiredClaims));

        // Within the grace window counts as expired (proactive refresh).
        auto almostExpired = JwtUtils::claims(makeJwt({{"exp", now + 60}}));
        QVERIFY(almostExpired.has_value());
        QVERIFY(JwtUtils::isExpired(*almostExpired));
        QVERIFY(!JwtUtils::isExpired(*almostExpired, 0));

        // Missing exp never reports expired.
        QVERIFY(!JwtUtils::isExpired(QJsonObject{{"foo", 1}}));
    }

    // ------------------------------------------------------------------
    // AuthHeaders
    // ------------------------------------------------------------------

    void authHeadersApplyAllFields() {
        const StudioApiHeaders headers =
                makeStudioApiHeaders(QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"));

        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api.suno.ai/api/feed/")));
        headers.apply(request);

        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer jwt-value"));
        QCOMPARE(request.rawHeader("Origin"), QByteArray("https://suno.com"));
        QCOMPARE(request.rawHeader("Referer"), QByteArray("https://suno.com/"));
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray(kBrowserUserAgent));
        QCOMPARE(request.rawHeader("Accept"),
                 QByteArray("application/json,text/plain,*/*"));
        QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
        QCOMPARE(request.rawHeader("Device-Id"), QByteArray("uuid-1234"));
    }

    void authHeadersBrowserTokenShape() {
        const StudioApiHeaders headers =
                makeStudioApiHeaders(QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"));

        // Outer shape: {"token":"<base64>"}.
        const QJsonDocument outer = QJsonDocument::fromJson(headers.browserToken);
        QVERIFY(outer.isObject());
        const QString b64 = outer.object().value("token").toString();
        QVERIFY(!b64.isEmpty());

        // Inner shape: {"timestamp":<epoch-ms>} - LEAD-CAPTURE-NOTE recipe.
        const QByteArray innerRaw = QByteArray::fromBase64(b64.toLatin1());
        const QJsonDocument inner = QJsonDocument::fromJson(innerRaw);
        QVERIFY(inner.isObject());
        const qint64 timestamp = inner.object().value("timestamp").toInteger(0);
        QVERIFY(timestamp > 0);
        QVERIFY(qAbs(timestamp - QDateTime::currentMSecsSinceEpoch()) < 60'000);
    }

    // ------------------------------------------------------------------
    // CredentialStore (forced FILE backend)
    // ------------------------------------------------------------------

    void credentialStoreRoundtrip() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("suno/default", QStringLiteral("cookie-a=1; cookie-b=2"))
                        .isOk());

        auto loaded = store.load("suno/default");
        QVERIFY(loaded.isOk());
        QCOMPARE(loaded.value(), QStringLiteral("cookie-a=1; cookie-b=2"));
    }

    void credentialStoreOverwrite() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("suno/default", QStringLiteral("first")).isOk());
        QVERIFY(store.store("suno/default", QStringLiteral("second")).isOk());

        auto loaded = store.load("suno/default");
        QVERIFY(loaded.isOk());
        QCOMPARE(loaded.value(), QStringLiteral("second"));
    }

    void credentialStoreRemoveThenLoadFails() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("suno/session", QStringLiteral("secret")).isOk());
        QVERIFY(store.remove("suno/session").isOk());

        auto loaded = store.load("suno/session");
        QVERIFY(loaded.isErr());
    }

    void credentialStoreFilePermissions() {
        CredentialStore store(CredentialStore::Backend::File, tempRoot_->path());
        QVERIFY(store.store("perm/check", QStringLiteral("x")).isOk());
#if defined(Q_OS_UNIX)
        const QString path = tempRoot_->path() + QStringLiteral(
                                                   "/chadvis-projectm-qt_chadvis_perm_check");
        QFile file(path);
        QVERIFY(file.exists());
        const QFile::Permissions perms = file.permissions();
        QVERIFY(perms & QFileDevice::ReadOwner);
        QVERIFY(perms & QFileDevice::WriteOwner);
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup)));
        QVERIFY(!(perms & (QFileDevice::ReadOther | QFileDevice::WriteOther)));
#endif
    }

    void redactNeverLeaksSecrets() {
        QCOMPARE(CredentialStore::redact(QStringLiteral("hello")),
                 QStringLiteral("****(len 5)"));
        QCOMPARE(CredentialStore::redact(QString()), QStringLiteral("****(len 0)"));
        const QString secret = QStringLiteral("__client=super-secret-value");
        QVERIFY(!CredentialStore::redact(secret).contains(QStringLiteral("secret")));
    }

private:
    std::unique_ptr<QTemporaryDir> tempRoot_;
};

int runTestAuthModule(int argc, char** argv) {
    TestAuthModule tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_AuthModule.moc"
