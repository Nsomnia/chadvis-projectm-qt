#include <QtTest>
#include "core/Logger.hpp"
#include "suno/auth/AuthHeaders.hpp"
#include "suno/auth/AuthTypes.hpp"
#include "suno/auth/CredentialStore.hpp"
#include "suno/auth/JwtUtils.hpp"
#include "qml_bridge/QmlSingletonBridge.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrl>

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

class DummySingleton : public QObject,
                       public qml_bridge::QmlSingletonBridge<
                               DummySingleton,
                               qml_bridge::SingletonPolicy::CachedQmlParented> {
    friend class qml_bridge::QmlSingletonBridge<
            DummySingleton, qml_bridge::SingletonPolicy::CachedQmlParented>;

    explicit DummySingleton(QObject* parent) : QObject(parent) {
        setInstance(this);
    }
};

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

    void cachedSingletonClearsOnDestruction() {
        auto* singleton = DummySingleton::create(nullptr, nullptr);
        QVERIFY(singleton);
        QCOMPARE(DummySingleton::instance(), singleton);
        delete singleton;
        QVERIFY(!DummySingleton::instance());
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

    void authHeadersApplyPostFields() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "POST",
                QByteArrayLiteral("{}"));

        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/feed/")));
        headers.apply(request);

        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer jwt-value"));
        QCOMPARE(request.rawHeader("Origin"), QByteArray("https://suno.com"));
        QCOMPARE(request.rawHeader("Referer"), QByteArray("https://suno.com/"));
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray(kBrowserUserAgent));
        QCOMPARE(request.rawHeader("Accept"), QByteArray("*/*"));
        QCOMPARE(request.rawHeader("Content-Type"), QByteArray("application/json"));
        QCOMPARE(request.rawHeader("Device-Id"), QByteArray("uuid-1234"));
        QVERIFY(request.rawHeader("Browser-Token").isEmpty());
        QVERIFY(request.attribute(QNetworkRequest::RedirectPolicyAttribute)
                        .value<QNetworkRequest::RedirectPolicy>() ==
                QNetworkRequest::ManualRedirectPolicy);
    }

    void authHeadersGetOmitsJsonContentType() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "GET", {});
        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/session/")));
        request.setRawHeader("Content-Type", QByteArray("application/json"));
        headers.apply(request);

        QVERIFY(request.rawHeader("Content-Type").isEmpty());
        QCOMPARE(request.rawHeader("Accept"), QByteArray("*/*"));
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer jwt-value"));
    }

    void authHeadersEmptyPostOmitsContentType() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "POST", {});
        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/action")));
        request.setRawHeader("Content-Type", QByteArray("application/json"));
        headers.apply(request);

        QVERIFY(request.rawHeader("Content-Type").isEmpty());
    }

    void authHeadersWithoutBearerFailClosed() {
        const StudioApiHeaders headers =
                makeStudioApiHeaders({}, QStringLiteral("uuid-1234"), "GET", {});
        QNetworkRequest request(QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/session/")));
        request.setRawHeader("Authorization", QByteArray("Bearer stale"));
        headers.apply(request);

        QVERIFY(request.rawHeader("Authorization").isEmpty());
    }

    void authHeadersDoNotSynthesizeBrowserToken() {
        const StudioApiHeaders headers = makeStudioApiHeaders(
                QStringLiteral("jwt-value"), QStringLiteral("uuid-1234"), "POST",
                QByteArrayLiteral("{}"));
        QNetworkRequest request(
                QUrl(QStringLiteral("https://studio-api-prod.suno.com/api/feed/v3")));
        headers.apply(request);
        QVERIFY(request.rawHeader("Browser-Token").isEmpty());
    }

    void studioApiHostPolicy_data() {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");

        QTest::newRow("captured-host")
                << QStringLiteral("https://studio-api-prod.suno.com/api/feed/v3") << true;
        QTest::newRow("explicit-default-port")
                << QStringLiteral("https://studio-api-prod.suno.com:443/api/session/") << true;
        QTest::newRow("modal") << QStringLiteral(
                "https://suno-ai--orpheus-prod-web.modal.run/v1/orchestrator/chat") << false;
        QTest::newRow("auth-host") << QStringLiteral("https://auth.suno.com/v1/client") << false;
        QTest::newRow("suffix-confusion")
                << QStringLiteral("https://studio-api-prod.suno.com.evil.test/api") << false;
        QTest::newRow("cleartext")
                << QStringLiteral("http://studio-api-prod.suno.com/api") << false;
        QTest::newRow("user-info")
                << QStringLiteral("https://user@studio-api-prod.suno.com/api") << false;
        QTest::newRow("non-default-port")
                << QStringLiteral("https://studio-api-prod.suno.com:444/api") << false;
        QTest::newRow("relative") << QStringLiteral("/api/session/") << false;
    }

    void studioApiHostPolicy() {
        QFETCH(QString, url);
        QFETCH(bool, allowed);
        QCOMPARE(isAllowedStudioApiUrl(QUrl(url)), allowed);
    }

    void cookieNormalizationPreservesOpaqueRemainder_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("unlabelled")
                << QStringLiteral(" __client=abc==; __session=Keep-Case ")
                << QStringLiteral("__client=abc==; __session=Keep-Case");
        QTest::newRow("mixed-case-label")
                << QStringLiteral("cOoKiE:__client=abc==; opaque=A%2FB")
                << QStringLiteral("__client=abc==; opaque=A%2FB");
        QTest::newRow("embedded-label")
                << QStringLiteral("Cookie: __client=Cookie:opaque")
                << QStringLiteral("__client=Cookie:opaque");
        QTest::newRow("label-only") << QStringLiteral(" Cookie: ") << QString();
    }

    void cookieNormalizationPreservesOpaqueRemainder() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(normalizeCookieHeader(input), expected);
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
