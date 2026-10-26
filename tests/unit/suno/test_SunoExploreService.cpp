#include <QtTest>

#include "suno/SunoExploreService.hpp"

#include <QJsonArray>
#include <QJsonObject>

using namespace vc::suno;

namespace {

QJsonObject clip(const QString& id, const QString& title)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("status"), QStringLiteral("complete")},
    };
}

}

class TestSunoExploreService : public QObject
{
    Q_OBJECT

private slots:
    void requestCursorContract()
    {
        const QJsonObject first = SunoExploreService::requestBody(std::nullopt);
        QVERIFY(first.value(QStringLiteral("cursor")).isNull());
        QCOMPARE(first.size(), 1);

        const QJsonObject next =
            SunoExploreService::requestBody(QStringLiteral("opaque-next"));
        QCOMPARE(next.value(QStringLiteral("cursor")).toString(),
                 QStringLiteral("opaque-next"));
        QCOMPARE(next.size(), 1);
    }

    void parsesNestedClipsAndPreservesFeedLabels()
    {
        const QJsonObject root{
            {QStringLiteral("feeds"), QJsonArray{
                                          QJsonObject{
                                              {QStringLiteral("id"), 42},
                                              {QStringLiteral("title"), QStringLiteral("Trending")},
                                              {QStringLiteral("items"), QJsonArray{
                                                                            QJsonObject{{QStringLiteral("item"),
                                                                                         clip(QStringLiteral("one"), QStringLiteral("One"))}},
                                                                            QJsonObject{{QStringLiteral("clip_schema"),
                                                                                         clip(QStringLiteral("two"), QStringLiteral("Two"))}},
                                                                            QJsonObject{{QStringLiteral("clip"),
                                                                                         clip(QStringLiteral("three"), QStringLiteral("Three"))}},
                                                                            QJsonObject{{QStringLiteral("item"), QJsonObject{
                                                                                                                     {QStringLiteral("clip_schema"),
                                                                                                                      clip(QStringLiteral("four"), QStringLiteral("Four"))}}}},
                                                                             QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("id"), QStringLiteral("wrapper")}, {QStringLiteral("clip_schema"), clip(QStringLiteral("five"), QStringLiteral("Five"))}}}},
                                                                             QJsonObject{{QStringLiteral("item"), QJsonObject{{QStringLiteral("type"), QStringLiteral("playlist")}}}},
                                                                        }},
                                          },
                                          QJsonObject{
                                              {QStringLiteral("name"), QStringLiteral("Editorial picks")},
                                              {QStringLiteral("items"), QJsonArray{
                                                                            QJsonObject{{QStringLiteral("kind"), QStringLiteral("collection")}},
                                                                        }},
                                          },
                                      }},
            {QStringLiteral("next_cursor"), QStringLiteral("next-page")},
        };

        auto page = SunoExploreService::parsePage(root);
        QVERIFY(page.has_value());
        QCOMPARE(page->feeds.size(), 2);
        QCOMPARE(page->feeds[0].id, QStringLiteral("42"));
        QCOMPARE(page->feeds[0].label, QStringLiteral("Trending"));
        QCOMPARE(page->feeds[0].clips.size(), 5);
        QCOMPARE(QString::fromStdString(page->feeds[0].clips[0].id),
                 QStringLiteral("one"));
        QCOMPARE(QString::fromStdString(page->feeds[0].clips[4].id),
                 QStringLiteral("five"));
        QCOMPARE(page->feeds[1].label, QStringLiteral("Editorial picks"));
        QVERIFY(page->feeds[1].clips.isEmpty());
        QCOMPARE(page->nextCursor, QStringLiteral("next-page"));
        QVERIFY(page->hasMore());
    }

    // The live 2026-10-24 envelope, which the old parser could not read at all:
    // feeds name themselves feed_id/feed_title, every item wraps its payload in
    // content_item, and one item's payload is *another feed* whose items are the
    // clips. Measured against the wire: 2 root feeds, 6 shortcut tiles, 3
    // generic_feed containers, 10 clips inside the first container.
    void parsesTheContentItemEnvelopeWithNestedFeeds()
    {
        const QJsonObject shortcut{
            {QStringLiteral("content_id"), QStringLiteral("s-1")},
            {QStringLiteral("content_type"), QStringLiteral("shortcut")},
            {QStringLiteral("content_item"), QJsonObject{
                                                 {QStringLiteral("id"), QStringLiteral("create")},
                                                 {QStringLiteral("entity_type"), QStringLiteral("shortcut_schema")},
                                                 {QStringLiteral("name"), QStringLiteral("Create")},
                                                 {QStringLiteral("redirect_url"), QStringLiteral("/create")},
                                             }},
        };
        const QJsonObject nestedFeed{
            {QStringLiteral("feed_id"), QStringLiteral("feed-staff-picks")},
            {QStringLiteral("feed_title"), QStringLiteral("Staff Picks")},
            {QStringLiteral("items"), QJsonArray{
                                          QJsonObject{
                                              {QStringLiteral("content_id"), QStringLiteral("c-1")},
                                              {QStringLiteral("content_type"), QStringLiteral("clip")},
                                              {QStringLiteral("content_item"),
                                               clip(QStringLiteral("one"), QStringLiteral("One"))},
                                          },
                                      }},
            {QStringLiteral("next_cursor"), QStringLiteral("10")},
        };

        const QJsonObject root{
            {QStringLiteral("feeds"), QJsonArray{
                                          QJsonObject{
                                              {QStringLiteral("feed_id"), QStringLiteral("shortcuts")},
                                              {QStringLiteral("feed_title"), QStringLiteral("Shortcuts")},
                                              {QStringLiteral("items"), QJsonArray{shortcut}},
                                          },
                                          QJsonObject{
                                              {QStringLiteral("feed_id"), QStringLiteral("for-you")},
                                              {QStringLiteral("feed_container_type"), QStringLiteral("feed")},
                                              {QStringLiteral("feed_title"), QStringLiteral("For You")},
                                              {QStringLiteral("items"), QJsonArray{
                                                                            QJsonObject{
                                                                                {QStringLiteral("content_id"), QStringLiteral("n-1")},
                                                                                {QStringLiteral("content_type"), QStringLiteral("generic_feed")},
                                                                                {QStringLiteral("content_item"), nestedFeed},
                                                                            },
                                                                        }},
                                          },
                                      }},
            {QStringLiteral("next_cursor"), QStringLiteral("3")},
        };

        const auto page = SunoExploreService::parsePage(root);
        QVERIFY(page.has_value());
        // The container ("For You") is not published: it holds no clips of its
        // own, and a title above an empty row reads as a failure.
        QCOMPARE(page->feeds.size(), 2);
        QCOMPARE(page->feeds[0].id, QStringLiteral("shortcuts"));
        QCOMPARE(page->feeds[0].label, QStringLiteral("Shortcuts"));
        QVERIFY(page->feeds[0].clips.isEmpty());
        QCOMPARE(page->feeds[1].id, QStringLiteral("feed-staff-picks"));
        QCOMPARE(page->feeds[1].label, QStringLiteral("Staff Picks"));
        QCOMPARE(page->feeds[1].clips.size(), 1);
        QCOMPARE(QString::fromStdString(page->feeds[1].clips[0].id), QStringLiteral("one"));
        QCOMPARE(page->nextCursor, QStringLiteral("3"));
        QVERIFY(page->hasMore());
    }

    void malformedAndExhaustedResponses()
    {
        QVERIFY(!SunoExploreService::parsePage(QJsonObject{}).has_value());
        QVERIFY(!SunoExploreService::parsePage(
                     QJsonObject{{QStringLiteral("feeds"), QStringLiteral("invalid")}})
                     .has_value());

        auto empty = SunoExploreService::parsePage(QJsonObject{
            {QStringLiteral("feeds"), QJsonArray{}},
            {QStringLiteral("next_cursor"), QJsonValue::Null},
        });
        QVERIFY(empty.has_value());
        QVERIFY(empty->feeds.isEmpty());
        QVERIFY(!empty->hasMore());
    }
};

#include "test_SunoExploreService.moc"

int main(int argc, char** argv)
{
    TestSunoExploreService test;
    return QTest::qExec(&test, argc, argv);
}
