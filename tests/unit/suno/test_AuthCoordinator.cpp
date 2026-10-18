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

    /// Sign-out resets the coordinator's own state, not just the stored
    /// credential: after a failed import (state "error") a sign-out must
    /// return the card to "signedOut" with no stale verdict, and after a
    /// plain sign-out the state must already be clean. The old version
    /// only cleared the credential, so an error survived the sign-out.
    void signOutResetsCoordinatorStateAndError() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [] { return false; };
        dependencies.clearLocalCredentials = [] {};
        AuthCoordinator coordinator(nullptr, std::move(dependencies));

        QString error;
        QVERIFY(!coordinator.beginGoogleSignIn(&error));
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("error"));
        QVERIFY(!coordinator.googleLoginError().isEmpty());

        coordinator.signOutSuno();
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("signedOut"));
        QVERIFY(coordinator.googleLoginError().isEmpty());
    }

    /// noteVerificationFailed() only converts the import wait ("browserImported")
    /// into an error. From any other state it is a no-op, so a NeedsReauth
    /// that arrives while the user is merely browsing, or after an
    /// unrelated failure, cannot rewrite the state that failure set.
    void verificationFailureIsIgnoredUnlessAnImportIsWaiting() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [] { return true; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));

        coordinator.noteVerificationFailed();
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("signedOut"));

        AuthCoordinator::Dependencies failedDependencies;
        failedDependencies.secureBackendAvailable = [] { return false; };
        AuthCoordinator failed(nullptr, std::move(failedDependencies));
        QString error;
        QVERIFY(!failed.beginGoogleSignIn(&error));
        QCOMPARE(failed.googleLoginState(), QStringLiteral("error"));
        failed.noteVerificationFailed();
        QCOMPARE(failed.googleLoginState(), QStringLiteral("error"));
        QCOMPARE(failed.googleLoginError(), error);
    }

    /// T0181, plus the waiting-state Cancel the first report of the import
    /// failure demanded: open the browser (through the seam, never a real
    /// QDesktopServices call), sit in "browserOpen" for as long as the user
    /// wants, and get back to "signedOut" with no error when they change
    /// their mind. Restarting the app used to be the only way out.
    void browserOpenStateCancelsBackToSignedOut() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [] { return true; };
        int launches = 0;
        QUrl launched;
        dependencies.launchBrowser = [&](const QUrl& url) {
            ++launches;
            launched = url;
            return true;
        };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));
        QSignalSpy states(&coordinator, &AuthCoordinator::stateChanged);

        QVERIFY(coordinator.beginGoogleSignIn());
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("browserOpen"));
        QCOMPARE(launches, 1);
        // The seam received the sign-in page and only that: https, Suno's
        // host, nothing that could carry a credential.
        QCOMPARE(launched.toString(),
                 QStringLiteral("https://suno.com/create?redirected_from=signin"));

        coordinator.cancelGoogleSignIn();
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("signedOut"));
        QVERIFY(coordinator.googleLoginError().isEmpty());
        QCOMPARE(states.count(), 2);
        QCOMPARE(states.at(1).at(0).toString(), QStringLiteral("signedOut"));

        // A cancel is not a dead end: cancelling twice stays consistent.
        coordinator.cancelGoogleSignIn();
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("signedOut"));
    }

    /// beginGoogleSignIn clears a previous error, so a retry after a failed
    /// Import does not start from yesterday's verdict.
    void aFailedAttemptIsClearedOnRetry() {
        // One flag drives both attempts so the SAME coordinator crosses the
        // threshold mid-test: the claim under test is the error reset, not
        // the secure backend.
        bool secure = false;
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [&secure] { return secure; };
        dependencies.launchBrowser = [](const QUrl&) { return true; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));

        QString error;
        QVERIFY(!coordinator.beginGoogleSignIn(&error));
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("error"));
        QVERIFY(!error.isEmpty());

        secure = true;
        QVERIFY(coordinator.beginGoogleSignIn());
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("browserOpen"));
        QVERIFY(coordinator.googleLoginError().isEmpty());
    }

    /// T0199: the import refuses an insecure backend BEFORE any keychain or
    /// store read. This is the paste path's rule applied to the harvest, and
    /// the only place it can be asserted without touching either.
    void importRefusesAnInsecureBackend() {
        AuthCoordinator::Dependencies dependencies;
        dependencies.secureBackendAvailable = [] { return false; };
        AuthCoordinator coordinator(nullptr, std::move(dependencies));
        QString error;
        QVERIFY(!coordinator.importSessionFromBrowser(&error));
        QCOMPARE(coordinator.googleLoginState(), QStringLiteral("error"));
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains(QStringLiteral("Bearer")));
        QVERIFY(!error.contains(QStringLiteral("__client=")));
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
