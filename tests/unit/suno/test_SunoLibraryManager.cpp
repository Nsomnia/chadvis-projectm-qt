#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoLibraryManager.hpp"

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
};

int runTestSunoLibraryManager(int argc, char** argv)
{
    TestSunoLibraryManager test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_SunoLibraryManager.moc"
