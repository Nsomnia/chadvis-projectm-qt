#include <QtTest>
#include <QSignalSpy>

#include "suno/auth/AuthCoordinator.hpp"
#include "suno/auth/CredentialStore.hpp"
#include "suno/auth/oauth/OAuthLoginService.hpp"

#ifdef CHADVIS_STANDALONE_AUTH_TEST
#include "suno/SunoClient.hpp"
namespace vc::suno {
// The standalone binary tests coordinator dependency seams without constructing
// the full network client; the project test links the real implementation.
void SunoClient::clearLocalCredentials() {}
} // namespace vc::suno
#endif

#include <QDateTime>

#include <memory>
#include <utility>

using vc::suno::auth::AuthCoordinator;
using vc::suno::auth::CredentialStore;
using namespace vc::suno::auth::oauth;

namespace {

std::unique_ptr<OAuthLoginService> makeKnownService(std::function<bool(const QUrl&)> openBrowser,
                                                    const QString& knownState) {
    return std::make_unique<OAuthLoginService>(
        std::move(openBrowser), [] { return QDateTime::currentDateTimeUtc(); },
        [knownState](const QString& path, quint16 port, const QDateTime& deadline) {
            auto transaction = OAuthTransaction::make(path, port, deadline);
            if (transaction) transaction->state = knownState;
            return transaction;
        });
}

CaptureApprovedLaunch coordinatorLaunch() {
    return CaptureApprovedLaunch{
        .authorizationUrl = QUrl(QStringLiteral("https://example.invalid/oauth/start")),
        .loopbackPath = QStringLiteral("/oauth/callback"),
    };
}

} // namespace

class TestAuthCoordinator : public QObject {
    Q_OBJECT

private slots:
    void insecureBackendRefusesFromSignedOut() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.loginService = makeKnownService([](const QUrl&) { return true; },
                                                     QStringLiteral("unused-state"));
        dependencies.secureBackendAvailable = [] { return false; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));
        QSignalSpy states(&coordinator, &AuthCoordinator::stateChanged);
        QSignalSpy errors(&coordinator, &AuthCoordinator::errorChanged);

        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("signedOut"));
        QVERIFY(!coordinator.googleLoginAvailable());
        QString error;
        QVERIFY(!coordinator.beginGoogleSignIn(&error));
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("error"));
        QCOMPARE(states.count(), 1);
        QCOMPARE(states.first().at(0).toString(), QStringLiteral("error"));
        QCOMPARE(errors.count(), 1);
        QVERIFY(error.contains(QStringLiteral("Secure credential storage")));
        QVERIFY(!error.contains(QStringLiteral("__client")));
        QVERIFY(!error.contains(QStringLiteral("token")));
        QCOMPARE(coordinator.googleLoginError(), error);
    }

    void secureBackendWithoutCaptureLaunchRefuses() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.loginService = makeKnownService([](const QUrl&) { return true; },
                                                     QStringLiteral("unused-state"));
        dependencies.secureBackendAvailable = [] { return true; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));

        QVERIFY(coordinator.isSecureBackendAvailable());
        QVERIFY(!coordinator.googleLoginAvailable());
        QString error;
        QVERIFY(!coordinator.beginGoogleSignIn(&error));
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("error"));
        QCOMPARE(error,
                 QStringLiteral("Desktop sign-in is not enabled in this build"));
    }

    void installsOneValidatedCallbackAndSignsOutLocally() {
        int openCalls = 0;
        int installCalls = 0;
        int clearCalls = 0;
        auto service = makeKnownService(
            [&](const QUrl&) { ++openCalls; return true; },
            QStringLiteral("coordinator-known-state-0123456789abcdefghijklm"));
        auto* rawService = service.get();

        AuthCoordinator::Dependencies dependencies;
        dependencies.loginService = std::move(service);
        dependencies.secureBackendAvailable = [] { return true; };
        dependencies.installCredentials = [&] { ++installCalls; return true; };
        dependencies.clearLocalCredentials = [&] { ++clearCalls; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));
        coordinator.setCaptureApprovedLaunch(coordinatorLaunch());
        QSignalSpy callbacks(&coordinator, &AuthCoordinator::callbackReceived);

        QVERIFY(coordinator.googleLoginAvailable());
        QString error;
        QVERIFY(coordinator.beginGoogleSignIn(&error));
        QCOMPARE(openCalls, 1);
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("browserOpen"));

        rawService->acceptCallback(
            QUrl(QStringLiteral("/oauth/callback?state=coordinator-known-state-0123456789abcdefghijklm")));
        QCOMPARE(callbacks.count(), 1);
        QCOMPARE(installCalls, 1);
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("authenticated"));

        // Replay cannot invoke the credential installer a second time.
        rawService->acceptCallback(
            QUrl(QStringLiteral("/oauth/callback?state=coordinator-known-state-0123456789abcdefghijklm")));
        QCOMPARE(callbacks.count(), 1);
        QCOMPARE(installCalls, 1);
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("error"));

        coordinator.signOutSuno();
        QCOMPARE(clearCalls, 1);
    }

    void keychainReadStatusMappingIsDistinguishable() {
        using Status = CredentialStore::KeychainReadStatus;
        QCOMPARE(CredentialStore::classifyKeychainReadStatus(0), Status::Success);
        QCOMPARE(CredentialStore::classifyKeychainReadStatus(-25300), Status::NotFound);
        QCOMPARE(CredentialStore::classifyKeychainReadStatus(-25308), Status::AccessDenied);
        QCOMPARE(CredentialStore::classifyKeychainReadStatus(-25293), Status::AccessDenied);
        QCOMPARE(CredentialStore::classifyKeychainReadStatus(-1), Status::Failed);
    }
};

int runTestAuthCoordinator(int argc, char** argv) {
    TestAuthCoordinator test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_AuthCoordinator.moc"
