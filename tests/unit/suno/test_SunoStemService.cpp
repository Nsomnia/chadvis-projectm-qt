#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoStemService.hpp"
#include "FakeNetworkReply.hpp"

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

class TestSunoStemService : public QObject
{
    Q_OBJECT

private slots:
    void submitSuccessReturnsProcessedClipId()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy changed(&service, &SunoStemService::changed);

        service.submit(QStringLiteral("clip-1"), 45.0, 74.0);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("POST"));
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/clip/clip-1/vox-stem")));

        // Body exactly the captured shape: two keys, no extras.
        const QJsonDocument sent = QJsonDocument::fromJson(rig.bodies.front());
        QCOMPARE(sent.object().size(), 2);
        QCOMPARE(sent.object().value(QStringLiteral("vocal_start_s")).toDouble(), 45.0);
        QCOMPARE(sent.object().value(QStringLiteral("vocal_end_s")).toDouble(), 74.0);

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-1")},
                                               {QStringLiteral("status"), QStringLiteral("submitted")},
                                               {QStringLiteral("source_clip_id"), QStringLiteral("clip-1")},
                                               {QStringLiteral("vocal_start_s"), 45},
                                               {QStringLiteral("vocal_end_s"), 74}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.submitState().processedClipId, QStringLiteral("pack-1"));
        QCOMPARE(service.submitState().status, SunoStemService::Status::Submitted);
        QCOMPARE(service.submitState().statusText, QStringLiteral("submitted"));
        QCOMPARE(service.submitState().raw.value(QStringLiteral("source_clip_id")).toString(),
                 QStringLiteral("clip-1"));
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isSubmitting());
    }

    void submitFailureEmptiesSectionWithAReason()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy failed(&service, &SunoStemService::failed);

        service.submit(QStringLiteral("clip-1"), 10.0, 20.0);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        // 5xx is retried once: the second reply is the one the service sees.
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.submitState().processedClipId.isEmpty());
        QVERIFY(service.submitState().raw.isEmpty());
        QVERIFY(!service.isSubmitting());
    }

    void pollSubmittedKeepsNoUrl()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy changed(&service, &SunoStemService::changed);

        service.poll(QStringLiteral("pack-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/processed_clip/pack-1")));

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-1")},
                                               {QStringLiteral("status"), QStringLiteral("submitted")},
                                               {QStringLiteral("source_clip_id"), QStringLiteral("clip-1")},
                                               {QStringLiteral("vocal_start_s"), 45},
                                               {QStringLiteral("vocal_end_s"), 74}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.pollState().status, SunoStemService::Status::Submitted);
        QVERIFY(service.pollState().vocalAudioUrl.isEmpty());
        QVERIFY(!service.isPolling());
    }

    void pollCompleteCarriesVocalAudioUrl()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy changed(&service, &SunoStemService::changed);

        service.poll(QStringLiteral("pack-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);

        rig.replies.front()->respond(
                compact({{QStringLiteral("id"), QStringLiteral("pack-1")},
                         {QStringLiteral("status"), QStringLiteral("complete")},
                         {QStringLiteral("source_clip_id"), QStringLiteral("clip-1")},
                         {QStringLiteral("vocal_start_s"), 45},
                         {QStringLiteral("vocal_end_s"), 74},
                         {QStringLiteral("vocal_audio_url"),
                          QStringLiteral("https://cdn.example/vocals.m4a")}}),
                200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.pollState().status, SunoStemService::Status::Complete);
        QCOMPARE(service.pollState().vocalAudioUrl, QStringLiteral("https://cdn.example/vocals.m4a"));
        QVERIFY(service.failureReason().isEmpty());
    }

    void pollUnobservedStatusDegradesToUnknown()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy changed(&service, &SunoStemService::changed);

        service.poll(QStringLiteral("pack-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);

        rig.replies.front()->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-1")},
                                               {QStringLiteral("status"), QStringLiteral("frobnicating")},
                                               {QStringLiteral("vocal_audio_url"), QStringLiteral("https://cdn.example/v.m4a")}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.pollState().status, SunoStemService::Status::Unknown);
        // Not a guessed terminal: text and url stay readable on the raw state.
        QCOMPARE(service.pollState().statusText, QStringLiteral("frobnicating"));
        QCOMPARE(service.pollState().vocalAudioUrl, QStringLiteral("https://cdn.example/v.m4a"));
        QCOMPARE(service.pollState().raw.value(QStringLiteral("status")).toString(),
                 QStringLiteral("frobnicating"));
    }

    void pollStaleReplyIsDropped()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy changed(&service, &SunoStemService::changed);

        service.poll(QStringLiteral("pack-old"));
        service.poll(QStringLiteral("pack-new"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-old")},
                                              {QStringLiteral("status"), QStringLiteral("complete")}}),
                                    200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-new")},
                                              {QStringLiteral("status"), QStringLiteral("submitted")}}),
                                    200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.pollState().statusText, QStringLiteral("submitted"));
    }

    void submitStaleReplyIsDropped()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy changed(&service, &SunoStemService::changed);

        service.submit(QStringLiteral("clip-old"), 1.0, 2.0);
        service.submit(QStringLiteral("clip-new"), 3.0, 4.0);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-old")},
                                              {QStringLiteral("status"), QStringLiteral("submitted")}}),
                                    200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("id"), QStringLiteral("pack-new")},
                                              {QStringLiteral("status"), QStringLiteral("submitted")}}),
                                    200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.submitState().processedClipId, QStringLiteral("pack-new"));
    }

    void invalidRangeRefusedBeforeAnyRequest()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy failed(&service, &SunoStemService::failed);

        service.submit(QStringLiteral("clip-1"), 74.0, 45.0); // start >= end
        service.submit(QStringLiteral("clip-1"), -1.0, 45.0); // negative start
        service.submit(QStringLiteral("clip-1"), 45.0, 45.0); // empty range
        service.submit(QStringLiteral("clip-1"), 45.0, -1.0); // negative end

        QCOMPARE(failed.count(), 4);
        QVERIFY(rig.replies.empty());
        QVERIFY(rig.requests.empty());
        QVERIFY(!service.isSubmitting());
    }

    void pollFailureEmptiesOnlyPollSection()
    {
        Rig rig;
        SunoStemService service(&rig.client);
        QSignalSpy failed(&service, &SunoStemService::failed);

        service.poll(QStringLiteral("pack-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({{QStringLiteral("status"), QStringLiteral("complete")},
                                               {QStringLiteral("vocal_audio_url"), QStringLiteral("https://cdn.example/v.m4a")}}),
                                     200);
        QTRY_VERIFY_WITH_TIMEOUT(service.pollState().status == SunoStemService::Status::Complete, 15000);

        service.poll(QStringLiteral("pack-1"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 404);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.failureReason().contains(QStringLiteral("404")));
        QVERIFY(service.pollState().raw.isEmpty());
        QVERIFY(service.pollState().vocalAudioUrl.isEmpty());
    }

    void noClientReportsSignInAndStaysEmpty()
    {
        SunoStemService service(nullptr);
        QSignalSpy failed(&service, &SunoStemService::failed);

        service.submit(QStringLiteral("clip-1"), 1.0, 2.0);
        service.poll(QStringLiteral("pack-1"));

        QCOMPARE(failed.count(), 2);
        QVERIFY(service.submitState().processedClipId.isEmpty());
        QVERIFY(service.pollState().raw.isEmpty());
        QVERIFY(!service.isSubmitting());
        QVERIFY(!service.isPolling());
    }
};

QTEST_MAIN(TestSunoStemService)
#include "test_SunoStemService.moc"
