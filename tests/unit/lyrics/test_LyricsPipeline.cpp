#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "qml_bridge/LyricsBridge.hpp"
#include "suno/ClipParser.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoLyrics.hpp"

using namespace vc;
using namespace vc::suno;

namespace {

SunoClip makeClip()
{
    SunoClip clip;
    clip.id = "clip-1";
    clip.title = "Captured Song";
    clip.display_name = "Captured Artist";
    clip.metadata.prompt = "style prompt words";
    clip.metadata.lyrics = "[Verse]\nhello world";
    return clip;
}

QJsonObject capturedClipObject()
{
    return QJsonDocument::fromJson(QByteArray(R"({
        "id": "clip-1",
        "title": "Captured Song",
        "display_name": "Captured Artist",
        "metadata": {
            "prompt": "style prompt words",
            "lyrics": "[Verse]\nhello world"
        }
    })"))
        .object();
}

LyricsData makeLyrics()
{
    LyricsData data;
    data.source = "suno";
    data.title = "Test";
    data.artist = "Artist";
    data.isSynced = true;

    LyricsLine first;
    first.text = "hello";
    first.startTime = 0.0f;
    first.endTime = 1.0f;
    first.isSynced = true;
    first.words.push_back({"hello", 0.0f, 0.5f, 1.0f});
    first.words.push_back({"world", 0.5f, 1.0f, 1.0f});

    LyricsLine second;
    second.text = "again";
    second.startTime = 1.0f;
    second.endTime = 2.0f;
    second.isSynced = true;
    second.words.push_back({"again", 1.0f, 2.0f, 1.0f});

    data.lines.push_back(first);
    data.lines.push_back(second);
    return data;
}

LyricsData makeLyricsOfCount(int lineCount)
{
    LyricsData data;
    data.source = "suno";
    data.title = "Test";
    data.artist = "Artist";
    data.isSynced = true;

    for (int i = 0; i < lineCount; ++i) {
        LyricsLine line;
        line.text = "line" + std::to_string(i);
        line.startTime = static_cast<f32>(i);
        line.endTime = static_cast<f32>(i) + 1.0f;
        line.isSynced = true;
        line.words.push_back({line.text, line.startTime, line.endTime, 1.0f});
        data.lines.push_back(line);
    }
    return data;
}

class DownloadPathGuard
{
public:
    explicit DownloadPathGuard(fs::path path)
        : path_(std::move(path))
    {
    }

    ~DownloadPathGuard()
    {
        CONFIG.suno().downloadPath = path_;
    }

private:
    fs::path path_;
};

}

class TestLyricsPipeline : public QObject
{
    Q_OBJECT

private slots:
    void capturedFieldsAndMetadataLyrics()
    {
        const auto parsed = ClipParser::parseClip(capturedClipObject());
        QVERIFY(parsed.has_value());
        QCOMPARE(QString::fromStdString(parsed->metadata.lyrics),
                 QStringLiteral("[Verse]\nhello world"));
        QCOMPARE(QString::fromStdString(parsed->metadata.prompt),
                 QStringLiteral("style prompt words"));

        const auto clip = makeClip();
        const auto json = R"({
            "aligned_words": [
                {"word":"hello","start_s":0.0,"end_s":0.5,"p_align":0.93,"success":true},
                {"word":"world","start_s":0.5,"end_s":1.0,"p_align":0.81,"success":true}
            ]
        })";
        const auto lyrics = LyricsAligner::parseCapturedSunoLyrics(clip, json);
        QVERIFY(lyrics.has_value());
        QCOMPARE(lyrics->songId, std::string("clip-1"));
        QCOMPARE(lyrics->title, std::string("Captured Song"));
        QCOMPARE(lyrics->artist, std::string("Captured Artist"));
        QCOMPARE(lyrics->source, std::string("suno"));
        QVERIFY(lyrics->isSynced);
        QCOMPARE(lyrics->lines.size(), size_t{2});
        QCOMPARE(lyrics->lines[0].text, std::string("[Verse]"));
        QVERIFY(lyrics->lines[0].isInstrumental);
        QCOMPARE(lyrics->lines[1].text, std::string("hello world"));
        QCOMPARE(lyrics->lines[1].words.size(), size_t{2});
        QCOMPARE(lyrics->lines[1].words[0].confidence, 0.93f);
    }

    void invalidCapturedPayloadIsRejected()
    {
        const auto clip = makeClip();
        QVERIFY(!LyricsAligner::parseCapturedSunoLyrics(
            R"({"aligned_words":[{"word":"hello","start_s":0.0}]})", clip));
        QVERIFY(!LyricsAligner::parseCapturedSunoLyrics(
            R"({"aligned_words":[{"word":"hello","start_s":1.0,"end_s":0.5}]})", clip));
        QVERIFY(!LyricsAligner::parseCapturedSunoLyrics(
            R"({"aligned_words":[{"word":"hello","start_s":"bad","end_s":1.0}]})", clip));
    }

    void syncSelectsLineAndClampsProgress()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyrics());
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);

        // seek() only seeds the position; syncNow() is the one writer, so the
        // tests drive it directly instead of waiting on the 16 ms timer.
        sync.seek(0.75f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 0);
        QCOMPARE(sync.getPosition().wordIndex, 1);
        QVERIFY(std::abs(sync.getPosition().lineProgress - 0.75f) < 0.001f);

        sync.seek(3.0f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 1);
        QCOMPARE(sync.getPosition().lineProgress, 1.0f);
        QCOMPARE(sync.getPosition().wordProgress, 0.0f);
    }

    void seekSeedsAndAppliesExactlyOnce()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyrics());

        sync.seek(0.75f);

        // Nothing has moved yet: the seek installed a seed, it did not apply
        // a position. This is what a direct updatePosition() call used to do
        // here, and the following tick would then lerp the same instant a
        // second time.
        QCOMPARE(sync.getPosition().time, 0.0f);
        QCOMPARE(sync.getPosition().lineIndex, -1);

        // One drain lands exactly on the requested time, not part-way there.
        sync.syncNow();
        QCOMPARE(sync.getPosition().time, 0.75f);
        QCOMPARE(sync.getPosition().lineIndex, 0);
        QCOMPARE(sync.getPosition().wordIndex, 1);

        // The seed is consumed. With no AudioEngine there is nothing left to
        // sample, so a second drain is a no-op rather than another lerp.
        sync.syncNow();
        QCOMPARE(sync.getPosition().time, 0.75f);
        QCOMPARE(sync.getPosition().lineIndex, 0);
        QCOMPARE(sync.getPosition().wordIndex, 1);
    }

    void lastSeedBeforeADrainWins()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyricsOfCount(8));

        sync.seek(7.5f);
        sync.seek(3.5f);
        sync.syncNow();

        // The second seek overwrote the first seed rather than being lost
        // behind it, and neither was applied before the drain.
        QCOMPARE(sync.getPosition().time, 3.5f);
        QCOMPARE(sync.getPosition().lineIndex, 3);

        sync.syncNow();
        QCOMPARE(sync.getPosition().time, 3.5f);
    }

    void clearDropsAPendingSeed()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyricsOfCount(8));
        sync.seek(7.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 7);
        QVERIFY(sync.getPosition().time > 7.0f);

        // clear-then-load is the playback handoff ordering; a seed that
        // outlived clear() would be drained against a song that ends at 2 s.
        sync.clear();
        QCOMPARE(sync.getState(), LyricsSyncState::Idle);
        QCOMPARE(sync.getPosition().time, 0.0f);

        sync.loadLyrics(makeLyrics());
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);
        sync.syncNow();
        QCOMPARE(sync.getPosition().time, 0.0f);
        QCOMPARE(sync.getPosition().lineIndex, -1);

        // The new song still seeks normally.
        sync.seek(1.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 1);
        QCOMPARE(sync.getPosition().time, 1.5f);
    }

    void mutatorsRunOnTheObjectsOwnThread()
    {
        LyricsSync sync(nullptr);
        QCOMPARE(QThread::currentThread(), sync.thread());

        // loadLyrics, seek and clear each assert thread affinity, so reaching
        // the end of this on a debug build is the check: a mutator called from
        // a worker would abort rather than race the timer silently.
        sync.loadLyrics(makeLyrics());
        sync.seek(0.75f);
        sync.syncNow();
        sync.clear();
        sync.loadLyrics(makeLyrics());
        sync.jumpToLine(1);
        sync.syncNow();

        QCOMPARE(sync.getState(), LyricsSyncState::Paused);
        // Line 1 starts at exactly 1.0 s, and line 0's span is inclusive of
        // its end, so the boundary resolves to line 0. That is pre-existing
        // findLineIndex behaviour; the seed carrying the request is what this
        // covers, including a seed that happens to be a boundary value.
        QCOMPARE(sync.getPosition().time, 1.0f);
        QCOMPARE(sync.getPosition().lineIndex, 0);
    }

    void emptyLoadIsTerminal()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(LyricsData{});
        QCOMPARE(sync.getState(), LyricsSyncState::Error);
        sync.seek(1.0f);
        QCOMPARE(sync.getState(), LyricsSyncState::Error);
    }

    void getTimeRangeClampsIndexPastEndOfShortSong()
    {
        const LyricsData data = makeLyrics();
        QCOMPARE(data.lineCount(), size_t{2});

        // 99 - 2 is 97, which the old low-end-only clamp handed straight to
        // lines[] on a two-line song.
        const auto past = data.getTimeRange(99, 2);
        QCOMPARE(past.first, 0.0f);
        QCOMPARE(past.second, 2.0f);

        // A near-SIZE_MAX index used to wrap lineIndex + contextLines + 1 to
        // zero, which made the `endIdx - 1` subscript SIZE_MAX.
        const auto wrapped = data.getTimeRange(std::numeric_limits<size_t>::max(), 2);
        QCOMPARE(wrapped.first, 0.0f);
        QCOMPARE(wrapped.second, 2.0f);

        // Exactly one past the end is still out of range, and a huge context
        // must not overflow the inclusive end index either.
        const auto adjacent = data.getTimeRange(2, 0);
        QCOMPARE(adjacent.first, 0.0f);
        QCOMPARE(adjacent.second, 2.0f);
        const auto wide = data.getTimeRange(0, std::numeric_limits<size_t>::max());
        QCOMPARE(wide.first, 0.0f);
        QCOMPARE(wide.second, 2.0f);

        // In-range behaviour is unchanged: line 1 with one line of context is
        // the whole song.
        const auto centred = data.getTimeRange(1, 1);
        QCOMPARE(centred.first, 0.0f);
        QCOMPARE(centred.second, 2.0f);
    }

    void getTimeRangeWithZeroContextCoversOnlyTheLine()
    {
        const LyricsData data = makeLyrics();

        // contextLines == 0 and lineIndex == 0 is the tightest case for the
        // inclusive end index: it stays at 0 instead of stepping back one.
        const auto first = data.getTimeRange(0, 0);
        QCOMPARE(first.first, 0.0f);
        QCOMPARE(first.second, 1.0f);

        const auto last = data.getTimeRange(1, 0);
        QCOMPARE(last.first, 1.0f);
        QCOMPARE(last.second, 2.0f);
    }

    void getTimeRangeOnEmptyLyricsIsDegenerate()
    {
        const LyricsData data;
        QVERIFY(data.empty());

        const auto near = data.getTimeRange(0, 0);
        QCOMPARE(near.first, 0.0f);
        QCOMPARE(near.second, 0.0f);

        const auto far = data.getTimeRange(99, 2);
        QCOMPARE(far.first, 0.0f);
        QCOMPARE(far.second, 0.0f);
    }

    void contextQueriesStayBoundedAfterShorterSongLoad()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyricsOfCount(8));
        sync.seek(7.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 7);
        QCOMPARE(sync.getContextLines(2, 2).size(), size_t{3});
        QCOMPARE(sync.getUpcomingLines(2).size(), size_t{0});

        // A shorter song replaces lyrics_ underneath the cached lineIndex.
        sync.loadLyrics(makeLyrics());
        QCOMPARE(sync.getPosition().lineIndex, -1);

        // Every path that replaces lyrics_ resets currentPos_, so there is no
        // public way to hold a stale index today; the bounded query is what
        // must hold if that ever changes. The before/after pointers must come
        // from the new two-line song, never from the discarded eight-line one.
        QVERIFY(sync.getContextLines(2, 2).empty());
        const auto upcoming = sync.getUpcomingLines(3);
        QCOMPARE(upcoming.size(), size_t{1});
        QVERIFY(upcoming.front() == &sync.getLyrics().lines[1]);
        QCOMPARE(upcoming.front()->text, std::string("again"));

        // The QML surface clamps its own cached index into range and reads the
        // same two lines; with no active line it anchors to the first one.
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        {
            qml_bridge::LyricsBridge bridge;
            QCOMPARE(bridge.currentLineIndex(), -1);
            QCOMPARE(bridge.getContextLines(2, 2).size(), 2);
            QCOMPARE(bridge.getUpcomingLines(3).size(), 2);
        }
    }

    void unsyncedLinesWithEmptyWordsAreQueryable()
    {
        LyricsData data;
        data.source = "txt";
        data.isSynced = false;

        // `words` is documented as "may be empty for unsynced"; nothing may
        // index into it when it is.
        LyricsLine unsynced;
        unsynced.text = "no timing here";
        unsynced.isSynced = false;
        QCOMPARE(unsynced.words.size(), size_t{0});
        QCOMPARE(unsynced.getActiveWordIndex(0.0f), -1);
        QCOMPARE(unsynced.getActiveWordIndex(1.0f), -1);
        data.lines.push_back(unsynced);

        LyricsSync sync(nullptr);
        sync.loadLyrics(data);
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);
        QCOMPARE(sync.getLyrics().lines[0].words.size(), size_t{0});

        sync.seek(0.0f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 0);
        QCOMPARE(sync.getPosition().wordIndex, -1);
        QCOMPARE(sync.getPosition().wordProgress, 0.0f);
        QCOMPARE(sync.getContextLines(1, 1).size(), size_t{1});
        QCOMPARE(sync.getUpcomingLines(2).size(), size_t{0});

        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        {
            qml_bridge::LyricsBridge bridge;
            QCOMPARE(bridge.getLine(0).value(QStringLiteral("text")).toString(),
                     QStringLiteral("no timing here"));
            QVERIFY(bridge.getLine(1).isEmpty());
        }
    }

    void malformedInvertedTimingStaysSane()
    {
        LyricsData data;
        data.source = "suno";
        data.isSynced = true;

        // Remote data with endTime before startTime: reported verbatim, never
        // used as a divisor and never wrapped into an index.
        LyricsLine inverted;
        inverted.text = "inverted";
        inverted.startTime = 2.0f;
        inverted.endTime = 1.0f;
        inverted.isSynced = true;
        inverted.words.push_back({"skewed", 1.0f, 0.5f, 1.0f});
        data.lines.push_back(inverted);

        LyricsLine wellFormed;
        wellFormed.text = "well formed";
        wellFormed.startTime = 3.0f;
        wellFormed.endTime = 4.0f;
        wellFormed.isSynced = true;
        data.lines.push_back(wellFormed);

        QCOMPARE(inverted.words[0].getProgress(0.75f), 0.0f);
        const auto own = data.getTimeRange(0, 0);
        QCOMPARE(own.first, 2.0f);
        QCOMPARE(own.second, 1.0f);
        const auto widened = data.getTimeRange(0, 1);
        QCOMPARE(widened.first, 2.0f);
        QCOMPARE(widened.second, 4.0f);

        LyricsSync sync(nullptr);
        sync.loadLyrics(data);
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);

        sync.seek(3.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 1);
        QCOMPARE(sync.getPosition().lineProgress, 0.5f);
        QCOMPARE(sync.getContextLines(1, 1).size(), size_t{2});
        QCOMPARE(sync.getUpcomingLines(1).size(), size_t{0});

        // The inverted line is never reported as the active one, and its
        // zero-width span leaves lineProgress untouched rather than negative.
        sync.seek(2.0f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 0);
        QCOMPARE(sync.getPosition().lineProgress, 0.0f);
    }

    void downloaderSignalsAfterExistingFileJumps()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto filePath = directory.filePath(QStringLiteral("CapturedClip.mp3"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        DownloadPathGuard pathGuard(CONFIG.suno().downloadPath);
        CONFIG.suno().downloadPath = directory.path().toStdString();

        QTemporaryDir sessionDirectory;
        QVERIFY(sessionDirectory.isValid());
        AudioEngine audio(
            fs::path(sessionDirectory.path().toStdString()) / "last_session.m3u");
        QVERIFY(audio.init());
        SunoDatabase database;
        QVERIFY(database.init(":memory:"));
        auto* network = new QNetworkAccessManager;
        SunoDownloader downloader(nullptr, database, &audio, network);

        QSignalSpy ready(&downloader, &SunoDownloader::playbackReady);
        bool currentAtReady = false;
        QObject::connect(&downloader, &SunoDownloader::playbackReady,
                         &downloader, [&currentAtReady, &audio](const QString&) {
                             currentAtReady = audio.playlist().currentItem().has_value();
                         });

        SunoClip clip;
        clip.id = "clip-ready";
        clip.title = "CapturedClip";
        clip.status = "complete";
        clip.media_urls = {{"https://audiopipe.suno.ai/clip-ready.mp3", "mp3",
                             "streaming", ""}};
        downloader.downloadAndPlay(clip);

        QCOMPARE(ready.count(), 1);
        QVERIFY(currentAtReady);
        QCOMPARE(ready.takeFirst().at(0).toString(), QStringLiteral("clip-ready"));
    }

    void searchAndExportsOwnedLyrics()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(makeLyrics());
        sync.seek(0.5f);
        sync.syncNow();

        bridge.setSearchQuery(QStringLiteral("hello"));
        QCOMPARE(bridge.searchResults().size(), 1);
        QCOMPARE(bridge.getUpcomingLines(1).size(), 1);
        QCOMPARE(bridge.getContextLines(1, 1).size(), 2);
        QCOMPARE(bridge.getLine(0).value(QStringLiteral("text")).toString(),
                 QStringLiteral("hello"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString srtPath = directory.filePath(QStringLiteral("lyrics.srt"));
        const QString lrcPath = directory.filePath(QStringLiteral("lyrics.lrc"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToSrt(srtPath);
        bridge.exportToLrc(lrcPath);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 2000);
        QCOMPARE(failed.count(), 0);

        QFile srt(srtPath);
        QVERIFY(srt.open(QIODevice::ReadOnly));
        const QByteArray srtContents = srt.readAll();
        QVERIFY(srtContents.contains("00:00:00,000 --> 00:00:01,000"));
        QVERIFY(srtContents.contains("hello"));

        QFile lrc(lrcPath);
        QVERIFY(lrc.open(QIODevice::ReadOnly));
        const QByteArray lrcContents = lrc.readAll();
        QVERIFY(lrcContents.contains("[ti:Test]"));
        QVERIFY(lrcContents.contains("[00:00.00]hello"));

        bridge.exportToSrt(QString());
        QCOMPARE(finished.count(), 2);
        QCOMPARE(failed.count(), 1);
    }

    void lazyBridgeReceivesUpdates()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        {
            qml_bridge::LyricsBridge bridge;
            QSignalSpy lyricsSpy(&bridge, &qml_bridge::LyricsBridge::lyricsChanged);
            QSignalSpy positionSpy(&bridge, &qml_bridge::LyricsBridge::positionChanged);

            sync.loadLyrics(makeLyrics());
            sync.seek(0.75f);
            sync.syncNow();

            QVERIFY(bridge.hasLyrics());
            QCOMPARE(bridge.currentLineIndex(), 0);
            QCOMPARE(bridge.currentWordIndex(), 1);
            QVERIFY(lyricsSpy.count() > 0);
            QVERIFY(positionSpy.count() > 0);
        }
        QVERIFY(!qml_bridge::LyricsBridge::instance());
    }
};

int runTestLyricsPipeline(int argc, char** argv)
{
    TestLyricsPipeline test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_LyricsPipeline.moc"
