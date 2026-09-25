#include <QtTest>

#include "suno/SunoAudioUploadService.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

using namespace vc::suno;

class TestSunoAudioUploadService : public QObject
{
    Q_OBJECT

private slots:
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
        const QJsonObject payload{
            {QStringLiteral("id"), QStringLiteral("upload-123")},
            {QStringLiteral("url"), QStringLiteral("https://storage.example.test/upload?signature=opaque")},
            {QStringLiteral("is_file_uploaded"), false},
            {QStringLiteral("fields"), QJsonObject{
                 {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
                 {QStringLiteral("Content-Type"), QStringLiteral("audio/mp4")},
                 {QStringLiteral("key"), QStringLiteral("key-123")},
                 {QStringLiteral("policy"), QStringLiteral("policy-123")},
                 {QStringLiteral("signature"), QStringLiteral("signature-123")},
                 {QStringLiteral("extra"), QStringLiteral("retained")},
             }},
        };

        auto parsed = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->id, QStringLiteral("upload-123"));
        QCOMPARE(parsed->url.host(), QStringLiteral("storage.example.test"));
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
                    {QStringLiteral("url"), QStringLiteral("https://storage.example.test/upload")},
                    {QStringLiteral("is_file_uploaded"), false},
                    {QStringLiteral("fields"), QJsonObject{
                         {QStringLiteral("AWSAccessKeyId"), QStringLiteral("access")},
                     }},
                }).toJson(QJsonDocument::Compact));
        QVERIFY(!missingField.has_value());

        const auto invalidUrl = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("upload-123")},
                    {QStringLiteral("url"), QStringLiteral("http://storage.example.test/upload")},
                    {QStringLiteral("is_file_uploaded"), false},
                    {QStringLiteral("fields"), validFields},
                }).toJson(QJsonDocument::Compact));
        QVERIFY(!invalidUrl.has_value());

        const auto nonStringField = SunoAudioUploadService::parseInitializeResponse(
                QJsonDocument(QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("upload-123")},
                    {QStringLiteral("url"), QStringLiteral("https://storage.example.test/upload")},
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

    void directRequestHasNoBearerAndNoRedirect()
    {
        const QUrl url(QStringLiteral("https://storage.example.test/upload?signature=opaque"));
        const QNetworkRequest request = SunoAudioUploadService::directRequest(url);
        QVERIFY(request.rawHeader("Authorization").isEmpty());
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
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
