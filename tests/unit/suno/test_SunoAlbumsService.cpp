#include <QtTest>

#include "suno/SunoAlbumsService.hpp"
#include "suno/SunoClient.hpp"

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

QByteArray albumsBody()
{
    return QByteArrayLiteral(R"({
        "feed": {
            "feed_id": "library_albums",
            "feed_title": "Albums",
            "feed_direct_link": null,
            "items": [
                {
                    "content_id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
                    "content_type": "album",
                    "content_item": {"id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "title": "Half Light"}
                },
                {
                    "content_id": "ffffffff-1111-2222-3333-444444444444",
                    "content_type": "album",
                    "content_item": {"id": "ffffffff-1111-2222-3333-444444444444", "title": "Second"}
                }
            ],
            "item_count": 2
        }
    })");
}

ScriptedReply* findReply(const std::vector<QPointer<ScriptedReply>>& replies,
                         const QString& suffix)
{
    for (QPointer<ScriptedReply> reply : replies) {
        if (reply.isNull()) {
            continue;
        }
        if (reply->url().path().endsWith(suffix)) {
            return reply;
        }
    }
    return nullptr;
}

} // namespace

class TestSunoAlbumsService : public QObject
{
    Q_OBJECT

private slots:
    void feedTitleAndItemsAreParsed()
    {
        const QJsonDocument document = QJsonDocument::fromJson(albumsBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoAlbumsService::parseAlbums(document.object());
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed[0].value(QStringLiteral("content_type")).toString(),
                 QStringLiteral("album"));
        QCOMPARE(parsed[0].value(QStringLiteral("content_id")).toString(),
                 QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
        QCOMPARE(parsed[1].value(QStringLiteral("content_item")).toObject().value(
                         QStringLiteral("title")).toString(),
                 QStringLiteral("Second"));
        QCOMPARE(SunoAlbumsService::parseFeedTitle(document.object()),
                 QStringLiteral("Albums"));
        QCOMPARE(SunoAlbumsService::parseFeedId(document.object()),
                 QStringLiteral("library_albums"));
    }

    void aMissingFeedIsAnEmptyList()
    {
        QVERIFY(SunoAlbumsService::parseAlbums(QJsonObject{}).isEmpty());
        QVERIFY(SunoAlbumsService::parseAlbums(
                        QJsonObject{{QStringLiteral("feed"), QJsonObject{}}})
                        .isEmpty());
        QVERIFY(SunoAlbumsService::parseAlbums(QJsonObject{
                        {QStringLiteral("feed"),
                         QJsonObject{{QStringLiteral("items"), QStringLiteral("nope")}}}})
                        .isEmpty());
        QVERIFY(SunoAlbumsService::parseFeedTitle(QJsonObject{}).isEmpty());
        QVERIFY(SunoAlbumsService::parseFeedId(QJsonObject{}).isEmpty());
    }

    void requestBodyIsExactlyTheAlbumsShape()
    {
        const QJsonObject body = SunoAlbumsService::requestBody();
        QCOMPARE(body.value(QStringLiteral("feed_id")).toString(),
                 QStringLiteral("library_albums"));
        QVERIFY(body.value(QStringLiteral("cursor")).isNull());
        QCOMPARE(body.value(QStringLiteral("page_size")).toInt(), 12);
        const QJsonObject metadata = body.value(QStringLiteral("request_metadata")).toObject();
        QCOMPARE(metadata.value(QStringLiteral("term")).toString(), QStringLiteral(""));
        QCOMPARE(metadata.value(QStringLiteral("library_scope")).toString(),
                 QStringLiteral("all"));
        QCOMPARE(body.size(), 4);
        QCOMPARE(metadata.size(), 2);
    }

    void noClientReportsSignInAndStaysEmpty()
    {
        SunoAlbumsService service(nullptr);
        QSignalSpy failed(&service, &SunoAlbumsService::failed);

        service.loadAlbums();

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(service.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.albums().isEmpty());
        QVERIFY(service.feedTitle().isEmpty());
        QVERIFY(service.feedId().isEmpty());
        QVERIFY(!service.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<ScriptedReply>> replies;
        QByteArray sentPayload;
        auto factory = [&replies, &sentPayload](const QNetworkRequest& request,
                                                 const std::string& method,
                                                 const QByteArray& payload) {
            sentPayload = payload;
            auto* reply = new ScriptedReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        SunoAlbumsService service(&client);
        QSignalSpy failed(&service, &SunoAlbumsService::failed);

        service.loadAlbums();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isLoading());
        QVERIFY(!sentPayload.isEmpty());

        // The bytes on the wire are the albums request body.
        const QJsonDocument sentDocument = QJsonDocument::fromJson(sentPayload);
        QVERIFY(sentDocument.isObject());
        QCOMPARE(sentDocument.object(), SunoAlbumsService::requestBody());

        ScriptedReply* reply = findReply(replies, QStringLiteral("/unified/feed"));
        QVERIFY(reply != nullptr);
        reply->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(service.albums().isEmpty());
        QVERIFY(service.feedTitle().isEmpty());
        QVERIFY(service.feedId().isEmpty());
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

        SunoAlbumsService service(&client);
        QSignalSpy failed(&service, &SunoAlbumsService::failed);

        service.loadAlbums();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        ScriptedReply* first = findReply(replies, QStringLiteral("/unified/feed"));
        QVERIFY(first != nullptr);
        first->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is transient: SunoClient re-sends once after its backoff floor, so
        // only the second reply's status reaches the service.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        ScriptedReply* second = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((second = findReply(replies, QStringLiteral("/unified/feed")))
                                 != nullptr,
                                 15000);
        second->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.albums().isEmpty());
    }

    // TODO(T0383): success path has not run yet -- the orchestrator build is the
    // first execution. Analysis (and any fixture correction) happens after it.
    void successfulLoadFillsAlbums()
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

        SunoAlbumsService service(&client);
        QSignalSpy changed(&service, &SunoAlbumsService::sectionChanged);

        service.loadAlbums();

        ScriptedReply* reply = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((reply = findReply(replies, QStringLiteral("/unified/feed")))
                                 != nullptr,
                                 15000);
        reply->respond(albumsBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.albums().size() == 2, 15000);

        QCOMPARE(changed.count(), 2);
        QCOMPARE(service.feedTitle(), QStringLiteral("Albums"));
        QCOMPARE(service.feedId(), QStringLiteral("library_albums"));
        QCOMPARE(service.albums()[0].value(QStringLiteral("content_type")).toString(),
                 QStringLiteral("album"));
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isLoading());
    }
};

QTEST_MAIN(TestSunoAlbumsService)
#include "test_SunoAlbumsService.moc"
