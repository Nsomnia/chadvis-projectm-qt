#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoSearchService.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSignalSpy>

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

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
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

class TestSunoSearchService : public QObject
{
    Q_OBJECT

private slots:
    void noClientReportsSignInAndStaysEmpty()
    {
        SunoSearchService service(nullptr);
        QSignalSpy failed(&service, &SunoSearchService::failed);

        service.suggest(QStringLiteral("beats"), 5, 10);
        service.search(QStringLiteral("beats"), QStringLiteral("enter"));

        QCOMPARE(failed.count(), 2);
        QVERIFY(service.state().suggestionFeed.isEmpty());
        QVERIFY(service.state().searchFeed.isEmpty());
        QVERIFY(service.state().searchItems.isEmpty());
        QVERIFY(!service.isLoading());
    }

    void emptyTermIsRefusedWithoutARequest()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy failed(&service, &SunoSearchService::failed);

        service.suggest(QString(), 5, 10);
        service.search(QString(), QStringLiteral("enter"));

        QCOMPARE(failed.count(), 2);
        QVERIFY(rig.replies.empty());
        QVERIFY(service.state().failureReason.contains(QStringLiteral("empty")));
        QVERIFY(!service.isLoading());
    }

    void suggestSuccessPostsCapturedBodyAndKeepsRawFeed()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy changed(&service, &SunoSearchService::changed);

        service.suggest(QStringLiteral("neon"), 5, 10);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("POST"));
        QVERIFY(rig.requests.front().url().path().endsWith(
                QStringLiteral("/unified/search/suggest")));
        const QJsonDocument sent = QJsonDocument::fromJson(rig.bodies.front());
        QCOMPARE(sent.object().value(QStringLiteral("term")).toString(),
                 QStringLiteral("neon"));
        QCOMPARE(sent.object().value(QStringLiteral("suggestion_size")).toInt(), 5);
        QCOMPARE(sent.object().value(QStringLiteral("content_size")).toInt(), 10);

        const QJsonObject feed{{QStringLiteral("feed_id"), QStringLiteral("suggest-feed")},
                               {QStringLiteral("feed_title"), QStringLiteral("Suggestions")}};
        rig.replies.front()->respond(compact({{QStringLiteral("feed"), feed}}), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.state().suggestionFeed.value(QStringLiteral("feed_id")).toString(),
                 QStringLiteral("suggest-feed"));
        QCOMPARE(service.state().suggestionTerm, QStringLiteral("neon"));
        QVERIFY(service.state().failureReason.isEmpty());
        QVERIFY(!service.isLoading());
    }

    void searchSuccessPostsCapturedBodyAndExtractsFeedKeys()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy changed(&service, &SunoSearchService::changed);

        service.search(QStringLiteral("neon"), QStringLiteral("enter"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("POST"));
        QVERIFY(rig.requests.front().url().path().endsWith(
                QStringLiteral("/unified/search/omnisearch")));
        const QJsonDocument sent = QJsonDocument::fromJson(rig.bodies.front());
        QCOMPARE(sent.object().value(QStringLiteral("term")).toString(),
                 QStringLiteral("neon"));
        QCOMPARE(sent.object().value(QStringLiteral("completion_method")).toString(),
                 QStringLiteral("enter"));

        QJsonArray items;
        items.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("clip")}});
        const QJsonObject feed{{QStringLiteral("feed_id"), QStringLiteral("explore-feed")},
                               {QStringLiteral("feed_title"), QStringLiteral("Results")},
                               {QStringLiteral("items"), items}};
        rig.replies.front()->respond(compact({{QStringLiteral("feed"), feed}}), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.state().searchFeedId, QStringLiteral("explore-feed"));
        QCOMPARE(service.state().searchFeedTitle, QStringLiteral("Results"));
        QCOMPARE(service.state().searchItems.size(), 1);
        QCOMPARE(service.state().searchItems.at(0).toObject()
                         .value(QStringLiteral("kind"))
                         .toString(),
                 QStringLiteral("clip"));
        QVERIFY(service.state().failureReason.isEmpty());
    }

    void suggestHttpErrorDegradesToEmptyWithAReason()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy failed(&service, &SunoSearchService::failed);

        service.suggest(QStringLiteral("neon"), 5, 10);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        // 5xx is retried once: the second reply is the one the service sees.
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().failureReason.contains(QStringLiteral("500")));
        QVERIFY(service.state().suggestionFeed.isEmpty());
    }

    void searchHttpErrorDegradesToEmptyWithAReason()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy failed(&service, &SunoSearchService::failed);

        service.search(QStringLiteral("neon"), QStringLiteral("enter"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().failureReason.contains(QStringLiteral("500")));
        QVERIFY(service.state().searchFeed.isEmpty());
        QVERIFY(service.state().searchItems.isEmpty());
    }

    void failureIsolatesTheOtherRoute()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy failed(&service, &SunoSearchService::failed);

        service.search(QStringLiteral("neon"), QStringLiteral("enter"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        const QJsonObject feed{{QStringLiteral("feed_id"), QStringLiteral("explore-feed")},
                               {QStringLiteral("feed_title"), QStringLiteral("Results")},
                               {QStringLiteral("items"), QJsonArray{}}};
        rig.replies.front()->respond(compact({{QStringLiteral("feed"), feed}}), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.state().searchFeedId == QStringLiteral("explore-feed"),
                                 15000);

        service.suggest(QStringLiteral("neon"), 5, 10);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.at(1)->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 3, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QCOMPARE(service.state().searchFeedId, QStringLiteral("explore-feed"));
        QVERIFY(service.state().suggestionFeed.isEmpty());
    }

    void suggestStaleReplyIsDropped()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy changed(&service, &SunoSearchService::changed);

        service.suggest(QStringLiteral("old"), 3, 4);
        service.suggest(QStringLiteral("new"), 5, 10);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        const auto bodyWithId = [](const QString& id) {
            return compact({{QStringLiteral("feed"),
                             QJsonObject{{QStringLiteral("feed_id"), id}}}});
        };
        rig.replies.at(0)->respond(bodyWithId(QStringLiteral("old")), 200);
        rig.replies.at(1)->respond(bodyWithId(QStringLiteral("new")), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().suggestionFeed.value(QStringLiteral("feed_id")).toString(),
                 QStringLiteral("new"));
    }

    void searchStaleReplyIsDropped()
    {
        Rig rig;
        SunoSearchService service(&rig.client);
        QSignalSpy changed(&service, &SunoSearchService::changed);

        service.search(QStringLiteral("old"), QStringLiteral("enter"));
        service.search(QStringLiteral("new"), QStringLiteral("enter"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        const auto bodyWithId = [](const QString& id) {
            return compact({{QStringLiteral("feed"),
                             QJsonObject{{QStringLiteral("feed_id"), id},
                                         {QStringLiteral("feed_title"), id},
                                         {QStringLiteral("items"), QJsonArray{}}}}});
        };
        rig.replies.at(0)->respond(bodyWithId(QStringLiteral("old")), 200);
        rig.replies.at(1)->respond(bodyWithId(QStringLiteral("new")), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().searchFeedId, QStringLiteral("new"));
    }
};

QTEST_MAIN(TestSunoSearchService)
#include "test_SunoSearchService.moc"
