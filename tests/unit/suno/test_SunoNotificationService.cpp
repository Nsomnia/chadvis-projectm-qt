#include <QtTest>

#include "suno/SunoNotificationService.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

using namespace vc::suno;

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
};

#include "test_SunoNotificationService.moc"

int main(int argc, char** argv)
{
    TestSunoNotificationService test;
    return QTest::qExec(&test, argc, argv);
}
