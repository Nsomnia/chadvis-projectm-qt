#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
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

QByteArray feedEnvelope(
    std::initializer_list<QString> clipIds, bool hasMore, const QString& nextCursor)
{
    QJsonArray clips;
    for (const QString& id : clipIds) {
        clips.append(QJsonObject{{QStringLiteral("id"), id}});
    }
    const QJsonObject envelope{
        {QStringLiteral("clips"), clips},
        {QStringLiteral("has_more"), hasMore},
        {QStringLiteral("next_cursor"), nextCursor},
    };
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

QJsonObject feedRequest(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

QString feedSearchText(const QByteArray& body)
{
    return feedRequest(body).value(QStringLiteral("filters")).toObject().value(
        QStringLiteral("searchText")).toString();
}

bool feedHasSearchText(const QByteArray& body)
{
    return feedRequest(body).value(QStringLiteral("filters")).toObject().contains(
        QStringLiteral("searchText"));
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
        QByteArray feedBody;
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
            [&feedReply, &feedBody](const QNetworkRequest& request, const std::string&,
                                    const QByteArray& body) {
                feedBody = body;
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

    // 100 clips per request, not 20: the first sync pages the entire library,
    // so the page size is the difference between 108 round trips and 22 on a
    // 2 147-clip library. The server accepts up to 100 and answers 400 above it
    // (measured 2026-10-25); a server that later caps it lower still works,
    // because `has_more` decides when to stop.
    void theFirstPageRequestsAHundredClips()
    {
        fake_net::FakeReply* feedReply = nullptr;
        QByteArray feedBody;
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
            [&feedReply, &feedBody](const QNetworkRequest& request, const std::string&,
                                    const QByteArray& body) {
                feedBody = body;
                auto* reply = new fake_net::FakeReply(request);
                feedReply = reply;
                return reply;
            });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());
        manager.refreshLibrary(1);

        QTRY_VERIFY_WITH_TIMEOUT(feedReply != nullptr, 5000);
        const QJsonObject body = QJsonDocument::fromJson(feedBody).object();
        QCOMPARE(body.value(QStringLiteral("limit")).toInt(), 100);

        // Leave nothing pending: an uncompeted reply keeps the 1 Hz queue busy
        // into the next case.
        feedReply->succeed(QByteArrayLiteral("{\"clips\":[],\"has_more\":false}"));
    }

    /// T0334: a retryable failure on page 2 must not discard page 1. The
    /// settled report carries pagesFetched=1, pagesLoaded stays 1, and the
    /// next request reuses the held cursor (page 2), never page 1.
    void failedPageIsRetriedNotRestarted()
    {
        std::vector<fake_net::FakeReply*> replies;
        std::vector<QByteArray> requestBodies;
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
            [&replies, &requestBodies](const QNetworkRequest& request, const std::string&,
                                        const QByteArray& body) {
                requestBodies.push_back(body);
                auto* reply = new fake_net::FakeReply(request);
                replies.push_back(reply);
                return reply;
            });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        QSignalSpy settled(&manager, &SunoLibraryManager::librarySyncSettled);
        QSignalSpy fetchFailed(&manager, &SunoLibraryManager::libraryFetchFailed);

        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        manager.refreshLibrary(1);
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 5000);
        const auto firstCursor =
            QJsonDocument::fromJson(requestBodies[0]).object().value(QStringLiteral("cursor"));
        QVERIFY(firstCursor.isNull());

        replies[0]->succeed(QByteArrayLiteral(
            R"({"clips":[{"id":"clip-1"},{"id":"clip-2"}],"has_more":true,"next_cursor":"cursor-page-2"})"));
        QTRY_COMPARE_WITH_TIMEOUT(manager.currentPage(), 1, 5000);
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(2));

        // The auto-pagination timer (1.1s) requests page 2; fail it with a
        // retryable transport error.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 5000);
        replies[1]->fail(QNetworkReply::TemporaryNetworkFailureError, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fetchFailed.count(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 5000);

        QList<QVariant> failedArgs = settled.first();
        QCOMPARE(failedArgs.at(0).toBool(), false); // complete
        QCOMPARE(failedArgs.at(1).toInt(), 1);      // pagesFetched survives
        QCOMPARE(failedArgs.at(2).toInt(), 2);      // clipCount
        QCOMPARE(manager.currentPage(), 1);        // page 1 not discarded
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(2));
        QVERIFY(manager.hasMorePages());           // still resumable

        manager.requestNextPage();
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 3, 5000);
        const auto resumeCursor =
            QJsonDocument::fromJson(requestBodies[2]).object().value(QStringLiteral("cursor"));
        QCOMPARE(resumeCursor.toString(), QStringLiteral("cursor-page-2"));
        QCOMPARE(manager.currentPage(), 1); // unchanged until page 2 lands

        replies[2]->succeed(QByteArrayLiteral(
            R"({"clips":[{"id":"clip-3"}],"has_more":false,"next_cursor":""})"));
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 2, 5000);
        QList<QVariant> doneArgs = settled.last();
        QCOMPARE(doneArgs.at(0).toBool(), true); // complete
        QCOMPARE(doneArgs.at(1).toInt(), 2);     // pagesFetched
        QCOMPARE(doneArgs.at(2).toInt(), 3);     // clipCount
        QCOMPARE(manager.currentPage(), 2);
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(3));
    }

    /// T0366: changing the term starts a NEW feed: the new first page is a
    /// null cursor carrying the new filters.searchText, and the previous
    /// page's clips are discarded rather than appended.
    void aTermChangeStartsANewFeedInsteadOfAppending()
    {
        std::vector<fake_net::FakeReply*> replies;
        std::vector<QByteArray> requestBodies;
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
            [&replies, &requestBodies](const QNetworkRequest& request, const std::string&,
                                        const QByteArray& body) {
                requestBodies.push_back(body);
                auto* reply = new fake_net::FakeReply(request);
                replies.push_back(reply);
                return reply;
            });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        manager.setSearchText(QStringLiteral("first"));
        QCOMPARE(manager.searchText(), QStringLiteral("first"));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 5000);
        QVERIFY(feedRequest(requestBodies[0]).value(QStringLiteral("cursor")).isNull());
        QCOMPARE(feedSearchText(requestBodies[0]), QStringLiteral("first"));
        replies[0]->succeed(feedEnvelope({QStringLiteral("clip-a1"), QStringLiteral("clip-a2")},
                                         false, QString()));
        QTRY_COMPARE_WITH_TIMEOUT(manager.accumulatedClips().size(), std::size_t(2), 5000);

        manager.setSearchText(QStringLiteral("second"));
        QCOMPARE(manager.searchText(), QStringLiteral("second"));
        // Discarded immediately, before the new page lands.
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(0));
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 5000);
        QVERIFY(feedRequest(requestBodies[1]).value(QStringLiteral("cursor")).isNull());
        QCOMPARE(feedSearchText(requestBodies[1]), QStringLiteral("second"));
        replies[1]->succeed(feedEnvelope({QStringLiteral("clip-b1")}, false, QString()));
        QTRY_COMPARE_WITH_TIMEOUT(manager.accumulatedClips().size(), std::size_t(1), 5000);
        QCOMPARE(manager.currentPage(), 1);
        QCOMPARE(QString::fromStdString(manager.accumulatedClips()[0].id),
                 QStringLiteral("clip-b1"));
    }

    /// T0366: clearing the term returns to the unfiltered feed: the new
    /// first page omits filters.searchText entirely and replaces the
    /// search accumulation.
    void anEmptyTermReturnsToTheUnfilteredFeed()
    {
        std::vector<fake_net::FakeReply*> replies;
        std::vector<QByteArray> requestBodies;
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
            [&replies, &requestBodies](const QNetworkRequest& request, const std::string&,
                                        const QByteArray& body) {
                requestBodies.push_back(body);
                auto* reply = new fake_net::FakeReply(request);
                replies.push_back(reply);
                return reply;
            });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        manager.setSearchText(QStringLiteral("term"));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 5000);
        QCOMPARE(feedSearchText(requestBodies[0]), QStringLiteral("term"));
        replies[0]->succeed(feedEnvelope({QStringLiteral("clip-s1")}, false, QString()));
        QTRY_COMPARE_WITH_TIMEOUT(manager.accumulatedClips().size(), std::size_t(1), 5000);

        manager.setSearchText(QString());
        QVERIFY(manager.searchText().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 5000);
        QVERIFY(feedRequest(requestBodies[1]).value(QStringLiteral("cursor")).isNull());
        QVERIFY(!feedHasSearchText(requestBodies[1]));
        // Still the preserved filter controls, just no search key.
        QCOMPARE(feedRequest(requestBodies[1])
                     .value(QStringLiteral("filters"))
                     .toObject()
                     .value(QStringLiteral("disliked"))
                     .toString(),
                 QStringLiteral("False"));
        replies[1]->succeed(
            feedEnvelope({QStringLiteral("clip-u1"), QStringLiteral("clip-u2")}, false,
                         QString()));
        QTRY_COMPARE_WITH_TIMEOUT(manager.accumulatedClips().size(), std::size_t(2), 5000);
        QCOMPARE(QString::fromStdString(manager.accumulatedClips()[0].id),
                 QStringLiteral("clip-u1"));
    }

    /// T0366 + T0334: a failed search page resumes with the term intact --
    /// the retry reuses the held cursor and the same filters.searchText,
    /// and keeps page one -- instead of restarting from page one.
    void aFailedSearchPageResumesWithTheTermIntact()
    {
        std::vector<fake_net::FakeReply*> replies;
        std::vector<QByteArray> requestBodies;
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
            [&replies, &requestBodies](const QNetworkRequest& request, const std::string&,
                                        const QByteArray& body) {
                requestBodies.push_back(body);
                auto* reply = new fake_net::FakeReply(request);
                replies.push_back(reply);
                return reply;
            });
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        SunoLibraryManager manager(&client, database);

        QSignalSpy settled(&manager, &SunoLibraryManager::librarySyncSettled);
        QSignalSpy fetchFailed(&manager, &SunoLibraryManager::libraryFetchFailed);

        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        manager.setSearchText(QStringLiteral("resume-term"));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 5000);
        QCOMPARE(feedSearchText(requestBodies[0]), QStringLiteral("resume-term"));
        replies[0]->succeed(feedEnvelope({QStringLiteral("clip-1"), QStringLiteral("clip-2")},
                                         true, QStringLiteral("cursor-page-2")));
        QTRY_COMPARE_WITH_TIMEOUT(manager.currentPage(), 1, 5000);
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(2));

        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 5000);
        QCOMPARE(feedRequest(requestBodies[1]).value(QStringLiteral("cursor")).toString(),
                 QStringLiteral("cursor-page-2"));
        QCOMPARE(feedSearchText(requestBodies[1]), QStringLiteral("resume-term"));
        replies[1]->fail(QNetworkReply::TemporaryNetworkFailureError, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fetchFailed.count(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 5000);
        QVERIFY(manager.hasMorePages());
        QCOMPARE(manager.currentPage(), 1);
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(2));

        manager.requestNextPage();
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 3, 5000);
        // Not restarted: same cursor, same term.
        QCOMPARE(feedRequest(requestBodies[2]).value(QStringLiteral("cursor")).toString(),
                 QStringLiteral("cursor-page-2"));
        QCOMPARE(feedSearchText(requestBodies[2]), QStringLiteral("resume-term"));
        replies[2]->succeed(feedEnvelope({QStringLiteral("clip-3")}, false, QString()));
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 2, 5000);
        QCOMPARE(manager.currentPage(), 2);
        QCOMPARE(manager.accumulatedClips().size(), std::size_t(3));
        QCOMPARE(manager.searchText(), QStringLiteral("resume-term"));
    }
};

int runTestSunoLibraryManager(int argc, char** argv)
{
    TestSunoLibraryManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_SunoLibraryManager.moc"
