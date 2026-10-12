#include <QtTest>
#include <QSignalSpy>

#include "suno/auth/AuthCoordinator.hpp"
#include "suno/auth/CredentialStore.hpp"

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

namespace {

} // namespace

class TestAuthCoordinator : public QObject {
    Q_OBJECT

private slots:
    void insecureBackendRefusesFromSignedOut() {
        AuthCoordinator::Dependencies dependencies;
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

    /// T0181: a secure backend is the WHOLE precondition now. The old gate was
    /// a capture-approved launch whose loopback path could never be populated
    /// (Clerk completes the OAuth exchange server-side), so sign-in used to
    /// fail every time and this test asserted exactly that. Green, and the
    /// feature it covered was unreachable.
    void secureBackendMakesGoogleSignInAvailable() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [] { return true; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));

        QVERIFY(coordinator.isSecureBackendAvailable());
        QVERIFY(coordinator.googleLoginAvailable());
    }

    /// T0181: sign-out is local only, and it is the one half of this suite that
    /// was always correct. The credential *install* half lost its only caller
    /// when the loopback service went away -- the credential now arrives from
    /// the UI's paste field through SunoClient directly -- so what is left to
    /// pin is that a sign-out clears the local credential exactly once.
    void signOutClearsTheLocalCredentialOnce() {
        int clearCalls = 0;
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [] { return true; };
        dependencies.clearLocalCredentials = [&] { ++clearCalls; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));

        QVERIFY(coordinator.googleLoginAvailable());
        coordinator.signOutSuno();
        coordinator.signOutSuno();
        QCOMPARE(clearCalls, 2);
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
