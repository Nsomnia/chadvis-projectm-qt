#include <QtTest>

#include "suno/SunoAudioUploadService.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTemporaryDir>

using namespace vc::suno;

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
};

#include "test_SunoAudioUploadService.moc"

int main(int argc, char** argv)
{
    TestSunoAudioUploadService test;
    return QTest::qExec(&test, argc, argv);
}
