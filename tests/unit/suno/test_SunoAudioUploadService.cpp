#include <QtTest>

#include "suno/SunoAudioUploadService.hpp"
#include "suno/SunoClient.hpp"
#include "FakeNetworkReply.hpp"

#include <QFile>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTemporaryDir>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {

QJsonObject initializerPayload(const QString& url)
{
    return {
        {QStringLiteral("id"), QStringLiteral("upload-123")},
        {QStringLiteral("url"), url},
        {QStringLiteral("is_file_uploaded"), false},
        {QStringLiteral("fields"), QJsonObject{
             {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
             {QStringLiteral("Content-Type"), QStringLiteral("audio/mp4")},
             {QStringLiteral("key"), QStringLiteral("key-123")},
             {QStringLiteral("policy"), QStringLiteral("policy-123")},
             {QStringLiteral("signature"), QStringLiteral("signature-123")},
         }},
    };
}

QJsonObject initializeClipPayload(bool available)
{
    return {
        {QStringLiteral("clip_id"), QStringLiteral("clip-456")},
        {QStringLiteral("rights_clearance_available"), available},
    };
}

static QString fakeBearer(const QString& marker)
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(QJsonDocument(object)
                                      .toJson(QJsonDocument::Compact)
                                      .toBase64(QByteArray::Base64UrlEncoding |
                                                 QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)},
                                    {QStringLiteral("test"), marker}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

static CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

QString writeAudioFile(const QTemporaryDir& dir, const QString& name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    Q_ASSERT(file.open(QIODevice::WriteOnly));
    Q_ASSERT(file.write(QByteArrayLiteral("audio")) == 5);
    file.close();
    return path;
}

/// The SunoClient ReplyFactory seam: records every request, body and reply so
/// a chain test can route each leg of the upload in order.
SunoClient::ReplyFactory recordingFactory(std::vector<FakeReply*>& replies,
                                          std::vector<QNetworkRequest>& requests,
                                          std::vector<QByteArray>& bodies)
{
    return [&replies, &requests, &bodies](const QNetworkRequest& request,
                                          const std::string& method,
                                          const QByteArray& body) -> QNetworkReply* {
        requests.push_back(request);
        bodies.push_back(body);
        auto* reply = new FakeReply(request, method);
        replies.push_back(reply);
        return reply;
    };
}

/// Drives a started upload through reserve and the direct S3 POST, so the next
/// studio reply is the `initialize-clip` one. Bodies of studio requests are
/// recorded alongside their replies. `path` is a real audio file, given so the
/// temporary dir that owns it outlives this call.
void driveToInitializeClip(const QString& path,
                           SunoAudioUploadService& service,
                           std::vector<FakeReply*>& studioReplies,
                           std::vector<FakeReply*>& directReplies)
{
    service.start(path);
    QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 1, 4000);
    studioReplies.front()->complete(
        QJsonDocument(initializerPayload(
            QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload?signature=opaque")))
            .toJson(QJsonDocument::Compact),
        200);
    QTRY_VERIFY_WITH_TIMEOUT(directReplies.size() == 1, 4000);
    directReplies.front()->complete(QByteArray{}, 204);
}

}

class TestSunoAudioUploadService : public QObject
{
    Q_OBJECT

private slots:
    void oversizedFileIsRefusedBeforeAnyTicketIsRequested()
    {
        // T0035: the bound that did not exist. `start()` validated size() > 0
        // only, so a 2 GB file was POSTed to S3 with no way to stop it.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("huge.m4a"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        // Sparse: seek past the cap and write one byte, so the test costs no
        // disk while still reporting a real size.
        QVERIFY(file.seek(SunoAudioUploadService::kMaxUploadBytes));
        QCOMPARE(file.write("x"), qint64(1));
        file.close();

        SunoAudioUploadService service(nullptr, nullptr);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);
        service.start(path);

        QCOMPARE(failed.count(), 1);
        QVERIFY(service.error().contains(QStringLiteral("too large")));
        QVERIFY(!service.isUploading());
        // Nothing was uploaded, so no progress was ever reported.
        QCOMPARE(service.progress(), 0);
    }

    void aFileUnderTheCapIsNotRefusedForItsSize()
    {
        // The complement of the above: the cap must not reject ordinary files.
        // File validation now precedes the auth check (T0035), so a valid file
        // with no client fails with the SIGN-IN error rather than a size error.
        // Asserting on which error it is -- not merely that it failed -- is what
        // makes this a test of the cap rather than of the error path.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("small.m4a"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("tiny"), qint64(4));
        file.close();

        SunoAudioUploadService service(nullptr, nullptr);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);
        service.start(path);

        QCOMPARE(failed.count(), 1);
        QVERIFY(!service.error().contains(QStringLiteral("too large")));
        QCOMPARE(service.error(), QStringLiteral("Sign in to Suno before uploading audio."));
    }

    void cancelIsIdempotentAndSilentWhenNotUploading()
    {
        // T0035: cancel() is the new user-facing abort. Cancelling an idle
        // service must emit nothing -- a "cancelled" failure on an upload that
        // was never running would be a lie a UI could not distinguish from a
        // real one.
        SunoAudioUploadService service(nullptr, nullptr);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);

        service.cancel();
        service.cancel();

        QCOMPARE(failed.count(), 0);
        QVERIFY(!service.isUploading());
    }

    void initializeContractIsExact()
    {
        const QJsonObject body = SunoAudioUploadService::initializeBody();
        QCOMPARE(body.size(), 2);
        QCOMPARE(body.value(QStringLiteral("extension")).toString(),
                 QStringLiteral("m4a"));
        QCOMPARE(body.value(QStringLiteral("upload_type")).toString(),
                 QStringLiteral("file_upload"));
    }

    void finishContractUsesLocalBasename()
    {
        const QJsonObject body = SunoAudioUploadService::finishBody(QStringLiteral("take.m4a"));
        QCOMPARE(body.size(), 3);
        QCOMPARE(body.value(QStringLiteral("agreed_to_vip_upload_terms")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("upload_filename")).toString(),
                 QStringLiteral("take.m4a"));
        QCOMPARE(body.value(QStringLiteral("upload_type")).toString(),
                 QStringLiteral("file_upload"));
    }

    void parsesCapturedInitializerAndKeepsAllReturnedFields()
    {
        QJsonObject payload = initializerPayload(
                QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload?signature=opaque"));
        payload[QStringLiteral("fields")] = QJsonObject{
            {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
            {QStringLiteral("Content-Type"), QStringLiteral("audio/mp4")},
            {QStringLiteral("key"), QStringLiteral("key-123")},
            {QStringLiteral("policy"), QStringLiteral("policy-123")},
            {QStringLiteral("signature"), QStringLiteral("signature-123")},
            {QStringLiteral("extra"), QStringLiteral("retained")},
        };

        auto parsed = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->id, QStringLiteral("upload-123"));
        QCOMPARE(parsed->url.host(), QStringLiteral("suno-uploads.s3.amazonaws.com"));
        QVERIFY(!parsed->isFileUploaded);
        QCOMPARE(parsed->fields.size(), 6);
        QCOMPARE(parsed->fields.value(QStringLiteral("signature")),
                 QStringLiteral("signature-123"));
        QCOMPARE(parsed->fields.value(QStringLiteral("extra")),
                 QStringLiteral("retained"));
    }

    void rejectsMalformedInitializers()
    {
        const QJsonObject validFields{
            {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
            {QStringLiteral("Content-Type"), QStringLiteral("audio/mp4")},
            {QStringLiteral("key"), QStringLiteral("key-123")},
            {QStringLiteral("policy"), QStringLiteral("policy-123")},
            {QStringLiteral("signature"), QStringLiteral("signature-123")},
        };

        const auto missingField = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("upload-123")},
                    {QStringLiteral("url"), QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload")},
                    {QStringLiteral("is_file_uploaded"), false},
                    {QStringLiteral("fields"), QJsonObject{
                         {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
                     }},
                }).toJson(QJsonDocument::Compact));
        QVERIFY(!missingField.has_value());

        const auto invalidUrl = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("upload-123")},
                    {QStringLiteral("url"), QStringLiteral("http://suno-uploads.s3.amazonaws.com/upload")},
                    {QStringLiteral("is_file_uploaded"), false},
                    {QStringLiteral("fields"), validFields},
                }).toJson(QJsonDocument::Compact));
        QVERIFY(!invalidUrl.has_value());

        const auto nonStringField = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("upload-123")},
                    {QStringLiteral("url"), QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload")},
                    {QStringLiteral("is_file_uploaded"), false},
                    {QStringLiteral("fields"), QJsonObject{
                         {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
                         {QStringLiteral("Content-Type"), QStringLiteral("audio/mp4")},
                         {QStringLiteral("key"), QStringLiteral("key-123")},
                         {QStringLiteral("policy"), QStringLiteral("policy-123")},
                         {QStringLiteral("signature"), 1},
                     }},
                }).toJson(QJsonDocument::Compact));
        QVERIFY(!nonStringField.has_value());
    }

    void temporaryUrlPolicy_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");

        QTest::newRow("captured-host")
                << QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload?signature=opaque")
                << true;
        QTest::newRow("explicit-https-port")
                << QStringLiteral("https://suno-uploads.s3.amazonaws.com:443/upload")
                << true;
        QTest::newRow("cleartext")
                << QStringLiteral("http://suno-uploads.s3.amazonaws.com/upload")
                << false;
        QTest::newRow("alternate-host")
                << QStringLiteral("https://storage.example.test/upload")
                << false;
        QTest::newRow("suffix-confusion")
                << QStringLiteral("https://suno-uploads.s3.amazonaws.com.evil.test/upload")
                << false;
        QTest::newRow("userinfo")
                << QStringLiteral("https://user@suno-uploads.s3.amazonaws.com/upload")
                << false;
        QTest::newRow("nondefault-port")
                << QStringLiteral("https://suno-uploads.s3.amazonaws.com:444/upload")
                << false;
        QTest::newRow("fragment")
                << QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload#fragment")
                << false;
        QTest::newRow("empty-path")
                << QStringLiteral("https://suno-uploads.s3.amazonaws.com")
                << false;
        QTest::newRow("relative")
                << QStringLiteral("/upload")
                << false;
        QTest::newRow("file-scheme")
                << QStringLiteral("file:///tmp/upload")
                << false;
    }

    void temporaryUrlPolicy()
    {
        QFETCH(QString, url);
        QFETCH(bool, allowed);

        const QUrl parsed(url);
        QCOMPARE(SunoAudioUploadService::isSupportedTemporaryUrl(parsed), allowed);
        QCOMPARE(SunoAudioUploadService::directRequest(parsed).has_value(), allowed);
        const auto initialized = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(initializerPayload(url)).toJson(QJsonDocument::Compact));
        QCOMPARE(initialized.has_value(), allowed);
    }

    void directRequestHasNoBearerAndNoRedirect()
    {
        const QUrl url(QStringLiteral("https://suno-uploads.s3.amazonaws.com/upload?signature=opaque"));
        const auto request = SunoAudioUploadService::directRequest(url);
        QVERIFY(request.has_value());
        QVERIFY(request->rawHeader("Authorization").isEmpty());
        QCOMPARE(request->attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
    }

    void acceptsOnlyCapturedExtension()
    {
        QVERIFY(SunoAudioUploadService::isSupportedAudioPath(QStringLiteral("/tmp/song.M4A")));
        QVERIFY(!SunoAudioUploadService::isSupportedAudioPath(QStringLiteral("/tmp/song.MP3")));
        QVERIFY(!SunoAudioUploadService::isSupportedAudioPath(QStringLiteral("/tmp/song.txt")));
        QVERIFY(!SunoAudioUploadService::isSupportedAudioPath(QStringLiteral("/tmp/song")));
    }

    void initializeClipContractIsExact()
    {
        const QJsonObject body = SunoAudioUploadService::initializeClipBody();
        QCOMPARE(body.size(), 1);
        QCOMPARE(body.value(QStringLiteral("user_reviewed_tags")).toBool(), true);
    }

    void parsesInitializeClipResponses()
    {
        auto available = SunoAudioUploadService::parseInitializeClipResponse(
            QJsonDocument(initializeClipPayload(true)).toJson(QJsonDocument::Compact));
        QVERIFY(available.has_value());
        QCOMPARE(available->clipId, QStringLiteral("clip-456"));
        QVERIFY(available->rightsClearanceAvailable);
        QCOMPARE(available->raw.value(QStringLiteral("clip_id")).toString(),
                 QStringLiteral("clip-456"));
        QCOMPARE(available->raw.value(QStringLiteral("rights_clearance_available")).toBool(),
                 true);

        auto unavailable = SunoAudioUploadService::parseInitializeClipResponse(
            QJsonDocument(initializeClipPayload(false)).toJson(QJsonDocument::Compact));
        QVERIFY(unavailable.has_value());
        QVERIFY(!unavailable->rightsClearanceAvailable);
    }

    void rejectsMalformedInitializeClipResponses()
    {
        const auto missingRights = SunoAudioUploadService::parseInitializeClipResponse(
            QJsonDocument(QJsonObject{
                {QStringLiteral("clip_id"), QStringLiteral("clip-456")},
            })
                .toJson(QJsonDocument::Compact));
        QVERIFY(!missingRights.has_value());

        const auto emptyClip = SunoAudioUploadService::parseInitializeClipResponse(
            QJsonDocument(QJsonObject{
                {QStringLiteral("clip_id"), QStringLiteral("")},
                {QStringLiteral("rights_clearance_available"), true},
            })
                .toJson(QJsonDocument::Compact));
        QVERIFY(!emptyClip.has_value());

        const auto nonBoolRights = SunoAudioUploadService::parseInitializeClipResponse(
            QJsonDocument(QJsonObject{
                {QStringLiteral("clip_id"), QStringLiteral("clip-456")},
                {QStringLiteral("rights_clearance_available"), QStringLiteral("true")},
            })
                .toJson(QJsonDocument::Compact));
        QVERIFY(!nonBoolRights.has_value());

        const auto notJson = SunoAudioUploadService::parseInitializeClipResponse(
            QByteArrayLiteral("nope"));
        QVERIFY(!notJson.has_value());
    }

    void initializeClipSuccessProceedsToFinish()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeAudioFile(directory, QStringLiteral("take.m4a"));

        std::vector<FakeReply*> studioReplies;
        std::vector<QNetworkRequest> studioRequests;
        std::vector<QByteArray> studioBodies;
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(),
                          recordingFactory(studioReplies, studioRequests, studioBodies));
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        std::vector<FakeReply*> directReplies;
        auto directFactory = [&directReplies](const QNetworkRequest& request,
                                               QHttpMultiPart*) {
            auto* reply = new FakeReply(request, "POST");
            directReplies.push_back(reply);
            return reply;
        };
        SunoAudioUploadService service(&client, nullptr, nullptr, directFactory);
        QSignalSpy completed(&service, &SunoAudioUploadService::completed);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);

        driveToInitializeClip(path, service, studioReplies, directReplies);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 2, 4000);
        QVERIFY(studioRequests.at(1).url().path().endsWith(
            QStringLiteral("/uploads/audio/upload-123/initialize-clip/")));
        QCOMPARE(studioBodies.at(1),
                 QJsonDocument(SunoAudioUploadService::initializeClipBody())
                     .toJson(QJsonDocument::Compact));

        studioReplies.at(1)->complete(
            QJsonDocument(initializeClipPayload(true)).toJson(QJsonDocument::Compact), 200);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 3, 4000);
        QVERIFY(studioRequests.at(2).url().path().endsWith(
            QStringLiteral("/uploads/audio/upload-123/upload-finish/")));

        studioReplies.at(2)->complete(QByteArrayLiteral("{}"), 200);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 4000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(service.clipId(), QStringLiteral("clip-456"));
        QVERIFY(!service.isUploading());
    }

    void initializeClipWithoutRightsClearanceFailsWithoutExplicitDecision()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeAudioFile(directory, QStringLiteral("take.m4a"));

        std::vector<FakeReply*> studioReplies;
        std::vector<QNetworkRequest> studioRequests;
        std::vector<QByteArray> studioBodies;
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(),
                          recordingFactory(studioReplies, studioRequests, studioBodies));
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        std::vector<FakeReply*> directReplies;
        auto directFactory = [&directReplies](const QNetworkRequest& request,
                                               QHttpMultiPart*) {
            auto* reply = new FakeReply(request, "POST");
            directReplies.push_back(reply);
            return reply;
        };
        SunoAudioUploadService service(&client, nullptr, nullptr, directFactory);
        QSignalSpy completed(&service, &SunoAudioUploadService::completed);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);
        // No setProceedWithoutRightsClearance(true): the default is the safe
        // answer and must fail the upload rather than auto-accept it.

        driveToInitializeClip(path, service, studioReplies, directReplies);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 2, 4000);

        studioReplies.at(1)->complete(
            QJsonDocument(initializeClipPayload(false)).toJson(QJsonDocument::Compact), 200);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 4000);
        QVERIFY(service.error().contains(QStringLiteral("Rights clearance")));
        // The chain stops here: no finish request, no completion.
        QTRY_VERIFY_WITH_TIMEOUT(!service.isUploading(), 1000);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(studioReplies.size(), std::size_t{2});
    }

    void initializeClipWithoutRightsClearanceProceedsWithExplicitDecision()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeAudioFile(directory, QStringLiteral("take.m4a"));

        std::vector<FakeReply*> studioReplies;
        std::vector<QNetworkRequest> studioRequests;
        std::vector<QByteArray> studioBodies;
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(),
                          recordingFactory(studioReplies, studioRequests, studioBodies));
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        std::vector<FakeReply*> directReplies;
        auto directFactory = [&directReplies](const QNetworkRequest& request,
                                               QHttpMultiPart*) {
            auto* reply = new FakeReply(request, "POST");
            directReplies.push_back(reply);
            return reply;
        };
        SunoAudioUploadService service(&client, nullptr, nullptr, directFactory);
        service.setProceedWithoutRightsClearance(true);
        QSignalSpy completed(&service, &SunoAudioUploadService::completed);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);

        driveToInitializeClip(path, service, studioReplies, directReplies);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 2, 4000);

        studioReplies.at(1)->complete(
            QJsonDocument(initializeClipPayload(false)).toJson(QJsonDocument::Compact), 200);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 3, 4000);
        studioReplies.at(2)->complete(QByteArrayLiteral("{}"), 200);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 4000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(service.clipId(), QStringLiteral("clip-456"));
    }

    void initializeClipFailureStopsBeforeFinish()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeAudioFile(directory, QStringLiteral("take.m4a"));

        std::vector<FakeReply*> studioReplies;
        std::vector<QNetworkRequest> studioRequests;
        std::vector<QByteArray> studioBodies;
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(),
                          recordingFactory(studioReplies, studioRequests, studioBodies));
        QSignalSpy restored(&client, &SunoClient::credentialRestoreCompleted);
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 2000);
        client.setToken(fakeBearer(QStringLiteral("one")).toStdString());

        std::vector<FakeReply*> directReplies;
        auto directFactory = [&directReplies](const QNetworkRequest& request,
                                               QHttpMultiPart*) {
            auto* reply = new FakeReply(request, "POST");
            directReplies.push_back(reply);
            return reply;
        };
        SunoAudioUploadService service(&client, nullptr, nullptr, directFactory);
        QSignalSpy failed(&service, &SunoAudioUploadService::failed);

        driveToInitializeClip(path, service, studioReplies, directReplies);
        QTRY_VERIFY_WITH_TIMEOUT(studioReplies.size() == 2, 4000);

        // 400, not 5xx: this chain passes retryOnUnauthorized=false, so no
        // SunoClient retry is in play and a single reply settles it.
        studioReplies.at(1)->complete(QByteArrayLiteral("{}"), 400);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 4000);
        QVERIFY(service.error().contains(QStringLiteral("clip creation failed")));
        QCOMPARE(studioReplies.size(), std::size_t{2});
    }
};

#include "test_SunoAudioUploadService.moc"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestSunoAudioUploadService test;
    return QTest::qExec(&test, argc, argv);
}
