#include <QtTest>
#include <QSignalSpy>

#include "suno/auth/oauth/OAuthLoginService.hpp"
#include "suno/auth/oauth/OAuthTransaction.hpp"

#include <QDateTime>
#include <QSet>

#include <utility>

using namespace vc::suno::auth::oauth;

namespace {

CaptureApprovedLaunch testLaunch() {
    return CaptureApprovedLaunch{
        .authorizationUrl = QUrl(QStringLiteral("https://example.invalid/oauth/start")),
        .loopbackPath = QStringLiteral("/oauth/callback"),
    };
}

OAuthLoginService::TransactionFactory knownTransactionFactory(
    const QString& knownState, const QString& expectedPath = QStringLiteral("/oauth/callback")) {
    return [knownState, expectedPath](const QString& path, quint16 port,
                                       const QDateTime& deadline) -> std::expected<OAuthTransaction, QString> {
        auto transaction = OAuthTransaction::make(path, port, deadline);
        if (!transaction) return std::unexpected(QStringLiteral("test transaction setup failed"));
        if (path != expectedPath) {
            return std::unexpected(QStringLiteral("test path mismatch"));
        }
        transaction->state = knownState;
        transaction->nonce = QStringLiteral("independently-known-nonce");
        return transaction;
    };
}

bool pkceVerifier(const QString& value) {
    if (value.size() < 43 || value.size() > 128) return false;
    for (const QChar c : value) {
        const bool unreserved = (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
            (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
            (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
            c == QLatin1Char('-') || c == QLatin1Char('.') ||
            c == QLatin1Char('_') || c == QLatin1Char('~');
        if (!unreserved) return false;
    }
    return true;
}

} // namespace

class TestOAuthLoginService : public QObject {
    Q_OBJECT

private slots:
    void transactionEntropyAndUniqueness() {
        QSet<QString> states;
        QSet<QString> nonces;
        QSet<QString> verifiers;
        const QDateTime deadline = QDateTime::currentDateTimeUtc().addSecs(60);

        for (int i = 0; i < 128; ++i) {
            auto transaction = OAuthTransaction::make(
                QStringLiteral("/oauth/callback"), static_cast<quint16>(20000 + i), deadline);
            QVERIFY(transaction.has_value());
            QVERIFY(transaction->state.size() >= 43);
            QVERIFY(transaction->nonce.size() >= 32);
            QVERIFY(pkceVerifier(transaction->pkceVerifier));
            QVERIFY(transaction->state != transaction->nonce);
            QVERIFY(!states.contains(transaction->state));
            QVERIFY(!nonces.contains(transaction->nonce));
            QVERIFY(!verifiers.contains(transaction->pkceVerifier));
            states.insert(transaction->state);
            nonces.insert(transaction->nonce);
            verifiers.insert(transaction->pkceVerifier);
        }
    }

    void knownS256ChallengeVector() {
        const QString verifier =
            QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
        QCOMPARE(OAuthTransaction::s256Challenge(verifier),
                 QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        QVERIFY(OAuthTransaction::s256Challenge(QString()).isEmpty());
    }

    void transactionRejectsInvalidInputs() {
        const QDateTime future = QDateTime::currentDateTimeUtc().addSecs(60);
        QVERIFY(!OAuthTransaction::make(QString(), 12345, future).has_value());
        QVERIFY(!OAuthTransaction::make(QStringLiteral("relative"), 12345, future).has_value());
        QVERIFY(!OAuthTransaction::make(QStringLiteral("/callback"), 0, future).has_value());
        QVERIFY(!OAuthTransaction::make(QStringLiteral("/callback"), 12345, QDateTime{}).has_value());
        QVERIFY(!OAuthTransaction::make(
            QStringLiteral("/callback"), 12345,
            QDateTime::currentDateTimeUtc().addSecs(-1)).has_value());
    }

    void stateStringsAreStableQmlContract() {
        QCOMPARE(stateToString(State::SignedOut), QStringLiteral("signedOut"));
        QCOMPARE(stateToString(State::BrowserOpen), QStringLiteral("browserOpen"));
        QCOMPARE(stateToString(State::CallbackReceived), QStringLiteral("callbackReceived"));
        QCOMPARE(stateToString(State::Authenticated), QStringLiteral("authenticated"));
        QCOMPARE(stateToString(State::Error), QStringLiteral("error"));
        QCOMPARE(stateToString(State::Cancelled), QStringLiteral("cancelled"));
        QCOMPARE(OAuthLoginService::kLoginTimeoutMs, 7 * 60 * 1000);
    }

    void emptyLaunchIsCaptureGatedAndDoesNotOpenBrowser() {
        int openCalls = 0;
        OAuthLoginService service([&](const QUrl&) { ++openCalls; return true; });
        QSignalSpy failed(&service, &OAuthLoginService::failed);

        service.begin(CaptureApprovedLaunch{});
        QCOMPARE(openCalls, 0);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(service.state(), State::Error);
        QCOMPARE(failed.first().at(0).toString(),
                 QStringLiteral("Desktop sign-in is not enabled in this build"));
    }

    void beginIsSingleFlightAndConsumesBeforeCallbackSignal() {
        int openCalls = 0;
        const QString knownState = QStringLiteral("known-state-0123456789abcdefghijklmnopqrstuv");
        OAuthLoginService service(
            [&](const QUrl&) { ++openCalls; return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(knownState));
        QSignalSpy opened(&service, &OAuthLoginService::browserOpened);
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        QCOMPARE(service.state(), State::BrowserOpen);
        QCOMPARE(openCalls, 1);
        QCOMPARE(opened.count(), 1);

        service.begin(testLaunch());
        QCOMPARE(openCalls, 1);
        QCOMPARE(service.state(), State::BrowserOpen);
        QCOMPARE(failures.count(), 1);

        // T0166: a real success response carries a code alongside the state.
        // This test used to send state alone and pass, which is exactly the
        // shape of response the board item said was accepted as success.
        const QUrl callback(QStringLiteral("/oauth/callback?code=auth-code-value&state=") + knownState);
        bool replayed = false;
        QObject::connect(&service, &OAuthLoginService::callbackReceived, &service,
                         [&] {
                             if (std::exchange(replayed, true)) return;
                             service.acceptCallback(callback);
                         });
        service.acceptCallback(callback);

        QCOMPARE(callbacks.count(), 1);
        QCOMPARE(failures.count(), 2);
        QCOMPARE(service.state(), State::Error);
    }

    void mismatchedStateIsRejectedWithSafeError() {
        const QString marker = QStringLiteral("cookie=session-secret;token=jwt-secret");
        int openCalls = 0;
        OAuthLoginService service(
            [&](const QUrl&) { ++openCalls; return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("expected-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?state=wrong&payload=") + marker));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(service.state(), State::Error);
        const QString safeError = failures.first().at(0).toString();
        QVERIFY(!safeError.contains(marker));
        QVERIFY(!safeError.contains(QStringLiteral("wrong")));
        QVERIFY(!safeError.contains(QStringLiteral("expected-state")));
    }

    void deadlineExpiryRejectsCallback() {
        QDateTime now = QDateTime::currentDateTimeUtc();
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [&now] { return now; },
            knownTransactionFactory(QStringLiteral("deadline-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        now = now.addSecs((OAuthLoginService::kLoginTimeoutMs / 1000) + 1);
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?state=deadline-state")));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(service.state(), State::Error);
    }

    void cancelBeforeAndAfterCallback() {
        OAuthLoginService before(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("cancel-state")));
        QSignalSpy cancelledBefore(&before, &OAuthLoginService::cancelled);
        before.begin(testLaunch());
        before.cancel();
        before.cancel();
        QCOMPARE(before.state(), State::Cancelled);
        QCOMPARE(cancelledBefore.count(), 1);

        OAuthLoginService after(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("after-cancel-state")));
        QSignalSpy cancelledAfter(&after, &OAuthLoginService::cancelled);
        after.begin(testLaunch());
        after.acceptCallback(
            QUrl(QStringLiteral("/oauth/callback?code=after-cancel-code&state=after-cancel-state")));
        after.cancel();
        QCOMPARE(after.state(), State::CallbackReceived);
        QCOMPARE(cancelledAfter.count(), 0);
    }

    // --- T0166: an IdP error response must never reach the authenticated state.

    /// The board item's exact repro: a well-formed callback on the right path,
    /// inside the deadline, with the CORRECT state -- and no `code`. It reached
    /// State::CallbackReceived, and the coordinator then ran installCredentials_
    /// on an exchange with nothing to exchange.
    void callbackWithStateButNoCodeIsNotSuccess() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("no-code-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?state=no-code-state")));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(service.state(), State::Error);
    }

    /// The headline: `?error=access_denied&state=<valid>` is the response a
    /// user gets from the IdP's own "Cancel" button. Every pre-existing check
    /// passes on it.
    void idpErrorResponseIsRejectedAsSuccess() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("denied-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(
            QUrl(QStringLiteral("/oauth/callback?error=access_denied&state=denied-state")));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(service.state(), State::Error);
        QCOMPARE(failures.first().at(0).toString(), QStringLiteral("Sign-in was declined"));
    }

    /// RFC 6749 4.1.2.1: error_description is attacker-influenced free text.
    /// The callback arrives on a socket any local process can connect to, so it
    /// must never be echoed into the UI or the log.
    void idpErrorDescriptionIsNeverEchoed() {
        const QString marker = QStringLiteral("token=jwt-secret;password=hunter2");
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("echo-state")));
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?error=server_error&error_description=") +
                                   marker + QStringLiteral("&state=echo-state")));

        QCOMPARE(failures.count(), 1);
        const QString safeError = failures.first().at(0).toString();
        QVERIFY(!safeError.contains(marker));
        QVERIFY(!safeError.contains(QStringLiteral("hunter2")));
    }

    /// An unknown error code is an IdP extension. Passing it through would put
    /// a string this file never chose into the message.
    void unrecognisedErrorCodeIsNotEchoedVerbatim() {
        const QString marker = QStringLiteral("weird-provider-code-marker");
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("unknown-code-state")));
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?error=") + marker +
                                   QStringLiteral("&state=unknown-code-state")));

        QCOMPARE(failures.count(), 1);
        const QString safeError = failures.first().at(0).toString();
        QVERIFY(!safeError.contains(marker));
    }

    /// Duplicated parameters are an OAuth MUST NOT (RFC 6749 3.1). Two `code`
    /// values is an ambiguity, not a choice to make.
    void duplicatedCodeIsRejected() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("dupe-code-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?code=first&code=second&state=dupe-code-state")));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
    }

    /// The two arms are mutually exclusive. An injected `error` must win even
    /// when a valid `code` is also present -- otherwise an attacker who can add
    /// a parameter to an otherwise-valid callback rides along.
    void errorWinsOverAValidCode() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("both-arms-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral(
            "/oauth/callback?code=real-code&error=access_denied&state=both-arms-state")));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
    }

    void emptyCodeIsRejected() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("empty-code-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?code=&state=empty-code-state")));

        QCOMPARE(callbacks.count(), 0);
        QCOMPARE(failures.count(), 1);
    }

    /// Guards the fix from the other side: a genuine success still works.
    void wellFormedSuccessStillReachesCallbackReceived() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("good-state")));
        QSignalSpy callbacks(&service, &OAuthLoginService::callbackReceived);
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(
            QUrl(QStringLiteral("/oauth/callback?code=the-real-code&state=good-state")));

        QCOMPARE(callbacks.count(), 1);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(service.state(), State::CallbackReceived);
    }

    /// A rejected callback must not leave a half-consumed transaction, or a
    /// retry would behave differently from a first attempt.
    void rejectedCallbackDoesNotConsumeTheTransaction() {
        OAuthLoginService service(
            [](const QUrl&) { return true; }, [] { return QDateTime::currentDateTimeUtc(); },
            knownTransactionFactory(QStringLiteral("reuse-state")));
        QSignalSpy failures(&service, &OAuthLoginService::failed);

        service.begin(testLaunch());
        service.acceptCallback(QUrl(QStringLiteral("/oauth/callback?error=access_denied&state=reuse-state")));
        QCOMPARE(failures.count(), 1);
        QCOMPARE(service.state(), State::Error);

        // fail() resets the transaction, so a second callback is refused for
        // having no transaction rather than being re-admitted.
        service.acceptCallback(
            QUrl(QStringLiteral("/oauth/callback?code=late-code&state=reuse-state")));
        QCOMPARE(failures.count(), 2);
        QCOMPARE(service.state(), State::Error);
    }
};

int runTestOAuthLoginService(int argc, char** argv) {
    TestOAuthLoginService test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_OAuthLoginService.moc"
