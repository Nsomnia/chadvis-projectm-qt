#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoCoverArtService.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <cstring>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {


QString fakeBearer()
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(
            QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode(
        {{QStringLiteral("exp"), static_cast<qint64>(4102444800)}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) {
        return CredentialStoreWorker::Outcome{};
    };
}

/// The captured answer to POST /gen/prompt_image/:
/// {image_url, image_s3_id, upload_id}. The extra key is deliberate.
QByteArray imageBody()
{
    const QJsonObject nested{{QStringLiteral("nested"), QJsonArray{1, 2}}};
    const QJsonObject obj{{QStringLiteral("image_url"),
                           QStringLiteral("https://cdn2.suno.ai/image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70.jpeg")},
                          {QStringLiteral("image_s3_id"),
                           QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70")},
                          {QStringLiteral("upload_id"), QStringLiteral("upload_9a8b7c6d")},
                          {QStringLiteral("unknown_future_field"), nested}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

FakeReply* findReply(const std::vector<QPointer<FakeReply>>& replies,
                         const QString& suffix)
{
    for (QPointer<FakeReply> reply : replies) {
        if (!reply.isNull() && reply->url().path().endsWith(suffix)) {
            return reply;
        }
    }
    return nullptr;
}

} // namespace

class TestSunoCoverArtService : public QObject
{
    Q_OBJECT

private slots:
    void theImageIdShapeIsPreservedVerbatim()
    {
        const QJsonDocument document = QJsonDocument::fromJson(imageBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoCoverArtService::parseImage(document.object());
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->imageS3Id,
                 QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70"));
        QVERIFY(parsed->imageUrl.endsWith(QStringLiteral(".jpeg")));
        QCOMPARE(parsed->uploadId, QStringLiteral("upload_9a8b7c6d"));
        QVERIFY(!parsed->isEmpty());
    }

    void anImageIdIsRequired()
    {
        const auto parsed = SunoCoverArtService::parseImage(
            QJsonObject{{QStringLiteral("image_url"), QStringLiteral("https://x/y.jpeg")}});
        QVERIFY(!parsed.has_value());
        QVERIFY(parsed.error().contains(QStringLiteral("image id")));
    }

    void theRequestBodyIsJustThePrompt()
    {
        const QJsonObject body = SunoCoverArtService::requestBody(QStringLiteral("paper cut moon"));
        QCOMPARE(body.size(), 1);
        QCOMPARE(body.value(QStringLiteral("prompt")).toString(),
                 QStringLiteral("paper cut moon"));
    }

    void anEmptyPromptMakesNoRequest()
    {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        SunoCoverArtService service(&client);
        QSignalSpy failed(&service, &SunoCoverArtService::failed);

        service.generate(QStringLiteral("   "));

        QCOMPARE(failed.count(), 1);
        QVERIFY(service.failureReason().contains(QStringLiteral("prompt")));
        QVERIFY(service.image().isEmpty());
        QVERIFY(!service.isLoading());
        QTest::qWait(200);
        QVERIFY(replies.empty());
    }

    void signedOutReportsSignInAndStaysEmpty()
    {
        SunoCoverArtService service(nullptr);
        QSignalSpy failed(&service, &SunoCoverArtService::failed);

        service.generate(QStringLiteral("a red door"));

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(service.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.image().isEmpty());
        QVERIFY(!service.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<FakeReply>> replies;
        std::vector<QByteArray> bodies;
        auto factory = [&replies, &bodies](const QNetworkRequest& request,
                                           const std::string& method, const QByteArray& data) {
            if (request.url().path().endsWith(QStringLiteral("/gen/prompt_image/"))) {
                bodies.push_back(data);
            }
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoCoverArtService service(&client);
        QSignalSpy failed(&service, &SunoCoverArtService::failed);

        service.generate(QStringLiteral("a red door"));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isLoading());
        FakeReply* reply = findReply(replies, QStringLiteral("/gen/prompt_image/"));
        QVERIFY(reply != nullptr);
        QCOMPARE(static_cast<int>(reply->operation()),
                 static_cast<int>(QNetworkAccessManager::PostOperation));
        QVERIFY(!bodies.empty());
        QCOMPARE(
            QString::fromUtf8(QJsonDocument::fromJson(bodies.front()).toJson(
                QJsonDocument::Compact)),
            QString::fromUtf8(QJsonDocument(SunoCoverArtService::requestBody(
                                   QStringLiteral("a red door")))
                                  .toJson(QJsonDocument::Compact)));

        reply->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(service.image().isEmpty());
    }

    void anHttpErrorAfterRetryDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoCoverArtService service(&client);
        QSignalSpy failed(&service, &SunoCoverArtService::failed);

        service.generate(QStringLiteral("a red door"));
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        FakeReply* first = findReply(replies, QStringLiteral("/gen/prompt_image/"));
        QVERIFY(first != nullptr);
        first->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is transient: SunoClient re-sends once. The first reply is
        // deleted before the retry, so it no longer counts as an occurrence.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        FakeReply* second = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (second = findReply(replies, QStringLiteral("/gen/prompt_image/"))) != nullptr, 15000);
        second->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.image().isEmpty());
    }

    void successfulLoadFillsEveryField()
    {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoCoverArtService service(&client);
        QSignalSpy changed(&service, &SunoCoverArtService::imageChanged);

        service.generate(QStringLiteral("a red door"));
        FakeReply* reply = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            (reply = findReply(replies, QStringLiteral("/gen/prompt_image/"))) != nullptr, 15000);
        reply->respond(imageBody(), 200);

        QTRY_VERIFY_WITH_TIMEOUT(!service.image().isEmpty(), 15000);
        QCOMPARE(service.image().imageS3Id,
                 QStringLiteral("image_7d1f0f9e-1c2b-4a3d-9f10-2b3c4d5e6f70"));
        QVERIFY(!service.image().imageUrl.isEmpty());
        QVERIFY(!service.image().uploadId.isEmpty());
        QCOMPARE(service.failureReason(), QString());
        QCOMPARE(changed.count(), 2);
        QVERIFY(!service.isLoading());
    }
};

QTEST_MAIN(TestSunoCoverArtService)
#include "test_SunoCoverArtService.moc"
