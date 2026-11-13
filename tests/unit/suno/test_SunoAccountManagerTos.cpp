#include <QtTest>

#include "suno/SunoAccountManager.hpp"
#include "suno/SunoClient.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <string>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {

QString fakeBearer()
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

QByteArray tosBody(bool accepted, const QString& timestamp)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("has_accepted_tos"), accepted},
                                     {QStringLiteral("has_accepted_tos_timestamp"), timestamp}})
            .toJson(QJsonDocument::Compact);
}

struct Rig {
    Rig()
        : client(QStringLiteral("test-device"), nullptr, emptyBackend(),
                 [this](const QNetworkRequest& request, const std::string& method,
                        const QByteArray& body) {
                     auto* reply = new FakeReply(request, method);
                     requests.push_back(request);
                     methods.push_back(method);
                     bodies.push_back(body);
                     replies.push_back(reply);
                     return reply;
                 })
    {
        client.setToken(fakeBearer().toStdString());
    }

    SunoClient client;
    std::vector<QNetworkRequest> requests;
    std::vector<std::string> methods;
    std::vector<QByteArray> bodies;
    std::vector<FakeReply*> replies;
};

} // namespace

class TestSunoAccountManagerTos : public QObject
{
    Q_OBJECT

private slots:
    void cutoffBoundary()
    {
        QVERIFY(SunoAccountManager::needsTosReprompt(true, QStringLiteral("2025-11-07T07:59:59Z")));
        QVERIFY(!SunoAccountManager::needsTosReprompt(true, QStringLiteral("2025-11-07T08:00:00Z")));
        QVERIFY(!SunoAccountManager::needsTosReprompt(true, QStringLiteral("2025-11-07T08:00:01Z")));
        QVERIFY(!SunoAccountManager::needsTosReprompt(false, QStringLiteral("2020-01-01T00:00:00Z")));
        QVERIFY(!SunoAccountManager::needsTosReprompt(false, QStringLiteral("not-a-date")));
        QVERIFY(SunoAccountManager::needsTosReprompt(true, QStringLiteral("not-a-date")));
        QVERIFY(SunoAccountManager::needsTosReprompt(true, QString{}));
    }

    void acceptedRecentDoesNotReprompt()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::tosAcceptanceReady);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);

        account.refreshTosAcceptance();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("GET"));
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/user/tos_acceptance")));

        rig.replies.front()->respond(tosBody(true, QStringLiteral("2026-01-15T12:00:00Z")), 200);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QCOMPARE(errors.count(), 0);
        QVERIFY(account.hasTosAcceptance());
        QVERIFY(account.hasAcceptedTos());
        QCOMPARE(account.hasAcceptedTosTimestamp(), QStringLiteral("2026-01-15T12:00:00Z"));
        QVERIFY(!account.needsTosReprompt());
    }

    void acceptedStaleReprompts()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::tosAcceptanceReady);

        account.refreshTosAcceptance();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(tosBody(true, QStringLiteral("2025-10-01T00:00:00Z")), 200);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QVERIFY(account.hasAcceptedTos());
        QVERIFY(account.needsTosReprompt());
    }

    void notAcceptedDoesNotRepromptByStaleness()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::tosAcceptanceReady);

        account.refreshTosAcceptance();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(tosBody(false, QStringLiteral("2026-01-15T12:00:00Z")), 200);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QVERIFY(account.hasTosAcceptance());
        QVERIFY(!account.hasAcceptedTos());
        QVERIFY(!account.needsTosReprompt());
    }

    void malformedTimestampRepromptsOnceAccepted()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::tosAcceptanceReady);

        account.refreshTosAcceptance();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(tosBody(true, QStringLiteral("not-a-date")), 200);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QVERIFY(account.hasAcceptedTos());
        QCOMPARE(account.hasAcceptedTosTimestamp(), QStringLiteral("not-a-date"));
        QVERIFY(account.needsTosReprompt());
    }

    void transportFailureStaysUnknownAndDoesNotClaimAccepted()
    {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::tosAcceptanceReady);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);

        account.refreshTosAcceptance();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(tosBody(true, QStringLiteral("2026-01-15T12:00:00Z")), 500);
        // 5xx is retried once: the second reply is the one the handler sees.
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(tosBody(true, QStringLiteral("2026-01-15T12:00:00Z")), 500);

        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 15000);
        QCOMPARE(ready.count(), 0);
        QVERIFY(!account.hasTosAcceptance());
        QVERIFY(!account.tosAcceptance().has_value());
        QVERIFY(!account.hasAcceptedTos());
        QVERIFY(account.hasAcceptedTosTimestamp().isEmpty());
        QVERIFY(!account.needsTosReprompt());
    }

    void nonObjectEnvelopesAreRejectedNotTreatedAsEmpty() {
        Rig rig;
        SunoAccountManager account(&rig.client);
        QSignalSpy ready(&account, &SunoAccountManager::accountInfoReady);
        QSignalSpy errors(&account, &SunoAccountManager::accountError);

        account.refreshAll();
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        // refreshAll enqueues SESSION first, then billing. A valid-but-non-object
        // body used to become an empty QJsonObject, so the session path reported
        // the misleading "no user object" instead of naming the real fault.
        rig.replies[0]->respond(QByteArray("[]"), 200);
        rig.replies[1]->respond(QByteArray("[]"), 200);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 2, 15000);
        QCOMPARE(ready.count(), 0);
        bool sawSession = false;
        bool sawBilling = false;
        for (const auto& signal : errors) {
            const QString message = signal.at(0).toString();
            if (message.contains(QStringLiteral("session envelope: expected a JSON object"))) {
                sawSession = true;
            }
            if (message.contains(QStringLiteral("billing envelope: expected a JSON object"))) {
                sawBilling = true;
            }
        }
        QVERIFY(sawSession);
        QVERIFY(sawBilling);
    }
};

QTEST_MAIN(TestSunoAccountManagerTos)
#include "test_SunoAccountManagerTos.moc"
