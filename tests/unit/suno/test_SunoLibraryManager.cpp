#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QSignalSpy>

#include <atomic>

using vc::suno::CredentialStoreWorker;
using vc::suno::SunoClient;
using vc::suno::SunoDatabase;
using vc::suno::SunoExploreService;
using vc::suno::SunoLibraryManager;

namespace
{

QString base64Url(const QJsonObject& object)
{
    return QString::fromLatin1(
        QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString fakeBearer()
{
    const QString header = base64Url({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                       {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = base64Url(
        {{QStringLiteral("exp"), static_cast<qint64>(4102444800)},
         {QStringLiteral("test"), QStringLiteral("credential-clear")}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

}

class TestSunoLibraryManager : public QObject
{
    Q_OBJECT

private slots:
    void refreshWaitsForInFlightCredentialRestore()
    {
        QSemaphore backendEntered(0);
        QSemaphore releaseBackend(0);
        std::atomic_int backendCalls{0};

        CredentialStoreWorker::Backend backend =
            [&](CredentialStoreWorker::Request) {
                const int call = backendCalls.fetch_add(1) + 1;
                backendEntered.release();
                if (call == 1)
                {
                    releaseBackend.acquire();
                }
                return CredentialStoreWorker::Outcome{};
            };
        SunoClient client(QStringLiteral("test-device"), nullptr, std::move(backend));
        QVERIFY(backendEntered.tryAcquire(1, 2000));

        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);
        QSignalSpy authenticationRequired(
            &manager, &SunoLibraryManager::authenticationRequired);
        QSignalSpy fetchFailed(
            &manager, &SunoLibraryManager::libraryFetchFailed);

        manager.refreshLibrary(1);

        QCOMPARE(authenticationRequired.count(), 0);
        QCOMPARE(fetchFailed.count(), 0);
        QCOMPARE(backendCalls.load(), 1);

        releaseBackend.release();

        QTRY_COMPARE_WITH_TIMEOUT(authenticationRequired.count(), 1, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(fetchFailed.count(), 1, 2000);
        QCOMPARE(backendCalls.load(), 1);

        authenticationRequired.clear();
        fetchFailed.clear();
        manager.refreshLibrary(1);

        QCOMPARE(authenticationRequired.count(), 0);
        QCOMPARE(fetchFailed.count(), 0);

        QTRY_COMPARE_WITH_TIMEOUT(authenticationRequired.count(), 1, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(fetchFailed.count(), 1, 2000);
        QCOMPARE(backendCalls.load(), 2);

        SunoExploreService explore(&client);
        QSignalSpy exploreFailed(&explore, &SunoExploreService::failed);
        explore.refresh();

        QCOMPARE(exploreFailed.count(), 0);
        QVERIFY(explore.isLoading());

        QTRY_COMPARE_WITH_TIMEOUT(exploreFailed.count(), 1, 2000);
        QVERIFY(!explore.isLoading());
        QCOMPARE(backendCalls.load(), 3);

        const QString bearer = fakeBearer();
        client.setToken(bearer.toStdString());
        QVERIFY(client.isAuthenticated());
        QCOMPARE(client.configuredCredential(), bearer);

        QSignalSpy credentialChanged(&client, &SunoClient::credentialChanged);
        client.clearLocalCredentials();
        QCOMPARE(credentialChanged.count(), 1);
        QVERIFY(!client.isAuthenticated());
        QVERIFY(client.configuredCredential().isEmpty());
        QCOMPARE(client.authState(), vc::suno::auth::AuthState::Disconnected);
    }

    /// T0167: a sync that ends must say whether it ended COMPLETELY, on every
    /// exit path.
    void everySyncEndReportsCompleteness()
    {
        // Signed out: the refresh attempt cannot even start, and it must still
        // settle -- with complete=false, zero pages, and a reason. Before this
        // signal existed the attempt's only output was libraryFetchFailed, so
        // nothing downstream could tell "could not sync" from "synced and the
        // library is empty".
        SunoClient client(QStringLiteral("test-device"), nullptr, [](auto) {
            return CredentialStoreWorker::Outcome{};
        });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        QSignalSpy settled(&manager, &SunoLibraryManager::librarySyncSettled);
        QSignalSpy fetchFailed(&manager, &SunoLibraryManager::libraryFetchFailed);

        manager.refreshLibrary(1);
        QTRY_COMPARE_WITH_TIMEOUT(fetchFailed.count(), 1, 2000);
        QCOMPARE(settled.count(), 1);

        QList<QVariant> args = settled.first();
        QCOMPARE(args.at(0).toBool(), false);          // complete
        QCOMPARE(args.at(1).toInt(), 0);               // pagesFetched
        QCOMPARE(args.at(2).toInt(), 0);               // clipCount
        QVERIFY(args.at(4).toString().length() > 0);   // a reason is carried
    }

    /// A malformed envelope must end the sync, not become an empty success that
    /// leaves the manager re-requesting the same cursor forever.
    void malformedFeedPageEndsTheSyncInsteadOfLooping()
    {
        // The failure travels the client's libraryPageFailed channel -- NOT the
        // global errorOccurred, which also fires for explore/notifications and
        // which would abort a library sync because of an unrelated lyrics
        // failure (the T0017 coupling, rebuilt here if reused).
        // The reply is NOT completed inside the factory: FakeReply::succeed()
        // emits finished() at that instant, which is BEFORE the client has
        // connected to the reply it just received, so a completion fired there
        // is never observed. The factory hands the reply back unfinished and
        // the test completes it from the outside -- the same drive-it-later
        // pattern every other FakeReply consumer in this tree uses.
        fake_net::FakeReply* feedReply = nullptr;
        const QString storedBearer = fakeBearer();
        SunoClient client(
            QStringLiteral("test-device"), nullptr,
            [storedBearer](CredentialStoreWorker::Request request) {
                CredentialStoreWorker::Outcome outcome;
                if (request.operation == CredentialStoreWorker::Operation::Restore) {
                    outcome.bearer = storedBearer;
                }
                return outcome;
            },
            [&feedReply](const QNetworkRequest& request, const std::string&,
                         const QByteArray&) {
                auto* reply = new fake_net::FakeReply(request);
                feedReply = reply;
                return reply;
            });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        // libraryPageFailed is a vc::Signal, not a Qt signal, so QSignalSpy
        // cannot observe it; a lambda counter is the equivalent.
        int pageFailedCount = 0;
        auto pageFailedId =
            client.libraryPageFailed.connect([&pageFailedCount](const std::string&) {
                ++pageFailedCount;
            });
        QSignalSpy fetchFailed(&manager, &SunoLibraryManager::libraryFetchFailed);

        // An authenticated client whose feed reply is a valid 200 with a body
        // that is not a feed envelope.
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        manager.refreshLibrary(1);
        // The client's request queue ticks at its 1 Hz politeness interval, so
        // the feed request -- and with it `feedReply` -- may take a moment to
        // appear. Once it does, complete it with the malformed envelope.
        QTRY_VERIFY_WITH_TIMEOUT(feedReply != nullptr, 5000);
        feedReply->succeed(QByteArrayLiteral("{\"no_clips_here\": true}"));
        QTRY_COMPARE_WITH_TIMEOUT(fetchFailed.count(), 1, 5000);
        QVERIFY(pageFailedCount >= 1);
        client.libraryPageFailed.disconnect(pageFailedId);
        QVERIFY(pageFailedCount >= 1);
        client.libraryPageFailed.disconnect(pageFailedId);
        // The manager must NOT have left the syncing gate raised: a stuck gate
        // is how a failed sync becomes a sync that can never be retried.
        QTRY_VERIFY_WITH_TIMEOUT(!manager.hasMorePages(), 2000);
    }
};

int runTestSunoLibraryManager(int argc, char** argv)
{
    TestSunoLibraryManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_SunoLibraryManager.moc"
