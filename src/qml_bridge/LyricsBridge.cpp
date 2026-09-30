#include "LyricsBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"

#include <QSaveFile>

#include <algorithm>
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"

namespace qml_bridge {

namespace {

/// SRT writes milliseconds; LRC and ASS both write centiseconds.
constexpr vc::f32 kMillisecondsPerSecond = 1000.0f;
constexpr vc::f32 kCentisecondsPerSecond = 100.0f;

/// One message for every entry point that needs lyrics. The file writers and
/// the string builder all refuse the same way, and they must keep refusing the
/// same way: a caller that gets a different string from the string builder
/// than from exportToAss has no way to tell it is the same condition.
constexpr auto kNoLyricsMessage = "No lyrics are loaded.";

// ── SRT and LRC layout ──────────────────────────────────────────────────────
//
// The two formats with a single consumer, so they stay here. What they share
// with the ASS writer -- the seconds-to-whole-units conversion -- does not, and
// is `vc::LyricsExport::toTimeUnits`, one layer down: three copies of a rounding
// rule is three places for it to drift. Each call site passes its own scale; see
// the comment there for why the scale is a parameter and why the non-finite
// guard is the defect the helper exists for.
//
// formatSrtTime is gone: exportToSrt formats each end through
// formatSrtMilliseconds directly, because it has to widen an inverted span
// before formatting, so the seconds-taking wrapper had no caller left. The
// compiler said so.

QString formatSrtMilliseconds(const qint64 totalMs) {
    const qint64 hours = totalMs / 3600000;
    const qint64 minutes = (totalMs / 60000) % 60;
    const qint64 secs = (totalMs / 1000) % 60;
    const qint64 millis = totalMs % 1000;
    return QStringLiteral("%1:%2:%3,%4")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'))
            .arg(millis, 3, 10, QLatin1Char('0'));
}

QString formatLrcTime(const vc::f32 seconds) {
    const qint64 totalCentiseconds =
            vc::LyricsExport::toTimeUnits(seconds, kCentisecondsPerSecond);
    const qint64 minutes = totalCentiseconds / 6000;
    const qint64 secs = (totalCentiseconds / 100) % 60;
    const qint64 centis = totalCentiseconds % 100;
    return QStringLiteral("[%1:%2.%3]")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'))
            .arg(centis, 2, 10, QLatin1Char('0'));
}

bool writeExportFile(const QString& path, const QByteArray& contents, QString& error) {
    if (path.trimmed().isEmpty()) {
        error = QStringLiteral("An export path is required.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.write(contents) != contents.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

/// An LRC metadata value, for [ti:] and [ar:].
///
/// Same newline rule the ASS header escaper uses (now
/// `vc::LyricsExport`'s, in src/lyrics), and for the same reason: a record is
/// one line, so a newline in the value would end the tag early and leave the
/// remainder to be read as a record of its own.
///
/// LRC adds a second hazard that ASS does not have, because LRC defines no
/// escape syntax at all -- there is no way to write a literal `[`. A `[` in a
/// title is therefore not merely cosmetically ambiguous, it is re-read as the
/// start of a tag. This repository's own parser demonstrates it: fromLrc
/// (LyricsData.cpp) matches timestamps with an *unanchored* regex_search, so a
/// title of `Song [00:30] Live` exported as `[ti:Song [00:30] Live]` comes back
/// as a phantom lyric line at 30 seconds, with the trailing ` Live]` as its
/// text. Third-party players that parse [ti:]/[ar:] as tags would instead
/// truncate the value at the bracket.
///
/// So the brackets are *substituted* rather than escaped -- there is no escape
/// to use. U+FF3B and U+FF3D (fullwidth square brackets) are the substitution
/// because they are visually still brackets, so the title still reads
/// correctly, and they cannot match any LRC tag grammar. Dropping the
/// character instead would silently lose data, and a space would run two words
/// together.
///
/// Both ends are substituted, not just the opening one. Substituting only `[`
/// was measurably worse: it leaves the value visually unbalanced, and the
/// surviving ASCII `]` still gives a tag reader something to truncate at. With
/// both, the file's remaining ASCII brackets are exactly its real tag
/// delimiters, which is a checkable invariant rather than a hope.
QString lrcHeaderValue(const QString& value) {
    QString cleaned = value;
    cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
    cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
    cleaned.replace(QLatin1Char('['), QChar(0xFF3B)); // fullwidth [
    cleaned.replace(QLatin1Char(']'), QChar(0xFF3D)); // fullwidth ]
    return cleaned;
}


} // namespace

vc::LyricsSync* LyricsBridge::s_sync = nullptr;
vc::AudioEngine* LyricsBridge::s_engine = nullptr;
vc::LyricsSync* LyricsBridge::s_connectedSync = nullptr;
std::optional<std::size_t> LyricsBridge::s_positionConnection;
std::optional<std::size_t> LyricsBridge::s_stateConnection;

LyricsBridge::LyricsBridge(QObject* parent) : QObject(parent) {
    setInstance(this);
    connectSignals();
}

LyricsBridge::~LyricsBridge() {
    if (instance() != this) {
        return;
    }
    if (s_connectedSync && s_positionConnection) {
        s_connectedSync->positionChanged.disconnect(*s_positionConnection);
    }
    if (s_connectedSync && s_stateConnection) {
        s_connectedSync->stateChanged.disconnect(*s_stateConnection);
    }
    s_connectedSync = nullptr;
    s_positionConnection.reset();
    s_stateConnection.reset();
    s_sync = nullptr;
    s_engine = nullptr;
    setInstance(nullptr);
}

void LyricsBridge::setLyricsSync(vc::LyricsSync* sync) {
    s_sync = sync;
    connectSignals();
}

void LyricsBridge::setAudioEngine(vc::AudioEngine* engine) {
    s_engine = engine;
}

void LyricsBridge::connectSignals() {
    auto* bridge = instance();
    if (!bridge || !s_sync) {
        return;
    }

    if (s_connectedSync == s_sync && s_positionConnection && s_stateConnection) {
        return;
    }

    if (s_connectedSync && s_positionConnection) {
        s_connectedSync->positionChanged.disconnect(*s_positionConnection);
    }
    if (s_connectedSync && s_stateConnection) {
        s_connectedSync->stateChanged.disconnect(*s_stateConnection);
    }

    s_connectedSync = s_sync;
    s_positionConnection = s_sync->positionChanged.connect([](vc::LyricsSyncPosition pos) {
        if (auto* current = instance()) {
            current->onPositionChanged(pos);
        }
    });
    s_stateConnection = s_sync->stateChanged.connect([](vc::LyricsSyncState state) {
        if (auto* current = instance()) {
            current->onStateChanged(state);
        }
    });
}

bool LyricsBridge::hasLyrics() const {
    return s_sync && s_sync->hasLyrics();
}

QVariantList LyricsBridge::lines() const {
    QVariantList result;
    if (!s_sync) return result;
    
    const auto& lyricsLines = s_sync->getLyrics().lines;
    for (int i = 0; i < static_cast<int>(lyricsLines.size()); ++i) {
        result.append(lineToVariant(lyricsLines[i], i));
    }
    return result;
}

int LyricsBridge::currentLineIndex() const { return currentLineIndex_; }
int LyricsBridge::currentWordIndex() const { return currentWordIndex_; }
qreal LyricsBridge::lineProgress() const { return lineProgress_; }
qreal LyricsBridge::wordProgress() const { return wordProgress_; }
bool LyricsBridge::isInstrumental() const { return isInstrumental_; }

QString LyricsBridge::title() const {
    return s_sync ? QString::fromStdString(s_sync->getLyrics().title) : "";
}

QString LyricsBridge::artist() const {
    return s_sync ? QString::fromStdString(s_sync->getLyrics().artist) : "";
}

QVariantMap LyricsBridge::lineToVariant(const vc::LyricsLine& line, int index) const {
    QVariantMap map;
    map["index"] = index;
    map["text"] = QString::fromStdString(line.text);
    map["startTime"] = static_cast<qint64>(line.startTime * 1000.0f);
    map["isInstrumental"] = line.isInstrumental;
    return map;
}

QVariantMap LyricsBridge::getLine(int index) const {
    if (!s_sync || index < 0) return QVariantMap();
    const auto& lyricsLines = s_sync->getLyrics().lines;
    if (index >= static_cast<int>(lyricsLines.size())) return QVariantMap();
    return lineToVariant(lyricsLines[index], index);
}

void LyricsBridge::onPositionChanged(vc::LyricsSyncPosition pos) {
    currentLineIndex_ = pos.lineIndex;
    currentWordIndex_ = pos.wordIndex;
    lineProgress_ = pos.lineProgress;
    wordProgress_ = pos.wordProgress;
    isInstrumental_ = pos.isInstrumental;
    emit positionChanged();
}

void LyricsBridge::onLineChanged(int lineIndex) {
    currentLineIndex_ = lineIndex;
    emit positionChanged();
}

void LyricsBridge::onWordChanged(int lineIndex, int wordIndex) {
    currentLineIndex_ = lineIndex;
    currentWordIndex_ = wordIndex;
    emit positionChanged();
}

void LyricsBridge::onStateChanged(vc::LyricsSyncState state) {
    if (state == vc::LyricsSyncState::Loading ||
        state == vc::LyricsSyncState::Idle ||
        state == vc::LyricsSyncState::Error) {
        currentLineIndex_ = -1;
        currentWordIndex_ = -1;
        lineProgress_ = 0.0;
        wordProgress_ = 0.0;
        isInstrumental_ = false;
        emit positionChanged();
    }
    emit lyricsChanged();
    updateSearchResults();
}

void LyricsBridge::seekToLine(int lineIndex) {
    if (s_sync) s_sync->jumpToLine(static_cast<size_t>(lineIndex));
}

void LyricsBridge::exportToSrt(const QString& path) {
    if (!s_sync || !s_sync->hasLyrics()) {
        emit exportFailed(QString::fromLatin1(kNoLyricsMessage));
        return;
    }

    QString output;
    int index = 1;
    for (const auto& line : s_sync->getLyrics().lines) {
        if (line.text.empty()) {
            continue;
        }
        // Widen by a second when the end precedes the start, then floor in the
        // OUTPUT unit. Both steps are needed and they are not the same check.
        // The float comparison is true for an infinite endTime, which the
        // non-finite guard then maps to 0 -- so widening alone would emit a
        // backwards cue (00:00:01,000 --> 00:00:00,000), and a player discards
        // a cue whose End <= Start outright. The floor is inert for every
        // well-formed finite line, so it changes no golden byte. This mirrors
        // exportToAss's std::max(start + 1, end) for the same reason.
        const qint64 startMs =
                vc::LyricsExport::toTimeUnits(line.startTime, kMillisecondsPerSecond);
        qint64 endMs =
                vc::LyricsExport::toTimeUnits(line.endTime, kMillisecondsPerSecond);
        if (line.endTime <= line.startTime) {
            endMs = startMs + 1000;
        }
        endMs = std::max(endMs, startMs + 1);
        output += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                .arg(index++)
                .arg(formatSrtMilliseconds(startMs), formatSrtMilliseconds(endMs),
                     QString::fromStdString(line.text));
    }

    QString error;
    if (!writeExportFile(path, output.toUtf8(), error)) {
        emit exportFailed(error);
        return;
    }
    emit exportFinished(path);
}

void LyricsBridge::exportToLrc(const QString& path) {
    if (!s_sync || !s_sync->hasLyrics()) {
        emit exportFailed(QString::fromLatin1(kNoLyricsMessage));
        return;
    }

    QString output;
    const auto& lyrics = s_sync->getLyrics();
    // Escaped because these come from a remote payload: see lrcHeaderValue for
    // why a `[` here is a correctness problem and not a cosmetic one. The
    // header is the only part of an LRC file with a tag grammar -- a lyric line
    // is `[mm:ss.xx]text` and its text is whatever follows, so nothing in the
    // timed lines needs the same treatment.
    if (!lyrics.title.empty()) {
        output += QStringLiteral("[ti:%1]\n")
                          .arg(lrcHeaderValue(QString::fromStdString(lyrics.title)));
    }
    if (!lyrics.artist.empty()) {
        output += QStringLiteral("[ar:%1]\n")
                          .arg(lrcHeaderValue(QString::fromStdString(lyrics.artist)));
    }
    for (const auto& line : lyrics.lines) {
        if (line.text.empty()) {
            continue;
        }
        output += formatLrcTime(line.startTime) +
                  QString::fromStdString(line.text) + QLatin1Char('\n');
    }

    QString error;
    if (!writeExportFile(path, output.toUtf8(), error)) {
        emit exportFailed(error);
        return;
    }
    emit exportFinished(path);
}

void LyricsBridge::exportToAss(const QString& path) {
    if (!s_sync || !s_sync->hasLyrics()) {
        emit exportFailed(QString::fromLatin1(kNoLyricsMessage));
        return;
    }

    // The document is assembled one layer down, in vc::LyricsExport, because the
    // recorder muxes the same bytes into a subtitle stream and must not have to
    // reach up into a QML bridge for them. The raw bytes go to the file rather
    // than a QString round trip: the document is UTF-8 by construction, and the
    // muxer consumer wants exactly what the writer produced.
    const std::string document = vc::LyricsExport::toAssDocument(s_sync->getLyrics());
    QString error;
    if (!writeExportFile(path,
                         QByteArray(document.data(), static_cast<qsizetype>(document.size())),
                         error)) {
        emit exportFailed(error);
        return;
    }
    emit exportFinished(path);
}

QString LyricsBridge::assDocument(QString* error) const {
    if (!s_sync || !s_sync->hasLyrics()) {
        if (error) {
            *error = QString::fromLatin1(kNoLyricsMessage);
        }
        return {};
    }
    const std::string document = vc::LyricsExport::toAssDocument(s_sync->getLyrics());
    return QString::fromUtf8(document.data(), static_cast<qsizetype>(document.size()));
}

void LyricsBridge::setSearchQuery(const QString& query) {
    if (searchQuery_ == query) {
        return;
    }
    searchQuery_ = query;
    emit searchQueryChanged();
    updateSearchResults();
}

QString LyricsBridge::searchQuery() const { return searchQuery_; }
QVariantList LyricsBridge::searchResults() const { return searchResults_; }

void LyricsBridge::updateSearchResults() {
    QVariantList results;
    if (s_sync && !searchQuery_.trimmed().isEmpty()) {
        const auto& lines = s_sync->getLyrics().lines;
        for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
            if (QString::fromStdString(lines[static_cast<std::size_t>(index)].text)
                    .contains(searchQuery_, Qt::CaseInsensitive)) {
                results.append(lineToVariant(lines[static_cast<std::size_t>(index)], index));
            }
        }
    }
    searchResults_ = results;
    emit searchResultsChanged();
}

QVariantList LyricsBridge::getUpcomingLines(int count) const {
    QVariantList result;
    if (!s_sync || count <= 0) {
        return result;
    }
    const auto& lines = s_sync->getLyrics().lines;
    if (lines.empty()) {
        return result;
    }

    // This is the same overflow getContextLines had, one function up: the
    // window used to be `start + count`, and `count` arrives from QML as an
    // int, so any count near INT_MAX wrapped the end index negative, the loop
    // never ran, and the caller got an empty list with no error. The window is
    // therefore measured as a distance to the last line rather than summed, and
    // every subscript is proven by the same vc::checkedIndex gate LyricsData,
    // LyricsSync and getContextLines use, so there is one bounds gate in the
    // whole path. See upcomingLinesStopAtTheSongEndOnAnOversizedCount.
    const int last = static_cast<int>(lines.size()) - 1;

    // The start is chosen from the bounds rather than by `currentLineIndex_ + 1`,
    // which is signed overflow for the one value INT_MAX -- and it is a value
    // this surface can be handed, because currentLineIndex_ is just the last
    // position it was told about. An index at or past the last line has nothing
    // upcoming, so the window is empty and the addition is never reached.
    //
    // The anchor is deliberately NOT getContextLines' anchor. There, an unset
    // currentLineIndex_ means "the first line is the current one", so line 0 is
    // included. Here the same unset index has always meant "nothing is playing
    // yet, so the whole song from the first line is still to come", so -1 keeps
    // starting the window at line 0. Clamping the index to 0 and stepping off
    // it would silently drop line 0 from the pre-render buffer.
    const int start = currentLineIndex_ < 0
        ? 0
        : (currentLineIndex_ >= last ? static_cast<int>(lines.size()) : currentLineIndex_ + 1);

    // The window is the distance to the last line, not a sum with the caller's
    // count, so it cannot overflow for any `count` at all. Both ends of the
    // subtraction are bounded above by lines.size(), so the distance is itself
    // in [0, lines.size()] and the lower clamp is belt-and-braces rather than
    // load-bearing; the upper clamp is the count, which is the same pair of
    // bounds getContextLines applies per side. A negative count never reaches
    // here: the count <= 0 refusal above is that clamp applied to the whole
    // window, since a single span has no "other side" to fall back on, exactly
    // as getContextLines(-1, -1) is refused. That refusal is also what keeps
    // std::clamp's lo <= hi precondition true here.
    //
    // start + window is therefore at most lines.size(), so the loop bound is a
    // real subscript ceiling and not another sum of caller-supplied values.
    // `count` is never added to anything; it is only ever compared against.
    const int window = std::clamp(last - start + 1, 0, count);

    for (int index = start; index < start + window; ++index) {
        if (const auto offset = vc::checkedIndex(lines, index)) {
            result.append(lineToVariant(lines[*offset], index));
        }
    }
    return result;
}

QVariantList LyricsBridge::getContextLines(int before, int after) const {
    QVariantList result;
    if (!s_sync || (before < 0 && after < 0)) {
        return result;
    }
    const auto& lines = s_sync->getLyrics().lines;
    if (lines.empty()) {
        return result;
    }

    // This is the bridge's own copy of the context-window query, and it cannot
    // be merged into LyricsSync::getContextLines because the two pick the
    // centre differently and that difference is user-visible:
    //
    //   * this surface anchors on its own cached currentLineIndex_ and treats
    //     "no active line" as the start of the song, so it always returns at
    //     least line 0. LyricsSync anchors on currentPos_.lineIndex and returns
    //     nothing when that is unset. QML depends on the anchor, and the bridge
    //     is constructed lazily by the QML engine, so a page opened after
    //     playback started genuinely has currentLineIndex_ == -1.
    //   * LyricsSync takes a size_t window; this takes int, so a negative window
    //     from QML has no LyricsSync equivalent at all.
    //
    // So the window arithmetic is shared -- measured as a distance to the first
    // and last line rather than summed, and every subscript proven by the same
    // vc::checkedIndex gate LyricsData and LyricsSync use -- but the centre
    // rule stays here, deliberately. Collapsing it is a behaviour change and
    // needs a product decision, not a refactor. See
    // bridgeContextLinesMatchLyricsSyncExceptAtTheAnchor for the pinned
    // agreement, and for the one place the two answers differ.
    const int last = static_cast<int>(lines.size()) - 1;
    const int center = std::clamp(currentLineIndex_, 0, last);

    // before/after arrive from QML as an int, so `center + after + 1` overflows
    // for any `after` near INT_MAX: it wrapped `end` negative, the loop below
    // never ran, and the caller got an empty list with no error. Clamp each
    // span to the distance actually available before adding.
    const int start = center - std::min(std::max(0, before), center);
    const int end = center + 1 + std::min(std::max(0, after), last - center);

    for (int index = start; index < end; ++index) {
        if (const auto offset = vc::checkedIndex(lines, index)) {
            result.append(lineToVariant(lines[*offset], index));
        }
    }
    return result;
}

} // namespace qml_bridge
