#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoHooksService.hpp"
#include "FakeNetworkReply.hpp"

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
using fake_net::FakeReply;

namespace {


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

// No list key is pinned for the Hooks family (§4.12): these fixtures use
// plausible keys only to exercise the tolerant parser. The extra
// `unknown_future_field` is deliberate: entries stay raw.

QByteArray userHooksBody()
{
    return QByteArrayLiteral(R"({
        "hooks": [
            {
                "id": "11111111-2222-3333-4444-555555555555",
                "title": "Hook One",
                "status": "complete",
                "unknown_future_field": {"nested": [1, 2]}
            },
            {
                "id": "66666666-7777-8888-9999-000000000000",
                "title": "Hook Two",
                "status": "complete"
            }
        ]
    })");
}

QByteArray likedBody()
{
    return QByteArrayLiteral(R"({
        "clips": [
            {
                "id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
                "title": "Liked One",
                "status": "complete"
            }
        ]
    })");
}

QByteArray suggestedBody()
{
    return QByteArrayLiteral(R"({
        "suggested_clips": [
            {
                "id": "ffffffff-1111-2222-3333-444444444444",
                "title": "Suggested One",
                "status": "complete"
            },
            {
                "id": "00000000-1111-2222-3333-444444444444",
                "title": "Suggested Two",
                "status": "complete"
            },
            {
                "id": "99999999-1111-2222-3333-444444444444",
                "title": "Suggested Three",
                "status": "complete"
            }
        ]
    })");
}

FakeReply* findReply(const std::vector<QPointer<FakeReply>>& replies,
                         const QString& suffix, int occurrence = 0)
{
    int seen = 0;
    for (QPointer<FakeReply> reply : replies) {
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

class TestSunoHooksService : public QObject
{
    Q_OBJECT

private slots:
    void userHooksAreKeptAsTheServerSentThem()
    {
        const QJsonDocument document = QJsonDocument::fromJson(userHooksBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoHooksService::parseUserHooks(document.object());
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed[0].value(QStringLiteral("title")).toString(),
                 QStringLiteral("Hook One"));
        QVERIFY(parsed[0].contains(QStringLiteral("unknown_future_field")));
        QCOMPARE(parsed[1].value(QStringLiteral("id")).toString(),
                 QStringLiteral("66666666-7777-8888-9999-000000000000"));
    }

    void likedClipsAreKeptAsTheServerSentThem()
    {
        const QJsonDocument document = QJsonDocument::fromJson(likedBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoHooksService::parseLiked(document.object());
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed[0].value(QStringLiteral("title")).toString(),
                 QStringLiteral("Liked One"));
    }

    void suggestedClipsAreKeptAsTheServerSentThem()
    {
        const QJsonDocument document = QJsonDocument::fromJson(suggestedBody());
        QVERIFY(document.isObject());

        const auto parsed = SunoHooksService::parseSuggested(document.object());
        QCOMPARE(parsed.size(), 3);
        QCOMPARE(parsed[2].value(QStringLiteral("title")).toString(),
                 QStringLiteral("Suggested Three"));
    }

    void aMissingOrNonArrayKeyIsAnEmptyList()
    {
        QVERIFY(SunoHooksService::parseUserHooks(QJsonObject{}).isEmpty());
        QVERIFY(SunoHooksService::parseLiked(
                        QJsonObject{{QStringLiteral("hooks"), QStringLiteral("nope")}})
                        .isEmpty());
        QVERIFY(SunoHooksService::parseSuggested(QJsonObject{}).isEmpty());
        QVERIFY(SunoHooksService::parseSuggested(
                        QJsonObject{{QStringLiteral("suggested_clips"), 7}})
                        .isEmpty());
    }

    void noClientReportsSignInAndStaysEmpty()
    {
        SunoHooksService service(nullptr);
        QSignalSpy failed(&service, &SunoHooksService::failed);

        service.loadUserHooks();

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(service.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.userHooks().isEmpty());
        QVERIFY(!service.isLoading());

        service.loadLiked();

        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.likedHooks().isEmpty());
        QVERIFY(!service.isLoading());

        service.loadSuggested();

        QCOMPARE(failed.count(), 3);
        QCOMPARE(failed.at(2).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.suggestedClips().isEmpty());
        QVERIFY(!service.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
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

        SunoHooksService service(&client);
        QSignalSpy failed(&service, &SunoHooksService::failed);

        service.loadUserHooks();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isLoading());
        FakeReply* reply = findReply(replies, QStringLiteral("/video/hooks/user_hooks"));
        QVERIFY(reply != nullptr);

        reply->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(service.userHooks().isEmpty());
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

        SunoHooksService service(&client);
        QSignalSpy failed(&service, &SunoHooksService::failed);

        service.loadLiked();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        FakeReply* first = findReply(replies, QStringLiteral("/video/hooks/me/liked/v2"));
        QVERIFY(first != nullptr);
        first->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is transient: SunoClient re-sends once after its backoff floor, so
        // only the second reply's status reaches the service.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        FakeReply* second = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((second = findReply(replies, QStringLiteral("/video/hooks/me/liked/v2"))) != nullptr, 15000);
        second->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.likedHooks().isEmpty());
    }

    // TODO(T0383): success path has not run yet -- the orchestrator build is the
    // first execution. Analysis (and any fixture correction) happens after it.
    void successfulLoadsFillAllSections()
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

        SunoHooksService service(&client);
        QSignalSpy changed(&service, &SunoHooksService::sectionChanged);

        service.loadUserHooks();

        FakeReply* hooks = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (hooks = findReply(replies, QStringLiteral("/video/hooks/user_hooks")))
                        != nullptr,
                15000);
        hooks->respond(userHooksBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.userHooks().size() == 2, 15000);

        // Loaded after the first settles, so queue serialisation cannot starve
        // the next request.
        service.loadLiked();
        FakeReply* liked = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (liked = findReply(replies, QStringLiteral("/video/hooks/me/liked/v2")))
                        != nullptr,
                15000);
        liked->respond(likedBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.likedHooks().size() == 1, 15000);

        service.loadSuggested();
        FakeReply* suggested = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (suggested = findReply(replies, QStringLiteral("/video/hooks/suggested_clips")))
                        != nullptr,
                15000);
        suggested->respond(suggestedBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.suggestedClips().size() == 3, 15000);

        QCOMPARE(changed.count(), 6);
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isLoading());
    }
};

QTEST_MAIN(TestSunoHooksService)
#include "test_SunoHooksService.moc"
