#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoVoiceVerificationService.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSignalSpy>

#include <type_traits>
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

class TestSunoVoiceVerificationService : public QObject
{
    Q_OBJECT

private slots:
    void idWrappersAreNotInterchangeable()
    {
        static_assert(!std::is_convertible_v<UploadId, VerificationId>);
        static_assert(!std::is_convertible_v<VerificationId, UploadId>);
        static_assert(!std::is_convertible_v<QString, UploadId>);
        static_assert(!std::is_convertible_v<QString, VerificationId>);
        static_assert(!std::is_convertible_v<UploadId, QString>);
        static_assert(!std::is_convertible_v<VerificationId, QString>);
        const UploadId upload{QStringLiteral("upload-1")};
        const VerificationId verification{QStringLiteral("verification-1")};
        QCOMPARE(upload.value, QStringLiteral("upload-1"));
        QCOMPARE(verification.value, QStringLiteral("verification-1"));
    }

    void fetchPhraseReadsPhraseText()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy changed(&service, &SunoVoiceVerificationService::changed);

        service.fetchPhrase();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("GET"));
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/voice-verification/phrase/")));

        rig.replies.front()->respond(compact({{QStringLiteral("phrase_id"), QStringLiteral("phrase-1")},
                                               {QStringLiteral("phrase_text"),
                                                QStringLiteral("Singing voices drift softly through a peaceful midnight chorus")}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.phraseState().phraseId, QStringLiteral("phrase-1"));
        QCOMPARE(service.phraseState().phraseText,
                 QStringLiteral("Singing voices drift softly through a peaceful midnight chorus"));
        QCOMPARE(service.phraseState().raw.value(QStringLiteral("phrase_id")).toString(),
                 QStringLiteral("phrase-1"));
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isFetchingPhrase());
    }

    void submitStemSendsUploadIdAndRange()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy changed(&service, &SunoVoiceVerificationService::changed);

        service.submitStem(UploadId{QStringLiteral("upload-1")}, 0.0, 39.48);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("POST"));
        QVERIFY(rig.requests.front().url().path().endsWith(
                QStringLiteral("/processed_clip/voice-vox-stem")));

        const QJsonDocument sent = QJsonDocument::fromJson(rig.bodies.front());
        QCOMPARE(sent.object().size(), 3);
        QCOMPARE(sent.object().value(QStringLiteral("upload_id")).toString(),
                 QStringLiteral("upload-1"));
        QCOMPARE(sent.object().value(QStringLiteral("vocal_start_s")).toDouble(), 0.0);
        QCOMPARE(sent.object().value(QStringLiteral("vocal_end_s")).toDouble(), 39.48);

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-1")},
                                               {QStringLiteral("status"), QStringLiteral("submitted")},
                                               {QStringLiteral("voice_recording_id"),
                                                QStringLiteral("voice-rec-1")}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.stemState().processedClipId, QStringLiteral("pack-1"));
        QCOMPARE(service.stemState().voiceRecordingId, QStringLiteral("voice-rec-1"));
        QCOMPARE(service.stemState().status, SunoVoiceVerificationService::Status::Submitted);
        QCOMPARE(service.stemState().statusText, QStringLiteral("submitted"));
    }

    void submitVerificationSendsThreeIds()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy changed(&service, &SunoVoiceVerificationService::changed);

        service.submitVerification(QStringLiteral("voice-rec-1"), QStringLiteral("verify-rec-1"),
                                   QStringLiteral("phrase-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("POST"));
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/voice-verification/")));

        const QJsonDocument sent = QJsonDocument::fromJson(rig.bodies.front());
        QCOMPARE(sent.object().size(), 3);
        QCOMPARE(sent.object().value(QStringLiteral("voice_recording_id")).toString(),
                 QStringLiteral("voice-rec-1"));
        QCOMPARE(sent.object().value(QStringLiteral("verification_recording_id")).toString(),
                 QStringLiteral("verify-rec-1"));
        QCOMPARE(sent.object().value(QStringLiteral("phrase_id")).toString(),
                 QStringLiteral("phrase-1"));

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("verification-1")},
                                               {QStringLiteral("status"), QStringLiteral("submitted")}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.verificationState().verificationId, QStringLiteral("verification-1"));
        QCOMPARE(service.verificationState().statusText, QStringLiteral("submitted"));
    }

    void pollTransitionsSubmittedToApproved()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy changed(&service, &SunoVoiceVerificationService::changed);

        service.poll(VerificationId{QStringLiteral("verification-1")});
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QVERIFY(rig.requests.front().url().path().endsWith(
                QStringLiteral("/voice-verification/verification-1")));

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("verification-1")},
                                               {QStringLiteral("status"), QStringLiteral("submitted")}}),
                                     200);
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.pollState().status, SunoVoiceVerificationService::Status::Submitted);

        service.poll(VerificationId{QStringLiteral("verification-1")});
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(
                compact({{QStringLiteral("id"), QStringLiteral("verification-1")},
                         {QStringLiteral("status"), QStringLiteral("approved")},
                         {QStringLiteral("verification_prompt_text"),
                          QStringLiteral("Singing voices drift softly through a peaceful midnight chorus")}}),
                200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 4, 15000);
        QCOMPARE(service.pollState().status, SunoVoiceVerificationService::Status::Approved);
        QCOMPARE(service.pollState().verificationPromptText,
                 QStringLiteral("Singing voices drift softly through a peaceful midnight chorus"));
        QVERIFY(service.failureReason().isEmpty());
    }

    void pollUnobservedStatusDegradesToUnknown()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy changed(&service, &SunoVoiceVerificationService::changed);

        service.poll(VerificationId{QStringLiteral("verification-1")});
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("verification-1")},
                                               {QStringLiteral("status"), QStringLiteral("pending-review")},
                                               {QStringLiteral("verification_prompt_text"),
                                                QStringLiteral("prompt")}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.pollState().status, SunoVoiceVerificationService::Status::Unknown);
        QCOMPARE(service.pollState().statusText, QStringLiteral("pending-review"));
        QCOMPARE(service.pollState().verificationPromptText, QStringLiteral("prompt"));
        QCOMPARE(service.pollState().raw.value(QStringLiteral("status")).toString(),
                 QStringLiteral("pending-review"));
    }

    void pollStaleReplyIsDropped()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy changed(&service, &SunoVoiceVerificationService::changed);

        service.poll(VerificationId{QStringLiteral("verification-old")});
        service.poll(VerificationId{QStringLiteral("verification-new")});
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("id"), QStringLiteral("verification-old")},
                                              {QStringLiteral("status"), QStringLiteral("approved")}}),
                                    200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("id"), QStringLiteral("verification-new")},
                                              {QStringLiteral("status"), QStringLiteral("submitted")}}),
                                    200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.pollState().statusText, QStringLiteral("submitted"));
    }

    void stemFailureEmptiesOnlyStemSection()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy failed(&service, &SunoVoiceVerificationService::failed);

        // Fill the phrase section first.
        service.fetchPhrase();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({{QStringLiteral("phrase_id"), QStringLiteral("phrase-1")},
                                               {QStringLiteral("phrase_text"), QStringLiteral("line")}}),
                                     200);
        QTRY_VERIFY_WITH_TIMEOUT(service.phraseState().phraseId == QStringLiteral("phrase-1"), 15000);

        // Mutating send: 5xx settles alone, no retry reply.
        service.submitStem(UploadId{QStringLiteral("upload-1")}, 1.0, 2.0);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.stemState().processedClipId.isEmpty());
        QVERIFY(service.stemState().raw.isEmpty());
        // Phrase section untouched.
        QCOMPARE(service.phraseState().phraseId, QStringLiteral("phrase-1"));
        QVERIFY(!service.isSubmittingStem());
    }

    void phraseFailureRetriesOnceOn5xx()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy failed(&service, &SunoVoiceVerificationService::failed);

        service.fetchPhrase();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.phraseState().phraseId.isEmpty());
        QVERIFY(!service.isFetchingPhrase());
    }

    void invalidRangeRefusedBeforeAnyRequest()
    {
        Rig rig;
        SunoVoiceVerificationService service(&rig.client);
        QSignalSpy failed(&service, &SunoVoiceVerificationService::failed);

        service.submitStem(UploadId{QStringLiteral("upload-1")}, 5.0, 5.0);
        service.submitStem(UploadId{QStringLiteral("upload-1")}, -1.0, 5.0);
        service.submitStem(UploadId{QStringLiteral("")}, 1.0, 2.0);

        QCOMPARE(failed.count(), 3);
        QVERIFY(rig.replies.empty());
        QVERIFY(rig.requests.empty());
        QVERIFY(!service.isSubmittingStem());
    }
};

QTEST_MAIN(TestSunoVoiceVerificationService)
#include "test_SunoVoiceVerificationService.moc"
