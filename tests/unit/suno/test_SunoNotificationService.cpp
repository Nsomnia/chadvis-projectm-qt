#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoNotificationService.hpp"
#include "FakeNetworkReply.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {


/// A JWT that decodes and carries a future `exp`, so setToken accepts it.
QString fakeBearer() {
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend() {
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

const char* const kOneNotification =
        R"({"notified_at":"2026-10-23T10:00:00.000Z","notifications":[
            {"id":"n-1","notification_type":"follow","is_read":false,
             "user_profiles":[{"display_name":"Ada","handle":"ada"}]}]})";

} // namespace

class TestSunoNotificationService : public QObject
{
    Q_OBJECT

private slots:
    void parsesCapturedNotificationEnvelope()
    {
        const QJsonObject root{
            {QStringLiteral("notified_at"), QStringLiteral("2026-09-24T17:43:54.690Z")},
            {QStringLiteral("notifications"), QJsonArray{
                                               QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("n-1")},
                                                   {QStringLiteral("notification_type"), QStringLiteral("comment_mention")},
                                                   {QStringLiteral("updated_at"), QStringLiteral("2026-09-24T12:00:00.000Z")},
                                                   {QStringLiteral("is_read"), false},
                                                   {QStringLiteral("priority"), 2},
                                                   {QStringLiteral("total_users"), 1},
                                                   {QStringLiteral("user_profiles"), QJsonArray{
                                                                                      QJsonObject{
                                                                                          {QStringLiteral("display_name"), QStringLiteral("Ada")},
                                                                                          {QStringLiteral("handle"), QStringLiteral("ada")},
                                                                                      }}},
                                                   {QStringLiteral("content_id"), QStringLiteral("clip-1")},
                                                   {QStringLiteral("content_title"), QStringLiteral("A clip title")},
                                                   {QStringLiteral("content_message"), QStringLiteral("A caption")},
                                               },
                                           }},
        };

        auto parsed = SunoNotificationService::parseResponse(root);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->notifiedAt, QStringLiteral("2026-09-24T17:43:54.690Z"));
        QCOMPARE(parsed->notifications.size(), 1);

        const auto& notification = parsed->notifications.front();
        QCOMPARE(notification.id, QStringLiteral("n-1"));
        QCOMPARE(notification.type, QStringLiteral("comment_mention"));
        QCOMPARE(notification.createdAt, QStringLiteral("2026-09-24T12:00:00.000Z"));
        QVERIFY(!notification.read);
        QCOMPARE(notification.author.displayName, QStringLiteral("Ada"));
        QCOMPARE(notification.author.handle, QStringLiteral("ada"));
        QCOMPARE(notification.contentId, QStringLiteral("clip-1"));
        QCOMPARE(notification.contentTitle, QStringLiteral("A clip title"));
        QCOMPARE(notification.contentMessage, QStringLiteral("A caption"));
        QCOMPARE(notification.caption, QStringLiteral("A caption"));
    }

    /// T0335: v3 carries the sentence as rich-text segments, not `caption`.
    /// Before this the parser built an empty caption and no error, so the list
    /// rendered blank cards the moment the route flipped.
    void parsesV3TextSegments()
    {
        const QJsonObject root{
            {QStringLiteral("notified_at"), QStringLiteral("2026-10-24T03:08:00.000Z")},
            {QStringLiteral("notifications"), QJsonArray{
                                               QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("v3-1")},
                                                   {QStringLiteral("notification_type"), QStringLiteral("comment_mention")},
                                                   {QStringLiteral("updated_at"), QStringLiteral("2026-10-24T03:00:00.000Z")},
                                                   {QStringLiteral("is_read"), false},
                                                   {QStringLiteral("text"), QJsonArray{
                                                                                  QJsonObject{{QStringLiteral("text"), QStringLiteral("Ada")},
                                                                                              {QStringLiteral("bold"), true}},
                                                                                  QJsonObject{{QStringLiteral("text"), QStringLiteral(" mentioned you")}},
                                                                              }},
                                                   {QStringLiteral("avatars"), QJsonArray{
                                                                                  QJsonObject{{QStringLiteral("image_url"), QStringLiteral("https://cdn2.suno.ai/a.jpg")}},
                                                                              }},
                                                   {QStringLiteral("thumbnail_url"), QStringLiteral("https://cdn2.suno.ai/t.jpg")},
                                               },
                                           }},
        };

        auto parsed = SunoNotificationService::parseResponse(root);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->notifications.size(), 1);
        const auto& notification = parsed->notifications.front();
        QCOMPARE(notification.id, QStringLiteral("v3-1"));
        QCOMPARE(notification.type, QStringLiteral("comment_mention"));
        QCOMPARE(notification.createdAt, QStringLiteral("2026-10-24T03:00:00.000Z"));
        QVERIFY(!notification.read);
        QCOMPARE(notification.caption, QStringLiteral("Ada mentioned you"));
        QCOMPARE(notification.contentMessage, QStringLiteral("Ada mentioned you"));
        QCOMPARE(notification.author.avatarImageUrl, QStringLiteral("https://cdn2.suno.ai/a.jpg"));
    }

    /// A v3 entry with no `text[]` and no `avatars[]` must degrade to an
    /// empty caption and an empty avatar, not crash or fail the whole list.
    void v3EntryWithoutTextOrAvatarsDegrades()
    {
        const QJsonObject root{
            {QStringLiteral("notified_at"), QStringLiteral("2026-10-24T03:08:00.000Z")},
            {QStringLiteral("notifications"), QJsonArray{
                                               QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("v3-bare")},
                                                   {QStringLiteral("notification_type"), QStringLiteral("follow")},
                                                   {QStringLiteral("updated_at"), QStringLiteral("2026-10-24T03:00:00.000Z")},
                                                   {QStringLiteral("is_read"), true},
                                               },
                                           }},
        };

        auto parsed = SunoNotificationService::parseResponse(root);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->notifications.size(), 1);
        const auto& notification = parsed->notifications.front();
        QCOMPARE(notification.id, QStringLiteral("v3-bare"));
        QVERIFY(notification.read);
        QCOMPARE(notification.caption, QString{});
        QCOMPARE(notification.author.displayName, QString{});
        QCOMPARE(notification.author.avatarImageUrl, QString{});
    }

    /// The two v3 fallbacks are independent: text without an avatar keeps the
    /// caption with an empty avatar, and an avatar without text keeps the
    /// avatar with an empty caption.
    void v3TextAndAvatarDegradeIndependently()
    {
        const QJsonObject textOnly{
            {QStringLiteral("notifications"), QJsonArray{
                                            QJsonObject{
                                                {QStringLiteral("id"), QStringLiteral("v3-text-only")},
                                                {QStringLiteral("notification_type"), QStringLiteral("follow")},
                                                {QStringLiteral("text"), QJsonArray{
                                                                       QJsonObject{{QStringLiteral("text"), QStringLiteral("Ada")}},
                                                                       QJsonObject{{QStringLiteral("text"), QStringLiteral(" followed you")}},
                                                                   }},
                                            },
                                        }},
        };
        auto textParsed = SunoNotificationService::parseResponse(textOnly);
        QVERIFY(textParsed.has_value());
        QCOMPARE(textParsed->notifications.size(), 1);
        QCOMPARE(textParsed->notifications.front().caption, QStringLiteral("Ada followed you"));
        QCOMPARE(textParsed->notifications.front().author.avatarImageUrl, QString{});

        const QJsonObject avatarOnly{
            {QStringLiteral("notifications"), QJsonArray{
                                            QJsonObject{
                                                {QStringLiteral("id"), QStringLiteral("v3-avatar-only")},
                                                {QStringLiteral("notification_type"), QStringLiteral("follow")},
                                                {QStringLiteral("avatars"), QJsonArray{
                                                                         QJsonObject{{QStringLiteral("image_url"), QStringLiteral("https://cdn2.suno.ai/a.jpg")}},
                                                                     }},
                                            },
                                        }},
        };
        auto avatarParsed = SunoNotificationService::parseResponse(avatarOnly);
        QVERIFY(avatarParsed.has_value());
        QCOMPARE(avatarParsed->notifications.size(), 1);
        QCOMPARE(avatarParsed->notifications.front().caption, QString{});
        QCOMPARE(avatarParsed->notifications.front().author.avatarImageUrl,
                 QStringLiteral("https://cdn2.suno.ai/a.jpg"));
    }

    /// T0335: the list fetch is v3, the badge counter stays on v2. The
    /// fixtures go through QJsonDocument so the bytes are exactly what the
    /// parser sees on the wire.
    void listUsesV3AndBadgeStaysV2()
    {
        std::vector<FakeReply*> replies;
        std::vector<QString> paths;
        auto factory = [&replies, &paths](const QNetworkRequest& request, const std::string& method,
                                          const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            paths.push_back(request.url().path());
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoNotificationService service(&client, nullptr, 0);
        QSignalSpy changed(&service, &SunoNotificationService::notificationsChanged);
        QSignalSpy unread(&service, &SunoNotificationService::unreadCountChanged);

        const QJsonObject listRoot{
            {QStringLiteral("notified_at"), QStringLiteral("2026-10-24T03:08:00.000Z")},
            {QStringLiteral("notifications"), QJsonArray{
                                               QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("v3-1")},
                                                   {QStringLiteral("notification_type"), QStringLiteral("follow")},
                                                   {QStringLiteral("text"), QJsonArray{
                                                                          QJsonObject{{QStringLiteral("text"), QStringLiteral("Ada")},
                                                                                      {QStringLiteral("bold"), true}},
                                                                          QJsonObject{{QStringLiteral("text"), QStringLiteral(" followed you")}},
                                                                      }},
                                                   {QStringLiteral("avatars"), QJsonArray{
                                                                          QJsonObject{{QStringLiteral("image_url"), QStringLiteral("https://cdn2.suno.ai/a.jpg")}},
                                                                      }},
                                               },
                                           }},
        };
        const QByteArray listBody =
                QJsonDocument(listRoot).toJson(QJsonDocument::Compact);
        const QJsonObject badgeRoot{{QStringLiteral("badge_count"), 7}};
        const QByteArray badgeBody =
                QJsonDocument(badgeRoot).toJson(QJsonDocument::Compact);

        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        bool sawV3 = false;
        bool sawV2Badge = false;
        for (FakeReply* reply : replies) {
            const QString path = reply->url().path();
            if (path.contains(QStringLiteral("notification/v3"))) {
                sawV3 = true;
                reply->respond(listBody, 200);
            } else if (path.contains(QStringLiteral("notification/v2/badge-count"))) {
                sawV2Badge = true;
                reply->respond(badgeBody, 200);
            }
        }
        QVERIFY2(sawV3, "the list fetch must hit notification/v3");
        QVERIFY2(sawV2Badge, "the badge counter must stay on notification/v2/badge-count");

        QTRY_VERIFY_WITH_TIMEOUT(!service.isLoading(), 15000);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(service.notifications().size(), 1);
        QCOMPARE(service.notifications().front().caption, QStringLiteral("Ada followed you"));
        QCOMPARE(service.notifications().front().author.avatarImageUrl,
                 QStringLiteral("https://cdn2.suno.ai/a.jpg"));
        QCOMPARE(service.unreadCount(), 7);
        QCOMPARE(unread.count(), 1);
        QVERIFY(service.error().isEmpty());
    }

    /// A transient 500 on the v3 list is retried once by SunoClient
    /// (reads pass retryOnUnauthorized=true), so the second reply carries
    /// the real list. The badge half answers 200 immediately.
    void transientListFailureIsRetriedOnce()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoNotificationService service(&client, nullptr, 0);
        QSignalSpy changed(&service, &SunoNotificationService::notificationsChanged);

        const QJsonObject listRoot{
            {QStringLiteral("notified_at"), QStringLiteral("2026-10-24T03:08:00.000Z")},
            {QStringLiteral("notifications"), QJsonArray{
                                               QJsonObject{
                                                   {QStringLiteral("id"), QStringLiteral("v3-1")},
                                                   {QStringLiteral("notification_type"), QStringLiteral("follow")},
                                                   {QStringLiteral("text"), QJsonArray{
                                                                          QJsonObject{{QStringLiteral("text"), QStringLiteral("Ada")}},
                                                                          QJsonObject{{QStringLiteral("text"), QStringLiteral(" followed you")}},
                                                                      }},
                                               },
                                           }},
        };
        const QByteArray listBody =
                QJsonDocument(listRoot).toJson(QJsonDocument::Compact);
        const QJsonObject badgeRoot{{QStringLiteral("badge_count"), 2}};
        const QByteArray badgeBody =
                QJsonDocument(badgeRoot).toJson(QJsonDocument::Compact);

        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        const std::size_t before = replies.size();
        for (FakeReply* reply : replies) {
            if (reply->url().path().contains(QStringLiteral("notification/v3"))) {
                reply->respond(QByteArrayLiteral("{}"), 500);
            } else {
                reply->respond(badgeBody, 200);
            }
        }
        // The 500 triggers one transient retry; a second v3 reply appears.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= before + 1, 15000);
        for (std::size_t i = before; i < replies.size(); ++i) {
            replies[i]->respond(listBody, 200);
        }

        QTRY_VERIFY_WITH_TIMEOUT(!service.isLoading(), 15000);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(service.notifications().size(), 1);
        QCOMPARE(service.notifications().front().caption, QStringLiteral("Ada followed you"));
        QCOMPARE(service.unreadCount(), 2);
    }

    void toleratesMalformedEntriesAndNestedContent()
    {
        const QJsonObject root{
            {QStringLiteral("notifications"), QJsonArray{
                                               QJsonValue(QStringLiteral("ignored")),
                                               QJsonObject{
                                                   {QStringLiteral("notification_type"), QStringLiteral("follow")},
                                                   {QStringLiteral("user_profiles"), QStringLiteral("ignored")},
                                                   {QStringLiteral("content"), QJsonObject{
                                                                                       {QStringLiteral("id"), QStringLiteral("nested-id")},
                                                                                       {QStringLiteral("title"), QStringLiteral("Nested title")},
                                                                                       {QStringLiteral("message"), QStringLiteral("Nested caption")},
                                                                                   }},
                                               },
                                           }},
        };

        auto parsed = SunoNotificationService::parseResponse(root);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->notifications.size(), 1);
        QCOMPARE(parsed->notifications.front().contentId, QStringLiteral("nested-id"));
        QCOMPARE(parsed->notifications.front().contentTitle, QStringLiteral("Nested title"));
        QCOMPARE(parsed->notifications.front().caption, QStringLiteral("Nested caption"));
    }

    void rejectsMissingNotificationsArray()
    {
        QVERIFY(!SunoNotificationService::parseResponse(QJsonObject{}).has_value());
        QVERIFY(!SunoNotificationService::parseResponse(
                         QJsonObject{{QStringLiteral("notifications"), QStringLiteral("invalid")}})
                         .has_value());
    }

    void parsesBadgeAndMarkAllReadContract()
    {
        auto badge = SunoNotificationService::parseBadgeCount(
                QJsonObject{{QStringLiteral("badge_count"), 3}});
        QVERIFY(badge.has_value());
        QCOMPARE(*badge, static_cast<qint64>(3));

        const QDateTime cutoff = QDateTime::fromString(
                QStringLiteral("2026-09-24T16:58:31.500Z"), Qt::ISODateWithMs);
        const QJsonObject body = SunoNotificationService::markAllReadBody(cutoff);
        QCOMPARE(body.size(), 2);
        QCOMPARE(body.value(QStringLiteral("all")).toBool(), true);
        QCOMPARE(body.value(QStringLiteral("before_datetime_utc")).toString(),
                 QStringLiteral("2026-09-24T16:58:31.500Z"));
    }

    // T0067, the correctness half: the badge counter is decorative and the list
    // is not, so a badge failure must not throw the list away. The old shape
    // paired the two replies, bumped the generation on any failure and cleared
    // both pending slots -- one 404 and a fully successful list was discarded.
    void aBadgeFailureDoesNotDiscardTheList()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        SunoNotificationService service(&client, nullptr, 0);  // no polling in this case
        QSignalSpy changed(&service, &SunoNotificationService::notificationsChanged);
        replies.clear();

        service.refresh();
        // SunoClient rate-limits its request queue, so "two replies" is not a
        // 2 s event on a loaded CI runner: measured 2.1 s on ubuntu-24.04/gcc.
        // The timeout is a bound on the test, not a claim about the client.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        for (FakeReply* reply : replies) {
            if (reply->url().path().contains(QStringLiteral("badge-count"))) {
                reply->respond(QByteArrayLiteral("{}"), 404);
            } else {
                reply->respond(QByteArray::fromRawData(kOneNotification,
                                                       static_cast<int>(std::strlen(kOneNotification))),
                               200);
            }
        }

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 15000);
        QCOMPARE(service.notifications().size(), 1);
        QCOMPARE(service.notifications().front().id, QStringLiteral("n-1"));
        QVERIFY2(!service.error().isEmpty(),
                 "the failed half must still be reported; only its data is independent");
        QVERIFY(!service.isLoading());
    }

    // The dedup half: the poll wakes up every interval, and re-emitting an
    // unchanged page would rebuild the list under the user's scroll position.
    void anUnchangedListIsAppliedOnce()
    {
        std::vector<FakeReply*> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoNotificationService service(&client, nullptr, 0);
        QSignalSpy changed(&service, &SunoNotificationService::notificationsChanged);
        const QByteArray page(kOneNotification, static_cast<int>(std::strlen(kOneNotification)));

        for (int round = 0; round < 2; ++round) {
            const int before = static_cast<int>(replies.size());
            service.refresh();
            QTRY_VERIFY_WITH_TIMEOUT(static_cast<int>(replies.size()) >= before + 2, 15000);
            for (std::size_t i = static_cast<std::size_t>(before); i < replies.size(); ++i) {
                replies[i]->respond(page, 200);
            }
            QTRY_VERIFY_WITH_TIMEOUT(!service.isLoading(), 15000);
        }

        QCOMPARE(changed.count(), 1);
    }

    // T0067, the feature half: the badge moves without anyone opening the view.
    // The interval is the test's, which is also why the constructor takes one.
    void pollingRefreshesWithoutAView()
    {
        int listRequests = 0;
        auto factory = [&listRequests](const QNetworkRequest& request, const std::string& method,
                                       const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            if (request.url().path().contains(QStringLiteral("notification/v2"))) {
                ++listRequests;
                // Answer it, or the service stays "loading" after the first poll
                // and the timer correctly does nothing for the rest of the test.
                QTimer::singleShot(0, reply, [reply] {
                    reply->respond(QByteArrayLiteral("{\"badge_count\":0,\"notifications\":[]}"),
                                   200);
                });
            }
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);

        // Signed out: the timer must be silent rather than reporting
        // "Not authenticated" every interval.
        SunoNotificationService signedOut(&client, nullptr, 10);
        QTest::qWait(120);
        QCOMPARE(listRequests, 0);

        client.setToken(fakeBearer().toStdString());
        SunoNotificationService service(&client, nullptr, 10);
        // One automatic request is the whole claim: nothing here opens a view,
        // so any request at all can only have come from the timer. Asking for
        // four turned the rate limiter's pacing into part of the assertion.
        QTRY_VERIFY_WITH_TIMEOUT(listRequests >= 1, 15000);
    }
};

#include "test_SunoNotificationService.moc"

int main(int argc, char** argv)
{
    // The parse cases need no event loop; the delivery and polling cases do --
    // replies arrive through signals and the poll timer needs a dispatcher.
    QCoreApplication app(argc, argv);
    TestSunoNotificationService test;
    return QTest::qExec(&test, argc, argv);
}
