#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoPersonaService.hpp"

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

namespace {

/// Same scripted seam the sibling suno suites use: nothing touches the network.
class ScriptedReply final : public QNetworkReply
{
public:
    ScriptedReply(const QNetworkRequest& request, const std::string& method)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(method == "POST" ? QNetworkAccessManager::PostOperation
                                      : QNetworkAccessManager::GetOperation);
        setOpenMode(ReadOnly | Unbuffered);
    }

    void abort() override
    {
        if (finished_) {
            return;
        }
        finished_ = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return payload_.size() - offset_ + QNetworkReply::bytesAvailable();
    }

    qint64 readData(char* data, qint64 maxSize) override
    {
        if (offset_ >= payload_.size()) {
            return 0;
        }
        const qint64 count = qMin(maxSize, static_cast<qint64>(payload_.size()) - offset_);
        std::memcpy(data, payload_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

    void respond(QByteArray body, int status)
    {
        if (finished_) {
            return;
        }
        payload_ = std::move(body);
        offset_ = 0;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        finished_ = true;
        emit finished();
    }

private:
    QByteArray payload_;
    qint64 offset_ = 0;
    bool finished_ = false;
};

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
        std::vector<ScriptedReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new ScriptedReply(request, method);
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
        std::vector<ScriptedReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new ScriptedReply(request, method);
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
        std::vector<ScriptedReply*> replies;
        auto factory = [&requests, &replies](const QNetworkRequest& request,
                                             const std::string& method, const QByteArray&) {
            requests.emplace_back(request.url().path(), QString::fromStdString(method));
            auto* reply = new ScriptedReply(request, method);
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
};

QTEST_MAIN(TestSunoPersonaService)
#include "test_SunoPersonaService.moc"
