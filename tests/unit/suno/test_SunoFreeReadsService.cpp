#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoFreeReadsService.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
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

class TestSunoFreeReadsService : public QObject
{
    Q_OBJECT

private slots:
    void noClientReportsSignInAndStaysEmpty()
    {
        SunoFreeReadsService service(nullptr);
        QSignalSpy failed(&service, &SunoFreeReadsService::failed);

        service.fetchClip(QStringLiteral("clip-1"));
        service.fetchPinnedClips(QStringLiteral("project-1"));
        service.fetchCoverArtSuggestions();
        service.fetchRealtimeToken({QStringLiteral("user:1")});
        service.fetchChallengeProgress();

        QCOMPARE(failed.count(), 5);
        QVERIFY(service.state().clip.isEmpty());
        QVERIFY(service.state().pinnedClips.isNull());
        QVERIFY(service.state().coverArtSuggestions.isEmpty());
        QVERIFY(service.state().realtimeToken.isEmpty());
        QVERIFY(service.state().challengeProgress.isEmpty());
        QVERIFY(!service.isLoading());
    }

    void clipSuccessKeepsRawObject()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchClip(QStringLiteral("clip-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/clip/clip-1")));

        rig.replies.front()->respond(
                compact({{QStringLiteral("id"), QStringLiteral("clip-1")},
                         {QStringLiteral("title"), QStringLiteral("Song")}}),
                200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.state().clip.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Song"));
        QVERIFY(service.state().failureReason.isEmpty());
        QVERIFY(!service.isLoading());
    }

    void clipHttpErrorDegradesToEmptyWithAReason()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy failed(&service, &SunoFreeReadsService::failed);

        service.fetchClip(QStringLiteral("clip-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        // 5xx is retried once: the second reply is the one the service sees.
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().failureReason.contains(QStringLiteral("500")));
        QVERIFY(service.state().clip.isEmpty());
    }

    void clipStaleReplyIsDropped()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchClip(QStringLiteral("clip-old"));
        service.fetchClip(QStringLiteral("clip-new"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("id"), QStringLiteral("clip-old")}}),
                                   200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("id"), QStringLiteral("clip-new")}}),
                                   200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().clip.value(QStringLiteral("id")).toString(),
                 QStringLiteral("clip-new"));
    }

    void pinnedClipsSuccessPreservesRawRoot()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchPinnedClips(QStringLiteral("project-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QVERIFY(rig.requests.front().url().path().endsWith(
                QStringLiteral("/project/project-1/pinned-clips")));

        rig.replies.front()->respond(compact({{QStringLiteral("pinned"), true}}), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QVERIFY(service.state().pinnedClips.isObject());
        QCOMPARE(service.state().pinnedClips.toObject().value(QStringLiteral("pinned")).toBool(),
                 true);
    }

    void pinnedClipsFailureDegradesToEmpty()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy failed(&service, &SunoFreeReadsService::failed);

        service.fetchPinnedClips(QStringLiteral("project-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().pinnedClips.isNull());
        QVERIFY(service.state().failureReason.contains(QStringLiteral("500")));
    }

    void pinnedClipsStaleReplyIsDropped()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchPinnedClips(QStringLiteral("project-old"));
        service.fetchPinnedClips(QStringLiteral("project-new"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("marker"), 1}}), 200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("marker"), 2}}), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().pinnedClips.toObject().value(QStringLiteral("marker")).toInt(),
                 2);
    }

    void coverArtSuggestionsSuccessParsesItems()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchCoverArtSuggestions();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QVERIFY(rig.requests.front().url().path().endsWith(
                QStringLiteral("/cms/cover-art-suggestions")));

        QJsonArray items;
        items.append(QJsonObject{{QStringLiteral("slug"), QStringLiteral("sunset")},
                                {QStringLiteral("label"), QJsonObject{{QStringLiteral("text"),
                                                                      QStringLiteral("Sunset")}}},
                                {QStringLiteral("prompt"), QJsonObject{}}});
        rig.replies.front()->respond(compact({{QStringLiteral("items"), items}}), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.state().coverArtSuggestions.size(), 1);
        QCOMPARE(service.state().coverArtSuggestions.at(0).slug, QStringLiteral("sunset"));
        QCOMPARE(service.state().coverArtSuggestions.at(0)
                         .label.value(QStringLiteral("text"))
                         .toString(),
                 QStringLiteral("Sunset"));
    }

    void coverArtSuggestionsFailureDegradesToEmpty()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy failed(&service, &SunoFreeReadsService::failed);

        service.fetchCoverArtSuggestions();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().coverArtSuggestions.isEmpty());
    }

    void coverArtSuggestionsStaleReplyIsDropped()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchCoverArtSuggestions();
        service.fetchCoverArtSuggestions();
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        const auto bodyWithSlug = [](const QString& slug) {
            QJsonArray items{QJsonObject{{QStringLiteral("slug"), slug}}};
            return compact({{QStringLiteral("items"), items}});
        };
        rig.replies.at(0)->respond(bodyWithSlug(QStringLiteral("old")), 200);
        rig.replies.at(1)->respond(bodyWithSlug(QStringLiteral("new")), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().coverArtSuggestions.at(0).slug, QStringLiteral("new"));
    }

    void realtimeTokenSuccessPostsCapabilities()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchRealtimeToken({QStringLiteral("user:42")});
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QCOMPARE(rig.methods.front(), std::string("POST"));
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/realtime/token")));
        const QJsonDocument sent = QJsonDocument::fromJson(rig.bodies.front());
        QCOMPARE(sent.object().value(QStringLiteral("capabilities")).toArray().at(0).toObject()
                         .value(QStringLiteral("channel_pattern"))
                         .toString(),
                 QStringLiteral("user:42"));

        rig.replies.front()->respond(compact({{QStringLiteral("token"), QStringLiteral("ably")}}),
                                     200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.state().realtimeToken.value(QStringLiteral("token")).toString(),
                 QStringLiteral("ably"));
    }

    void realtimeTokenFailureDegradesToEmpty()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy failed(&service, &SunoFreeReadsService::failed);

        service.fetchRealtimeToken({QStringLiteral("user:42")});
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().realtimeToken.isEmpty());
    }

    void realtimeTokenStaleReplyIsDropped()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchRealtimeToken({QStringLiteral("user:old")});
        service.fetchRealtimeToken({QStringLiteral("user:new")});
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("token"), QStringLiteral("old")}}),
                                   200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("token"), QStringLiteral("new")}}),
                                   200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().realtimeToken.value(QStringLiteral("token")).toString(),
                 QStringLiteral("new"));
    }

    void challengeProgressSuccessKeepsRawObject()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchChallengeProgress();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        QVERIFY(rig.requests.front().url().path().endsWith(QStringLiteral("/challenge/progress")));

        rig.replies.front()->respond(
                compact({{QStringLiteral("current_level"), 3},
                         {QStringLiteral("earned_credits"), 50}}),
                200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 15000);
        QCOMPARE(service.state().challengeProgress.value(QStringLiteral("current_level")).toInt(),
                 3);
        QCOMPARE(service.state().challengeProgress.value(QStringLiteral("earned_credits")).toInt(),
                 50);
    }

    void challengeProgressFailureDegradesToEmpty()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy failed(&service, &SunoFreeReadsService::failed);

        service.fetchChallengeProgress();
        QTRY_VERIFY_WITH_TIMEOUT(!rig.replies.empty(), 15000);
        rig.replies.front()->respond(compact({}), 500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);
        rig.replies.back()->respond(compact({}), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(service.state().challengeProgress.isEmpty());
    }

    void challengeProgressStaleReplyIsDropped()
    {
        Rig rig;
        SunoFreeReadsService service(&rig.client);
        QSignalSpy changed(&service, &SunoFreeReadsService::changed);

        service.fetchChallengeProgress();
        service.fetchChallengeProgress();
        QTRY_VERIFY_WITH_TIMEOUT(rig.replies.size() >= 2, 15000);

        rig.replies.at(0)->respond(compact({{QStringLiteral("current_level"), 1}}), 200);
        rig.replies.at(1)->respond(compact({{QStringLiteral("current_level"), 2}}), 200);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, 15000);
        QCOMPARE(service.state().challengeProgress.value(QStringLiteral("current_level")).toInt(),
                 2);
    }
};

QTEST_MAIN(TestSunoFreeReadsService)
#include "test_SunoFreeReadsService.moc"
