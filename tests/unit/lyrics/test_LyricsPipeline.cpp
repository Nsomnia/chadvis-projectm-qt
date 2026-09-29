#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

QByteArray readExported(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QString describeIndices(const std::vector<int>& indices)
{
    QStringList parts;
    for (const int index : indices) {
        parts << QString::number(index);
    }
    return parts.join(QLatin1Char(','));
}

/// The window the QML surface returns, as the indices it puts in each map.
std::vector<int> bridgeWindow(const qml_bridge::LyricsBridge& bridge, int before, int after)
{
    std::vector<int> indices;
    for (const auto& entry : bridge.getContextLines(before, after)) {
        // toMap(), not QVariant::value(QString) -- that overload is gone in Qt 6.
        indices.push_back(entry.toMap().value(QStringLiteral("index")).toInt());
    }
    return indices;
}

/// The upcoming window the QML surface returns, same shape as bridgeWindow().
std::vector<int> bridgeUpcoming(const qml_bridge::LyricsBridge& bridge, int count)
{
    std::vector<int> indices;
    for (const auto& entry : bridge.getUpcomingLines(count)) {
        indices.push_back(entry.toMap().value(QStringLiteral("index")).toInt());
    }
    return indices;
}

/// The texts the QML surface returns, so a test can prove each index carries the
/// line it names rather than only proving the ordering.
QString describeUpcomingTexts(const qml_bridge::LyricsBridge& bridge, int count)
{
    QStringList parts;
    for (const auto& entry : bridge.getUpcomingLines(count)) {
        parts << entry.toMap().value(QStringLiteral("text")).toString();
    }
    return parts.join(QLatin1Char(','));
}

/// The upcoming window from LyricsSync, with its line pointers mapped back to
/// indices, for the same comparison syncWindow() makes for the other query.
std::vector<int> syncUpcoming(const LyricsSync& sync, size_t count)
{
    const auto& lines = sync.getLyrics().lines;
    std::vector<int> indices;
    for (const auto* line : sync.getUpcomingLines(count)) {
        indices.push_back(static_cast<int>(line - lines.data()));
    }
    return indices;
}

/// Drive the bridge's cached line index through the real position wiring.
///
/// currentLineIndex_ is private and no public path produces an out-of-range
/// value -- the sync only ever reports a real line, and loadLyrics resets the
/// cache -- so the only way to exercise the values the window arithmetic has to
/// survive is to hand the bridge a position. Emitting the sync's own signal is
/// the exact path the bridge is connected to, so this is not a meta-object
/// reach-in, and it proves the connection is live while it is at it.
void forceBridgeLineIndex(LyricsSync& sync, qml_bridge::LyricsBridge& bridge, int lineIndex)
{
    LyricsSyncPosition position;
    position.lineIndex = lineIndex;
    sync.positionChanged.emitSignal(position);
    QCOMPARE(bridge.currentLineIndex(), lineIndex);
}

/// The same window from LyricsSync, with its line pointers mapped back to indices.
/// Pointer arithmetic, not std::distance: the two arguments are a vector
/// iterator and a raw pointer, which no std::distance overload accepts.
std::vector<int> syncWindow(const LyricsSync& sync, size_t before, size_t after)
{
    const auto& lines = sync.getLyrics().lines;
    std::vector<int> indices;
    for (const auto* line : sync.getContextLines(before, after)) {
        indices.push_back(static_cast<int>(line - lines.data()));
    }
    return indices;
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

    void checkedIndexIsTheSingleBoundsGate()
    {
        const LyricsData data = makeLyrics();
        QCOMPARE(data.lines.size(), size_t{2});

        // Happy path: a valid index survives unchanged.
        QCOMPARE(checkedIndex(data.lines, 0), std::optional<size_t>{0});
        QCOMPARE(checkedIndex(data.lines, 1), std::optional<size_t>{1});

        // The lower bound. -1 is the documented "no active line" sentinel and
        // must be refused, not reinterpreted as a huge subscript.
        QVERIFY(!checkedIndex(data.lines, -1).has_value());
        QVERIFY(!checkedIndex(data.lines, std::numeric_limits<int>::min()).has_value());

        // The upper bound that the old hand-rolled checks missed: one past the
        // end is out, and so is anything further.
        QVERIFY(!checkedIndex(data.lines, 2).has_value());
        QVERIFY(!checkedIndex(data.lines, 99).has_value());

        // The near-SIZE_MAX wrap from getTimeRange. A size_t end index of
        // SIZE_MAX narrows to -1 as an int, so the sign check is what has to
        // catch it. A gate that only compared against size() would have
        // compared -1 against 2 and waved it through as a subscript.
        static_assert(static_cast<int>(std::numeric_limits<size_t>::max()) == -1);
        QVERIFY(!checkedIndex(data.lines, static_cast<int>(std::numeric_limits<size_t>::max()))
                     .has_value());
        // The same value with the top bit cleared stays a large positive int and
        // is caught by the upper bound instead, so neither bound is load-bearing
        // on its own.
        QVERIFY(!checkedIndex(data.lines, std::numeric_limits<int>::max()).has_value());

        // An empty container has no valid index at all, including 0. Remote
        // lyrics arrive as an empty vector whenever the payload has no usable
        // words, so this is a normal state, not an error.
        const LyricsData empty;
        QVERIFY(empty.lines.empty());
        QVERIFY(!checkedIndex(empty.lines, 0).has_value());
        QVERIFY(!checkedIndex(empty.lines, -1).has_value());
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

    void contextLinesStopAtTheSongEndOnAnOversizedWindow()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyricsOfCount(8));
        sync.seek(3.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 3);

        // The window is caller-supplied unsigned arithmetic, exactly like the
        // remote data it is read alongside. `for (i = 1; i <= after; ++i)` never
        // terminates when `after` is SIZE_MAX, and the same counter spun for
        // two billion iterations on the way down. Both now stop at the first
        // and last line, so the answer is the whole song and it returns at all.
        const auto everything = sync.getContextLines(std::numeric_limits<size_t>::max(),
                                                     std::numeric_limits<size_t>::max());
        QCOMPARE(everything.size(), size_t{8});
        for (size_t i = 0; i < everything.size(); ++i) {
            QVERIFY(everything[i] == &sync.getLyrics().lines[i]);
        }
        // Order is preserved: furthest-before first, then the current line,
        // then forwards. The "before" walk still counts down.
        QCOMPARE(everything[0]->text, std::string("line0"));
        QCOMPARE(everything[3]->text, std::string("line3"));
        QCOMPARE(everything.back()->text, std::string("line7"));

        // A zero window is just the current line, from either direction.
        const auto alone = sync.getContextLines(0, 0);
        QCOMPARE(alone.size(), size_t{1});
        QVERIFY(alone.front() == &sync.getLyrics().lines[3]);
    }

    void staleLineIndexIsRefusedByTheGate()
    {
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyricsOfCount(8));
        sync.seek(7.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 7);

        // The shorter song replaces lyrics_ under the cached index.
        sync.loadLyrics(makeLyrics());
        QCOMPARE(sync.getPosition().lineIndex, -1);

        // loadLyrics and clear both reset currentPos_, so there is no public
        // way to leave the cache stale today and the reset is the real
        // protection. Assert the gate anyway, because it -- not the reset -- is
        // what bounds the subscripts: an index of 7 is exactly what a stale
        // cache carried from the eight-line song would hold, and it must be
        // refused against this two-line one rather than handed to lines[].
        QCOMPARE(sync.getLyrics().lines.size(), size_t{2});
        QVERIFY(!checkedIndex(sync.getLyrics().lines, 7).has_value());
        // Index 1 is the last real line and must still resolve, so the check
        // above is a bound and not a blanket refusal.
        QCOMPARE(checkedIndex(sync.getLyrics().lines, 1), std::optional<size_t>{1});
        QVERIFY(!checkedIndex(sync.getLyrics().lines, 2).has_value());
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

    void bridgeContextLinesMatchLyricsSyncExceptAtTheAnchor()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);

        // The bridge is built *after* the lyrics are loaded, which is what a
        // lazily instantiated QML singleton looks like when the user opens a
        // page after playback already started: currentLineIndex_ is still -1
        // while the sync is happily reporting a real line.
        sync.loadLyrics(makeLyricsOfCount(5));
        qml_bridge::LyricsBridge bridge;

        // The one documented divergence, asserted first because it is the whole
        // reason the bridge cannot delegate. With no active line LyricsSync
        // refuses to invent a centre and returns nothing; the QML surface
        // anchors on the first line so a panel is never blank. If someone
        // re-inlines the window walk and drops that anchor, this fails.
        QCOMPARE(sync.getPosition().lineIndex, -1);
        QVERIFY(sync.getContextLines(2, 2).empty());
        QCOMPARE(describeIndices(bridgeWindow(bridge, 0, 0)), QStringLiteral("0"));
        QCOMPARE(describeIndices(bridgeWindow(bridge, 1, 1)), QStringLiteral("0,1"));
        QCOMPARE(describeIndices(bridgeWindow(bridge, 2, 2)), QStringLiteral("0,1,2"));
        QCOMPARE(describeIndices(bridgeWindow(bridge, 99, 99)), QStringLiteral("0,1,2,3,4"));

        // From here the bridge's cache tracks the sync's -- positionChanged is
        // emitted synchronously by syncNow() -- so the two queries must produce
        // identical windows. Any re-inline in the bridge that shifts a single
        // index, changes the ordering, or changes the clipping fails here.
        const auto agreeAt = [&sync, &bridge](f32 time, int lineIndex, int before, int after) {
            sync.seek(time);
            sync.syncNow();
            QCOMPARE(sync.getPosition().lineIndex, lineIndex);
            QCOMPARE(bridge.currentLineIndex(), lineIndex);
            QCOMPARE(describeIndices(bridgeWindow(bridge, before, after)),
                     describeIndices(syncWindow(sync, static_cast<size_t>(before),
                                                static_cast<size_t>(after))));
        };

        // An oversized window is the case that used to overflow: the old
        // `center + after + 1` wrapped `end` negative for any after near
        // INT_MAX, so the bridge silently returned an empty list where LyricsSync
        // returned the whole song.
        const int huge = std::numeric_limits<int>::max();

        // First line -- the window is clipped at the start of the song.
        agreeAt(0.5f, 0, 0, 0);
        agreeAt(0.5f, 0, 2, 2);
        agreeAt(0.5f, 0, huge, huge);
        // Middle line, each side independently.
        agreeAt(2.5f, 2, 2, 2);
        agreeAt(2.5f, 2, 0, 0);
        agreeAt(2.5f, 2, 0, 1);
        agreeAt(2.5f, 2, 1, 0);
        agreeAt(2.5f, 2, huge, huge);
        // Last line -- clipped at the end.
        agreeAt(4.5f, 4, 0, 0);
        agreeAt(4.5f, 4, 2, 2);
        agreeAt(4.5f, 4, huge, huge);
        // Past the end of the song: findLineIndex falls back to the closest
        // line before the time, so this is a last-line centre, not a stale one.
        agreeAt(99.0f, 4, 2, 2);

        // Negative windows are the bridge's own domain: QML can pass them and
        // LyricsSync's size_t signature cannot express one, so only the bridge's
        // answer is asserted here. Each side is clamped to zero independently,
        // and a window negative on *both* sides is refused outright.
        sync.seek(2.5f);
        sync.syncNow();
        QCOMPARE(describeIndices(bridgeWindow(bridge, -1, 2)), QStringLiteral("2,3,4"));
        QCOMPARE(describeIndices(bridgeWindow(bridge, 2, -1)), QStringLiteral("0,1,2"));
        QVERIFY(bridge.getContextLines(-1, -1).isEmpty());
    }

    void upcomingLinesStopAtTheSongEndOnAnOversizedCount()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        // Built before any position, which is the real state of a lazily
        // constructed QML singleton: the bridge starts on -1 and stays there
        // until the transport reports a line. Both the unset and the set anchor
        // are exercised below, because they take different code paths.
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(makeLyricsOfCount(5));
        QCOMPARE(sync.getLyrics().lines.size(), size_t{5});
        QCOMPARE(bridge.currentLineIndex(), -1);

        // `count` arrives from QML as a signed int, so a binding can hand this
        // surface anything up to INT_MAX. The window used to be `start + count`,
        // which overflows for the first-line and middle anchors below: the sum
        // wrapped negative, the loop never ran, and the caller got an empty list
        // where the rest of the song is the answer. The mirror of
        // contextLinesStopAtTheSongEndOnAnOversizedWindow on this side.
        const int huge = std::numeric_limits<int>::max();

        // Unset anchor: the whole song is still to come, starting at line 0.
        // This is the case a panel opened before playback reaches, and it does
        // not overflow (0 + INT_MAX is representable) -- asserted so the fix
        // does not change it while fixing its neighbours.
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 1)), QStringLiteral("0"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 3)), QStringLiteral("0,1,2"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, huge)),
                 QStringLiteral("0,1,2,3,4"));

        // First line: the window is the rest of the song. Overflowed to an empty
        // list before.
        sync.seek(0.5f);
        sync.syncNow();
        QCOMPARE(bridge.currentLineIndex(), 0);
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 1)), QStringLiteral("1"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 4)), QStringLiteral("1,2,3,4"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, huge)),
                 QStringLiteral("1,2,3,4"));
        QCOMPARE(describeUpcomingTexts(bridge, huge),
                 QStringLiteral("line1,line2,line3,line4"));

        // Middle line: the two lines after it. Overflowed to an empty list too.
        sync.seek(2.5f);
        sync.syncNow();
        QCOMPARE(bridge.currentLineIndex(), 2);
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 1)), QStringLiteral("3"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 2)), QStringLiteral("3,4"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 99)), QStringLiteral("3,4"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, huge)), QStringLiteral("3,4"));
        QCOMPARE(describeUpcomingTexts(bridge, huge), QStringLiteral("line3,line4"));

        // Last line: the correct answer really is an empty list, so this is the
        // boundary the two above are measured against rather than a regression
        // in its own right. Asserted anyway, because a rewrite that clipped with
        // `std::max` on the wrong side would start returning the last line here.
        sync.seek(4.5f);
        sync.syncNow();
        QCOMPARE(bridge.currentLineIndex(), 4);
        QVERIFY(bridgeUpcoming(bridge, 1).empty());
        QVERIFY(bridgeUpcoming(bridge, huge).empty());
    }

    void upcomingLinesClampANegativeCount()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(makeLyricsOfCount(5));
        sync.seek(2.5f);
        sync.syncNow();
        QCOMPARE(bridge.currentLineIndex(), 2);

        // `count` is a signed int from QML, so a QML expression can hand this
        // surface a negative number. There is one window here rather than a
        // pair, so the per-side `std::max(0, .)` clamp getContextLines applies
        // degenerates to the count <= 0 refusal: with no "current line" to fall
        // back on, a non-positive request is nothing at all, which is exactly
        // how getContextLines(-1, -1) is refused. That behaviour is pre-existing
        // and unchanged; what the rewrite adds is that the window itself is
        // provably non-negative, so no negative size can reach the loop even if
        // the refusal above were removed.
        for (const int count : {-1, -2, std::numeric_limits<int>::min()}) {
            QCOMPARE(describeIndices(bridgeUpcoming(bridge, count)), QString());
        }
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 0)), QString());

        // The clamp is on the window, not on the answer: a valid count on either
        // side of the refused range is unaffected, so a negative count is
        // refused rather than saturating to "the rest of the song".
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 1)), QStringLiteral("3"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 2)), QStringLiteral("3,4"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, std::numeric_limits<int>::max())),
                 QStringLiteral("3,4"));

        // Same at the ends of the song, where a window of one or zero.
        forceBridgeLineIndex(sync, bridge, 0);
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, -1)), QString());
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 1)), QStringLiteral("1"));
        forceBridgeLineIndex(sync, bridge, 4);
        QVERIFY(bridgeUpcoming(bridge, -1).empty());
        QVERIFY(bridgeUpcoming(bridge, 1).empty());
    }

    void upcomingLinesRefuseAnAnchorPastTheEnd()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(makeLyricsOfCount(5));

        const int huge = std::numeric_limits<int>::max();

        // Nothing is upcoming from the last line, for any count.
        forceBridgeLineIndex(sync, bridge, 4);
        QVERIFY(bridgeUpcoming(bridge, 1).empty());
        QVERIFY(bridgeUpcoming(bridge, huge).empty());

        // One past the end, and a stale anchor into a longer song: both are the
        // same answer, and both are reachable by a cached index that outlived
        // the lines it names.
        forceBridgeLineIndex(sync, bridge, 5);
        QVERIFY(bridgeUpcoming(bridge, huge).empty());
        forceBridgeLineIndex(sync, bridge, 99);
        QVERIFY(bridgeUpcoming(bridge, 1).empty());
        QVERIFY(bridgeUpcoming(bridge, huge).empty());

        // INT_MAX is the one value that made the old `currentLineIndex_ + 1`
        // signed overflow. It wrapped to INT_MIN, std::max() pulled the start
        // back to 0, and the query answered with the *whole song* for a line
        // that cannot exist. Under a UBSan build that addition traps here
        // instead, which is the same defect reported earlier in the program.
        forceBridgeLineIndex(sync, bridge, huge);
        QCOMPARE(bridge.currentLineIndex(), huge);
        QVERIFY(bridgeUpcoming(bridge, 1).empty());
        QVERIFY(bridgeUpcoming(bridge, huge).empty());
    }

    void bridgeAndSyncUpcomingLinesAgreeExceptAtTheAnchor()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        sync.loadLyrics(makeLyricsOfCount(5));
        qml_bridge::LyricsBridge bridge;
        QCOMPARE(sync.getPosition().lineIndex, -1);
        QCOMPARE(bridge.currentLineIndex(), -1);

        // The one divergence, and it runs the OPPOSITE way to getContextLines'
        // anchor difference. There the bridge is the forgiving one: it clamps
        // -1 to 0 and includes line 0 as the current line, while LyricsSync
        // returns nothing. Here LyricsSync is the forgiving one -- it reads
        // "no active line" as "line 0 is current" and therefore starts *after*
        // it -- while the bridge has always read the same unset index as "the
        // whole song is still to come" and includes line 0 itself. Same cached
        // index, same count, different lines.
        QCOMPARE(describeIndices(syncUpcoming(sync, 2)), QStringLiteral("1,2"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 2)), QStringLiteral("0,1"));
        QCOMPARE(describeIndices(syncUpcoming(sync, 99)), QStringLiteral("1,2,3,4"));
        QCOMPARE(describeIndices(bridgeUpcoming(bridge, 99)),
                 QStringLiteral("0,1,2,3,4"));

        // Everywhere else they agree exactly. In particular the clip rule does
        // agree, which was not obvious: LyricsSync's loop has no early exit, so
        // an oversized count simply fails to push, and the number it returns is
        // min(count, lines left) -- the same distance window the bridge builds.
        const auto agreeAt = [&sync, &bridge](f32 time, int lineIndex, int count) {
            sync.seek(time);
            sync.syncNow();
            QCOMPARE(sync.getPosition().lineIndex, lineIndex);
            QCOMPARE(bridge.currentLineIndex(), lineIndex);
            QCOMPARE(describeIndices(bridgeUpcoming(bridge, count)),
                     describeIndices(syncUpcoming(sync, static_cast<size_t>(count))));
        };

        agreeAt(0.5f, 0, 1);
        agreeAt(0.5f, 0, 4);
        agreeAt(0.5f, 0, 99);
        agreeAt(2.5f, 2, 1);
        agreeAt(2.5f, 2, 2);
        agreeAt(2.5f, 2, 99);
        // Last line: nothing upcoming on either side.
        agreeAt(4.5f, 4, 1);
        agreeAt(4.5f, 4, 99);
        // Past the end of the song findLineIndex falls back to the closest line
        // before the time, so this is a last-line centre, not a stale one.
        agreeAt(99.0f, 4, 3);

        // A stale anchor is bounded the same way on both sides, and the second
        // half of why this pair is not one implementation is the count: the
        // bridge's is a signed int, so a QML binding can send it a negative
        // number, and LyricsSync's size_t cannot express that request at all.
        forceBridgeLineIndex(sync, bridge, 99);
        QVERIFY(bridgeUpcoming(bridge, 2).empty());
        QCOMPARE(describeIndices(syncUpcoming(sync, 2)), QString());

        // The signatures themselves, pinned because the whole point of keeping
        // two implementations is that these two are not interchangeable.
        static_assert(std::is_same_v<decltype(&qml_bridge::LyricsBridge::getUpcomingLines),
                                     QVariantList (qml_bridge::LyricsBridge::*)(int) const>);
        static_assert(std::is_same_v<decltype(&LyricsSync::getUpcomingLines),
                                     std::vector<const LyricsLine*> (LyricsSync::*)(size_t) const>);
    }

    void upcomingLinesYieldNothingWithoutLyricsOrASync()
    {
        // A sync that never loaded anything. `lines` is empty here, and that is
        // also what a remote payload with no usable words leaves behind, so the
        // empty container is a normal state rather than an error path.
        LyricsSync emptySync(nullptr);
        QCOMPARE(emptySync.getLyrics().lines.size(), size_t{0});
        QCOMPARE(emptySync.getPosition().lineIndex, -1);
        qml_bridge::LyricsBridge::setLyricsSync(&emptySync);
        {
            qml_bridge::LyricsBridge bridge;
            QVERIFY(!bridge.hasLyrics());
            QVERIFY(bridge.lines().isEmpty());
            // Every count, including the oversized one, and with an anchor set.
            // The old code built the window before it had proved anything, so
            // `last - start + 1` is the quantity that must not be computed here
            // at all; the empty container is refused ahead of the arithmetic.
            QVERIFY(bridge.getUpcomingLines(1).isEmpty());
            QVERIFY(bridge.getUpcomingLines(std::numeric_limits<int>::max()).isEmpty());
            forceBridgeLineIndex(emptySync, bridge, std::numeric_limits<int>::max());
            QVERIFY(bridge.getUpcomingLines(std::numeric_limits<int>::max()).isEmpty());
        }

        // A failed load reaches the same state through the public path.
        LyricsSync failed(nullptr);
        failed.loadLyrics(LyricsData{});
        QCOMPARE(failed.getState(), LyricsSyncState::Error);
        QCOMPARE(failed.getLyrics().lines.size(), size_t{0});
        qml_bridge::LyricsBridge::setLyricsSync(&failed);
        {
            qml_bridge::LyricsBridge bridge;
            QVERIFY(!bridge.hasLyrics());
            QVERIFY(bridge.getUpcomingLines(1).isEmpty());
            QVERIFY(bridge.getUpcomingLines(std::numeric_limits<int>::max()).isEmpty());
        }

        // No sync attached at all, which is a real state: registration sets the
        // static before any QML loads, so a bridge built before that exists.
        LyricsSync live(nullptr);
        live.loadLyrics(makeLyricsOfCount(5));
        live.seek(2.5f);
        live.syncNow();
        QCOMPARE(live.getPosition().lineIndex, 2);
        qml_bridge::LyricsBridge::setLyricsSync(nullptr);
        {
            qml_bridge::LyricsBridge unattached;
            QVERIFY(!unattached.hasLyrics());
            QCOMPARE(unattached.currentLineIndex(), -1);
            QVERIFY(unattached.getUpcomingLines(1).isEmpty());
            QVERIFY(unattached.getUpcomingLines(-1).isEmpty());
            QVERIFY(unattached.getUpcomingLines(std::numeric_limits<int>::max())
                        .isEmpty());
            // The detached bridge neither advanced nor cleared the sync it
            // cannot see.
            QCOMPARE(live.getPosition().lineIndex, 2);
        }
    }

    void contextLinesWithoutASyncYieldNothing()
    {
        // A real sync with a live position, deliberately not attached: the
        // bridge must reach only the static. Registration sets it before any
        // QML loads, so a bridge built before that is a real state, and every
        // window query on it is an empty list rather than a null dereference.
        LyricsSync sync(nullptr);
        sync.loadLyrics(makeLyricsOfCount(5));
        sync.seek(2.5f);
        sync.syncNow();
        QCOMPARE(sync.getPosition().lineIndex, 2);
        qml_bridge::LyricsBridge::setLyricsSync(nullptr);

        qml_bridge::LyricsBridge unattached;
        QVERIFY(!unattached.hasLyrics());
        QCOMPARE(unattached.currentLineIndex(), -1);
        QVERIFY(unattached.lines().isEmpty());
        QVERIFY(unattached.getContextLines(2, 2).isEmpty());
        QVERIFY(unattached.getContextLines(0, 0).isEmpty());
        QVERIFY(unattached.getContextLines(99, 99).isEmpty());
        QVERIFY(unattached.getContextLines(-1, -1).isEmpty());
        QVERIFY(unattached.getUpcomingLines(3).isEmpty());

        // The detached bridge neither advanced nor cleared the sync it cannot see.
        QCOMPARE(sync.getPosition().lineIndex, 2);
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

    void bridgeExportsAreTheOnlySrtAndLrcFormatter()
    {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;

        // This is the shape of real remote lyrics: an empty spacer line, and an
        // aligned span whose endTime precedes its startTime. Both are the cases
        // that gave the retired second formatter in LyricsData something to get
        // wrong -- it emitted both verbatim. These assertions exist so the
        // exported bytes are pinned to this formatter, and so a second one
        // reappearing has to match it rather than drift from it.
        LyricsData data = makeLyrics();

        LyricsLine spacer;
        spacer.text = "";
        spacer.startTime = 2.0f;
        spacer.endTime = 2.0f;
        spacer.isSynced = true;

        LyricsLine inverted;
        inverted.text = "backwards";
        inverted.startTime = 3.0f;
        inverted.endTime = 2.0f;
        inverted.isSynced = true;

        data.lines.push_back(spacer);
        data.lines.push_back(inverted);

        sync.loadLyrics(data);
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);

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

        // Four source lines, three entries: the empty one is skipped and does
        // not consume an index, so numbering stays consecutive. The inverted
        // span is widened to a whole second instead of being written backwards.
        const QByteArray srt = readExported(srtPath);
        QVERIFY(!srt.isEmpty());
        QCOMPARE(srt.count("-->"), 3);
        QVERIFY(srt.contains("1\n00:00:00,000 --> 00:00:01,000\nhello\n\n"));
        QVERIFY(srt.contains("2\n00:00:01,000 --> 00:00:02,000\nagain\n\n"));
        QVERIFY(srt.contains("3\n00:00:03,000 --> 00:00:04,000\nbackwards\n\n"));
        QVERIFY(!srt.contains("4\n"));
        QVERIFY(!srt.contains("00:00:03,000 --> 00:00:02,000"));

        // LRC carries the [ti:]/[ar:] headers, which the retired formatter never
        // wrote, and the same three lines.
        const QByteArray lrc = readExported(lrcPath);
        QVERIFY(!lrc.isEmpty());
        QVERIFY(lrc.startsWith("[ti:Test]\n[ar:Artist]\n"));
        QCOMPARE(lrc.count('\n'), 5);
        QVERIFY(lrc.contains("[00:00.00]hello\n"));
        QVERIFY(lrc.contains("[00:01.00]again\n"));
        QVERIFY(lrc.contains("[00:03.00]backwards\n"));
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
