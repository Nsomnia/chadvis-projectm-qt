#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoLibrarySectionService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QPointer>

#include <cstring>
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

/// The captured trash body: `{clips:[<song_schema>]}` (§4.12). The extra
/// `unknown_future_field` is deliberate: the parser must keep what it does not
/// know rather than map onto invented types.
QByteArray trashedBody()
{
    return QByteArrayLiteral(R"({
        "clips": [
            {
                "id": "11111111-2222-3333-4444-555555555555",
                "title": "Trashed One",
                "status": "complete",
                "is_trashed": true,
                "unknown_future_field": {"nested": [1, 2]}
            },
            {
                "id": "66666666-7777-8888-9999-000000000000",
                "title": "Trashed Two",
                "status": "complete",
                "is_trashed": true
            }
        ]
    })");
}

/// The captured history body: `{history:[{content:<song_schema>}]}` (§4.12).
QByteArray historyBody()
{
    return QByteArrayLiteral(R"({
        "history": [
            {
                "content": {
                    "id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
                    "title": "Listened One",
                    "status": "complete"
                }
            }
        ]
    })");
}

ScriptedReply* findReply(const std::vector<QPointer<ScriptedReply>>& replies,
                         const QString& suffix, int occurrence = 0)
{
    int seen = 0;
    for (QPointer<ScriptedReply> reply : replies) {
        if (reply.isNull()) {
            continue;
        }
        if (!reply->url().path().endsWith(suffix)) {
            continue;
        }
        if (seen == occurrence) {
            return reply;
        }
        ++seen;
    }
    return nullptr;
}

} // namespace

class TestSunoLibrarySectionService : public QObject
{
    Q_OBJECT

private slots:
    void trashedClipsAreKeptAsTheServerSentThem()
    {
        const QJsonDocument document = QJsonDocument::fromJson(trashedBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoLibrarySectionService::parseTrashed(document.object());
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed[0].value(QStringLiteral("title")).toString(),
                 QStringLiteral("Trashed One"));
        QVERIFY(parsed[0].value(QStringLiteral("is_trashed")).toBool());
        // Nothing the capture did not pin down was dropped.
        QVERIFY(parsed[0].contains(QStringLiteral("unknown_future_field")));
        QCOMPARE(parsed[1].value(QStringLiteral("id")).toString(),
                 QStringLiteral("66666666-7777-8888-9999-000000000000"));
    }

    void historyKeepsTheContentWrapper()
    {
        const QJsonDocument document = QJsonDocument::fromJson(historyBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoLibrarySectionService::parseHistory(document.object());
        QCOMPARE(parsed.size(), 1);
        const QJsonObject content = parsed[0].value(QStringLiteral("content")).toObject();
        QCOMPARE(content.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Listened One"));
    }

    void aMissingOrNonArrayKeyIsAnEmptyList()
    {
        QVERIFY(SunoLibrarySectionService::parseTrashed(QJsonObject{}).isEmpty());
        QVERIFY(SunoLibrarySectionService::parseTrashed(
                        QJsonObject{{QStringLiteral("clips"), QStringLiteral("nope")}})
                        .isEmpty());
        QVERIFY(SunoLibrarySectionService::parseHistory(QJsonObject{}).isEmpty());
        QVERIFY(SunoLibrarySectionService::parseHistory(
                        QJsonObject{{QStringLiteral("history"), 7}})
                        .isEmpty());
    }

    void noClientReportsSignInAndStaysEmpty()
    {
        SunoLibrarySectionService service(nullptr);
        QSignalSpy failed(&service, &SunoLibrarySectionService::failed);

        service.loadTrashed();

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(service.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.records().isEmpty());
        QVERIFY(!service.isLoading());

        service.loadHistory();

        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.history().isEmpty());
        QVERIFY(!service.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<ScriptedReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new ScriptedReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        SunoLibrarySectionService service(&client);
        QSignalSpy failed(&service, &SunoLibrarySectionService::failed);

        service.loadTrashed();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isLoading());
        ScriptedReply* reply = findReply(replies, QStringLiteral("/clips/trashed_v2"));
        QVERIFY(reply != nullptr);

        reply->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(service.records().isEmpty());
    }

    void anHttpErrorAfterRetryDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<ScriptedReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new ScriptedReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoLibrarySectionService service(&client);
        QSignalSpy failed(&service, &SunoLibrarySectionService::failed);

        service.loadHistory();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        ScriptedReply* first = findReply(replies, QStringLiteral("/profiles/listen-history"));
        QVERIFY(first != nullptr);
        first->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is transient: SunoClient re-sends once after its backoff floor, so
        // only the second reply's status reaches the service. The first reply is
        // deleted before the retry, so it no longer counts as an occurrence.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        ScriptedReply* second = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((second = findReply(replies, QStringLiteral("/profiles/listen-history"))) != nullptr, 15000);
        second->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.history().isEmpty());
    }

    // TODO(T0383): success path has not run yet -- the orchestrator build is the
    // first execution. Analysis (and any fixture correction) happens after it.
    void successfulLoadsFillBothSections()
    {
        std::vector<QPointer<ScriptedReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new ScriptedReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoLibrarySectionService service(&client);
        QSignalSpy changed(&service, &SunoLibrarySectionService::sectionChanged);

        service.loadTrashed();

        ScriptedReply* trash = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (trash = findReply(replies, QStringLiteral("/clips/trashed_v2"))) != nullptr,
                15000);
        trash->respond(trashedBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.records().size() == 2, 15000);

        // Loaded after the first settles, so queue serialisation cannot starve
        // the second request.
        service.loadHistory();
        ScriptedReply* history = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (history = findReply(replies, QStringLiteral("/profiles/listen-history")))
                        != nullptr,
                15000);
        history->respond(historyBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.history().size() == 1, 15000);

        QCOMPARE(changed.count(), 4);
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isLoading());
    }
};

QTEST_MAIN(TestSunoLibrarySectionService)
#include "test_SunoLibrarySectionService.moc"
