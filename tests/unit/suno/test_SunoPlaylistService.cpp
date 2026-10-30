#include <QtTest>

#include "suno/SunoEndpoints.hpp"
#include "suno/SunoPlaylistService.hpp"

#include <QJsonArray>
#include <QJsonObject>

using namespace vc::suno;

namespace {

QJsonObject clipJson(const QString& id, const QString& title)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("status"), QStringLiteral("complete")},
    };
}

/// The 2026-10-26 captured body, key for key: the playlist object at the TOP
/// level, `playlist_clips[] = {clip, relative_index, created_at}`.
QJsonObject capturedPlaylistBody()
{
    return QJsonObject{
        {QStringLiteral("entity_type"), QStringLiteral("playlist")},
        {QStringLiteral("id"), QStringLiteral("pl_1")},
        {QStringLiteral("name"), QStringLiteral("Night drive")},
        {QStringLiteral("image_url"), QStringLiteral("https://cdn1.suno.ai/p.jpg")},
        {QStringLiteral("num_total_results"), 42},
        {QStringLiteral("current_page"), 1},
        {QStringLiteral("song_count"), 2},
        {QStringLiteral("total_duration"), 331},
        {QStringLiteral("is_owned"), true},
        {QStringLiteral("is_public"), false},
        {QStringLiteral("user_display_name"), QStringLiteral("Derek")},
        {QStringLiteral("user_handle"), QStringLiteral("derek")},
        {QStringLiteral("playlist_clips"),
         QJsonArray{
                 QJsonObject{
                         {QStringLiteral("clip"), clipJson(QStringLiteral("c1"), QStringLiteral("one"))},
                         {QStringLiteral("relative_index"), 0},
                         {QStringLiteral("created_at"), QStringLiteral("2026-10-20T00:00:00Z")},
                 },
                 QJsonObject{
                         {QStringLiteral("clip"), clipJson(QStringLiteral("c2"), QStringLiteral("two"))},
                         {QStringLiteral("relative_index"), 1},
                         {QStringLiteral("created_at"), QStringLiteral("2026-10-20T00:01:00Z")},
                 },
         }},
    };
}

} // namespace

class TestSunoPlaylistService : public QObject
{
    Q_OBJECT

private slots:
    void theRequestPathIsTheCapturedOne()
    {
        // The capture is `/playlist/{id}/?page=1` -- slash before the query.
        // A tidy-up that drops it is a different route, so it is pinned here.
        const QString path = QString(qstr(endpoints::PLAYLIST_BY_ID))
                                     .replace(QStringLiteral("{}"), QStringLiteral("pl_1"))
                + QStringLiteral("/?page=1");
        QCOMPARE(path, QStringLiteral("/playlist/pl_1/?page=1"));
    }

    void parsesTheCapturedPlaylistBody()
    {
        const auto page = SunoPlaylistService::parsePage(capturedPlaylistBody());
        QVERIFY(page.has_value());
        QCOMPARE(page->playlist.id, QStringLiteral("pl_1"));
        QCOMPARE(page->playlist.name, QStringLiteral("Night drive"));
        QCOMPARE(page->playlist.totalDuration, qint64(331));
        QVERIFY(page->playlist.isOwned);
        QVERIFY(!page->playlist.isPublic);
        QCOMPARE(page->currentPage, 1);
        QCOMPARE(page->totalResults, 42);
        QCOMPARE(page->clips.size(), 2);
        QCOMPARE(QString::fromStdString(page->clips.at(0).id), QStringLiteral("c1"));
        QCOMPARE(QString::fromStdString(page->clips.at(1).id), QStringLiteral("c2"));
    }

    void anEmptyPastTheEndPageIsTheEndSignal()
    {
        QJsonObject body = capturedPlaylistBody();
        body[QStringLiteral("playlist_clips")] = QJsonArray{};
        body[QStringLiteral("current_page")] = 9;

        const auto page = SunoPlaylistService::parsePage(body);
        QVERIFY(page.has_value());
        QVERIFY(page->clips.isEmpty());
        QVERIFY(!SunoPlaylistService::hasMoreAfter(0, page->totalResults));
    }

    void pagingArithmeticUsesTheServerTotal()
    {
        // page 1 of a 42-clip playlist: keep going.
        QVERIFY(SunoPlaylistService::hasMoreAfter(20, 42));
        // exactly the total: stop, even though a page may still have come back full.
        QVERIFY(!SunoPlaylistService::hasMoreAfter(42, 42));
        // no denominator: only an empty page ends it.
        QVERIFY(SunoPlaylistService::hasMoreAfter(20, 0));
        QVERIFY(!SunoPlaylistService::hasMoreAfter(0, 0));
    }

    void aBodyThatIsNotAPlaylistIsRefused()
    {
        // Fail closed: an error the caller reports, not an empty playlist the
        // user would read as "this playlist has no tracks".
        QVERIFY(!SunoPlaylistService::parsePage(QJsonObject{}).has_value());
        QVERIFY(!SunoPlaylistService::parsePage(
                        QJsonObject{{QStringLiteral("detail"), QStringLiteral("Not found")}})
                         .has_value());
    }

    void listAcceptsBothEnvelopes()
    {
        const QJsonObject playlist{
            {QStringLiteral("id"), QStringLiteral("pl_1")},
            {QStringLiteral("name"), QStringLiteral("Night drive")},
            {QStringLiteral("num_total_results"), 42},
        };

        const auto bare = SunoPlaylistService::parseList(QJsonValue(QJsonArray{playlist}));
        QVERIFY(bare.has_value());
        QCOMPARE(bare->size(), 1);
        QCOMPARE(bare->at(0).id, QStringLiteral("pl_1"));

        const auto wrapped = SunoPlaylistService::parseList(
                QJsonValue(QJsonObject{{QStringLiteral("playlists"), QJsonArray{playlist}}}));
        QVERIFY(wrapped.has_value());
        QCOMPARE(wrapped->size(), 1);
        QCOMPARE(wrapped->at(0).name, QStringLiteral("Night drive"));
    }

    void listReadsTheConfirmedLiveEnvelope()
    {
        // The user's own extension client (a confirmed 200 in their register)
        // documents `/api/playlist/me` as
        // `{num_total_results, current_page, playlists:[...]}`. The wrapper is
        // what the real route sends, so the key is pinned rather than assumed.
        const auto parsed = SunoPlaylistService::parseList(QJsonValue(QJsonObject{
            {QStringLiteral("num_total_results"), 2},
            {QStringLiteral("current_page"), 1},
            {QStringLiteral("playlists"),
             QJsonArray{
                     QJsonObject{{QStringLiteral("id"), QStringLiteral("pl_1")},
                                 {QStringLiteral("name"), QStringLiteral("Mine")}},
                     QJsonObject{{QStringLiteral("id"), QStringLiteral("pl_2")},
                                 {QStringLiteral("name"), QStringLiteral("Also mine")}},
             }},
        }));
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->size(), 2);
        QCOMPARE(parsed->at(1).id, QStringLiteral("pl_2"));
    }

    void listRefusesAnEnvelopeWithNoArray()
    {
        const auto parsed = SunoPlaylistService::parseList(
                QJsonValue(QJsonObject{{QStringLiteral("detail"), QStringLiteral("Not found")}}));
        QVERIFY(!parsed.has_value());
    }

    void listSkipsEntriesWithNoId()
    {
        // A card without an id cannot be opened; it must not become a blank row.
        const auto parsed = SunoPlaylistService::parseList(QJsonValue(
                QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("nameless")}}}));
        QVERIFY(parsed.has_value());
        QVERIFY(parsed->isEmpty());
    }
};

QTEST_MAIN(TestSunoPlaylistService)
#include "test_SunoPlaylistService.moc"
