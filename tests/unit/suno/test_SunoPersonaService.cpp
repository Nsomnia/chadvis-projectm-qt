#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoPersonaService.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>

#include <cstring>
#include <utility>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {


/// A JWT that decodes and carries a future `exp`, so setToken accepts it.
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

const char* const kPersonaList = R"({
    "personas": [
        {"id": "p-1", "name": "Midnight Alto", "persona_type": "vox",
         "is_public": false, "description": "warm",
         "vox_audio_id": "pc-1", "vocal_start_s": 1.5, "vocal_end_s": 20.0},
        {"persona_id": "p-2", "name": "Bright Tenor", "persona_type": "vox"}
    ],
    "total_count": 2
})";

const char* const kOnePersona = R"({
    "persona": {"persona_id": "p-9", "name": "Low Bass",
                "persona_type": "vox", "description": "deep"}
})";

/// Records every request the factory is asked to issue.
using RequestLog = std::vector<std::pair<QString, QString>>;

} // namespace

class TestSunoPersonaService : public QObject
{
    Q_OBJECT

private slots:
    void listParsesTheEnvelopeAndPreservesOpaqueFields()
    {
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray(kPersonaList));
        QVERIFY(document.isObject());

        const auto records = SunoPersonaService::parseList(document.object());
        QVERIFY(records.has_value());
        QCOMPARE(records->size(), 2);
        QCOMPARE(records->at(0).id, QStringLiteral("p-1"));
        QCOMPARE(records->at(0).name, QStringLiteral("Midnight Alto"));
        QCOMPARE(records->at(1).id, QStringLiteral("p-2"));

        // Fields the service does not type stay verbatim in the record.
        QCOMPARE(records->at(0).raw.value(QStringLiteral("persona_type")).toString(),
                 QStringLiteral("vox"));
        QCOMPARE(records->at(0).raw.value(QStringLiteral("vox_audio_id")).toString(),
                 QStringLiteral("pc-1"));
        QCOMPARE(records->at(0).raw.value(QStringLiteral("vocal_start_s")).toDouble(), 1.5);
    }

    void aSinglePersonaIsUnwrapped()
    {
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray(kOnePersona));
        QVERIFY(document.isObject());

        const auto persona = SunoPersonaService::parseOne(document.object());
        QVERIFY(persona.has_value());
        QCOMPARE(persona->id, QStringLiteral("p-9"));
        QCOMPARE(persona->name, QStringLiteral("Low Bass"));
        QCOMPARE(persona->raw.value(QStringLiteral("description")).toString(),
                 QStringLiteral("deep"));
    }

    void aMissingListIsAReasonNotEmpty()
    {
        const auto records = SunoPersonaService::parseList(QJsonObject{});
        QVERIFY(!records.has_value());
        QVERIFY(records.error().contains(QStringLiteral("persona list")));
    }

    void unauthenticatedReadsSaySignIn()
    {
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), {});
        QVERIFY(!client.isAuthenticated());

        SunoPersonaService service(&client);
        QSignalSpy failed(&service, &SunoPersonaService::failed);
        QSignalSpy changed(&service, &SunoPersonaService::changed);

        service.loadList();

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(service.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.records().isEmpty());
        QVERIFY(!service.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPersonaService service(&client);
        QSignalSpy failed(&service, &SunoPersonaService::failed);

        service.loadList();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isLoading());
        replies.front()->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(service.records().isEmpty());
    }

    void http500DegradesToEmptyWithAReason()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPersonaService service(&client);
        QSignalSpy failed(&service, &SunoPersonaService::failed);

        service.loadList();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        replies.front()->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is transient: SunoClient re-sends once, so the second reply is
        // the one whose status the service sees.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        replies.back()->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.records().isEmpty());
        QVERIFY(!service.isLoading());
    }

    // Analysis after orchestrator build: this is the success path; the
    // request log also pins that only GETs of the two read routes are issued.
    void aSuccessfulReadFillsStateAndNeverCallsCreate()
    {
        RequestLog requests;
        std::vector<FakeReply*> replies;
        auto factory = [&requests, &replies](const QNetworkRequest& request,
                                             const std::string& method, const QByteArray&) {
            requests.emplace_back(request.url().path(), QString::fromStdString(method));
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPersonaService service(&client);
        QSignalSpy changed(&service, &SunoPersonaService::changed);
        QSignalSpy failed(&service, &SunoPersonaService::failed);

        service.loadOne(QStringLiteral("p-9"));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(requests.front().first.endsWith(QStringLiteral("/persona/get-persona/p-9/")));
        QCOMPARE(requests.front().second, QStringLiteral("GET"));
        replies.front()->respond(QByteArray(kOnePersona), 200);

        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, 15000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(service.persona().id, QStringLiteral("p-9"));
        QCOMPARE(service.persona().name, QStringLiteral("Low Bass"));
        QVERIFY(service.records().isEmpty());
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isLoading());

        for (const auto& request : requests) {
            QVERIFY2(!request.first.contains(QStringLiteral("/persona/create")),
                     "persona reads must never touch the credit-spending create route");
            QCOMPARE(request.second, QStringLiteral("GET"));
        }
    }

    void createSendsTheEvidencedBodyAndKeepsTheReplyVerbatim()
    {
        struct SentRequest {
            QString path;
            QString method;
            QByteArray body;
        };
        std::vector<SentRequest> sent;
        std::vector<FakeReply*> replies;
        auto factory = [&sent, &replies](const QNetworkRequest& request, const std::string& method,
                                         const QByteArray& data) {
            sent.push_back({request.url().path(), QString::fromStdString(method), data});
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPersonaService service(&client);
        QSignalSpy changed(&service, &SunoPersonaService::changed);
        QSignalSpy failed(&service, &SunoPersonaService::failed);

        SunoPersonaService::CreatePersonaParams params{
            QStringLiteral("gen-1"), QStringLiteral("Midnight Alto"), QStringLiteral("warm"),
            false, QStringLiteral("pc-1"), 13.11, 178.22, QStringLiteral("jazz"),
            QStringList{QStringLiteral("gen-1")}};
        service.createPersona(params);

        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isCreating());
        QCOMPARE(sent.size(), 1);
        QVERIFY(sent.front().path.endsWith(QStringLiteral("/persona/create/")));
        QCOMPARE(sent.front().method, QStringLiteral("POST"));

        // The body is the §5.14 shape, byte-exact: same keys in the same order.
        // The create response shape is not captured (§5.14 records the request
        // body only), so this reply is a stand-in pinning that the raw object
        // is kept verbatim and a 2xx is success.
        const QJsonObject expectedBody{
            {QStringLiteral("root_clip_id"), QStringLiteral("gen-1")},
            {QStringLiteral("name"), QStringLiteral("Midnight Alto")},
            {QStringLiteral("description"), QStringLiteral("warm")},
            {QStringLiteral("is_public"), false},
            {QStringLiteral("persona_type"), QStringLiteral("vox")},
            {QStringLiteral("vox_audio_id"), QStringLiteral("pc-1")},
            {QStringLiteral("vocal_start_s"), 13.11},
            {QStringLiteral("vocal_end_s"), 178.22},
            {QStringLiteral("user_input_styles"), QStringLiteral("jazz")},
            {QStringLiteral("clips"), QJsonArray{QStringLiteral("gen-1")}},
        };
        const QByteArray expectedBytes =
                QJsonDocument(expectedBody).toJson(QJsonDocument::Compact);
        QCOMPARE(sent.front().body, expectedBytes);

        const QJsonObject expectedReply{{QStringLiteral("persona_id"), QStringLiteral("p-new")},
                                        {QStringLiteral("name"), QStringLiteral("Midnight Alto")}};
        replies.front()->respond(QJsonDocument(expectedReply).toJson(QJsonDocument::Compact), 200);

        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, 15000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(service.created(), expectedReply);
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isCreating());
    }

    void createFailureIsNotRetried()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPersonaService service(&client);
        QSignalSpy failed(&service, &SunoPersonaService::failed);

        SunoPersonaService::CreatePersonaParams params{
            QStringLiteral("gen-1"), {}, {}, false, QStringLiteral("pc-1"), 13.11, 178.22, {},
            QStringList{QStringLiteral("gen-1")}};
        service.createPersona(params);

        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        replies.front()->respond(QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Compact), 500);

        // The send opts out of the client's 401/5xx retry, so the first (and
        // only) reply settles the call: no second reply is ever scripted.
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QCOMPARE(replies.size(), 1);
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.created().isEmpty());
        QVERIFY(!service.isCreating());
    }

    void createWithNoRootClipOrStemIsRefusedWithoutARequest()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPersonaService service(&client);
        QSignalSpy failed(&service, &SunoPersonaService::failed);

        SunoPersonaService::CreatePersonaParams noStem{QStringLiteral("gen-1"), {}, {}, false,
                                                       {}, 13.11, 178.22, {},
                                                       QStringList{QStringLiteral("gen-1")}};
        service.createPersona(noStem);
        SunoPersonaService::CreatePersonaParams noRootClip{{}, {}, {}, false, QStringLiteral("pc-1"),
                                                           13.11, 178.22, {},
                                                           QStringList{QStringLiteral("gen-1")}};
        service.createPersona(noRootClip);

        QCOMPARE(failed.count(), 2);
        QVERIFY(replies.empty());
        QVERIFY(!service.isCreating());
    }
};

QTEST_MAIN(TestSunoPersonaService)
#include "test_SunoPersonaService.moc"
