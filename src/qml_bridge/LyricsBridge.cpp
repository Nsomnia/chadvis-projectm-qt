#include "LyricsBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"

#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include "lyrics/LyricsSync.hpp"

namespace qml_bridge {

namespace {

QString formatSrtTime(const vc::f32 seconds) {
    const qint64 totalMs = std::max<qint64>(0, std::llround(seconds * 1000.0f));
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
    const qint64 totalCentiseconds = std::max<qint64>(0, std::llround(seconds * 100.0f));
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

/// Seconds to the centiseconds ASS timestamps are written in.
///
/// A karaoke tag's position is not stated directly: a player derives it by
/// *accumulating* the durations that precede it. That is what makes the
/// rounding rule load-bearing, and it is why this function exists separately
/// from the duration arithmetic. A writer that rounds each word's span on its
/// own pays up to half a centisecond of error per word, in one direction per
/// word, and the error survives into every later word in the line -- 200 words
/// of 0.333s is a 66.6s line that such a writer ends 0.6s early. Every
/// duration below is instead a difference of two *absolute* stamps, so the
/// error is bounded at each boundary and the last word of the line lands on
/// the real line end. See assLongWordSequenceKeepsTheTailHonest.
///
/// std::llround rounds half away from zero, matching formatSrtTime and
/// formatLrcTime. The non-finite check is the one place this is deliberately
/// stricter than the siblings: their std::llround is undefined behaviour on a
/// NaN or infinite start time, and a remote payload that parses one would take
/// the export down with it rather than merely mis-render.
qint64 toAssCentiseconds(const vc::f32 seconds) {
    if (!std::isfinite(seconds)) {
        return 0;
    }
    return std::max<qint64>(0, std::llround(seconds * 100.0f));
}

/// `H:MM:SS.cc` -- ASS counts centiseconds, not SRT's milliseconds.
///
/// Four values, so four placeholders. This was three placeholders with four
/// `.arg()` calls, which does not fail loudly in a build: Qt substitutes the
/// first three, finds no `%4` for the fourth, warns `Argument missing`, and
/// returns the string unchanged. The centiseconds were discarded and every
/// event collapsed to `00:00.00`, so the karaoke quantised to whole seconds.
/// The shape is *not* formatLrcTime's `MM:SS.cc` -- ASS carries an hours
/// field, which is the whole reason the two differ, and the reason the count
/// has to be checked rather than assumed.
/// See assThreeWordLineEmitsSmoothKaraokeTags and aTimestampWithNonZeroCentisecondsSurvives.
QString formatAssTime(const qint64 centiseconds) {
    const qint64 hours = centiseconds / 360000;
    const qint64 minutes = (centiseconds / 6000) % 60;
    const qint64 secs = (centiseconds / 100) % 60;
    const qint64 centis = centiseconds % 100;
    return QStringLiteral("%1:%2:%3.%4")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'))
            .arg(centis, 2, 10, QLatin1Char('0'));
}

/// Neutralise the three characters that would otherwise break the tag stream.
///
/// `{` and `}` open and close an override block, and a raw newline terminates
/// the Dialogue event -- so an unescaped brace does not just look wrong, it
/// silently eats the brace and everything after it (verified against libass
/// 0.17.5: the text `X{Y` renders as `XY`). Escaping both is the difference
/// between a lyric containing braces and a lyric with a hole in it.
///
/// A backslash is a deliberate pass-through, not an oversight: libass renders
/// `\\` as *two* glyphs, so doubling it is measurably lossy, and a lone one is
/// already literal unless it forms `\N`, `\n` or `\h` -- the irreducible case,
/// noted below.
QString escapeAssText(const QString& text) {
    QString escaped;
    escaped.reserve(text.size() + 8);
    for (const QChar ch : text) {
        switch (ch.unicode()) {
            case u'\\':
                escaped += ch;
                break;
            case u'{':
            case u'}':
                escaped += QLatin1Char('\\');
                escaped += ch;
                break;
            case u'\n':
            case u'\r':
                escaped += QLatin1Char(' ');
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

/// One event's text: the line's words, each behind its own karaoke tag.
///
/// \kf rather than \k. The capture gives word-level boundaries and nothing
/// finer, but a karaoke renderer still has to fill *across* the word while it
/// is sung; \kf interpolates that fill over the word's own span, where \k
/// would make the highlight jump per word. It is also what this application
/// already draws -- KaraokeMaster.qml sweeps the line continuously off
/// lineProgress -- so the sidecar and the live view agree.
QString assEventText(const vc::LyricsLine& line) {
    if (line.words.empty()) {
        // Nothing to sweep, but the words still have to reach the player: a
        // line that was timed without a word list is an untagged caption, not a
        // line to drop.
        return escapeAssText(QString::fromStdString(line.text));
    }

    QString text;
    for (std::size_t i = 0; i < line.words.size(); ++i) {
        const auto& word = line.words[i];

        // A minimum of one centisecond, because \kf0 is a degenerate tag that
        // makes renderers flash the syllable rather than show it, and a word
        // whose end precedes its start yields a negative span. The line's own
        // End field is written from the line's span, so widening a duration
        // here cannot push the event past the line.
        const qint64 duration = std::max<qint64>(1, toAssCentiseconds(word.endTime) -
                                                            toAssCentiseconds(word.startTime));
        text += QStringLiteral("{\\kf%1}").arg(duration);

        // Word timings carry bare tokens, so the separator is the writer's
        // responsibility; trimming a token's tail stops a payload that already
        // spaced its words from rendering a double space. The rebuilt sentence
        // is then byte-identical to line.text for any well-formed alignment,
        // which is pinned by wordTimingRebuildsExactlyTheSourceLineText.
        QString token = QString::fromStdString(word.text);
        while (token.endsWith(QLatin1Char(' ')) || token.endsWith(QLatin1Char('\t'))) {
            token.chop(1);
        }
        text += escapeAssText(token);
        if (i + 1 < line.words.size()) {
            text += QLatin1Char(' ');
        }
    }
    return text;
}

/// A [Script Info] value. Only the one that can be attacker-influenced: title
/// and artist come from a remote payload, and a newline in either would forge a
/// second header key and break the section for every reader.
QString assHeaderValue(const QString& value) {
    QString cleaned = value;
    cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
    cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
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
        emit exportFailed(QStringLiteral("No lyrics are loaded."));
        return;
    }

    QString output;
    int index = 1;
    for (const auto& line : s_sync->getLyrics().lines) {
        if (line.text.empty()) {
            continue;
        }
        const auto end = line.endTime > line.startTime ? line.endTime : line.startTime + 1.0f;
        output += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                .arg(index++)
                .arg(formatSrtTime(line.startTime), formatSrtTime(end),
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
        emit exportFailed(QStringLiteral("No lyrics are loaded."));
        return;
    }

    QString output;
    const auto& lyrics = s_sync->getLyrics();
    if (!lyrics.title.empty()) {
        output += QStringLiteral("[ti:%1]\n")
                .arg(QString::fromStdString(lyrics.title));
    }
    if (!lyrics.artist.empty()) {
        output += QStringLiteral("[ar:%1]\n")
                .arg(QString::fromStdString(lyrics.artist));
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
        emit exportFailed(QStringLiteral("No lyrics are loaded."));
        return;
    }

    const auto& lyrics = s_sync->getLyrics();

    QString output = QStringLiteral("[Script Info]\n");
    if (!lyrics.title.empty()) {
        output += QStringLiteral("Title: %1\n")
                          .arg(assHeaderValue(QString::fromStdString(lyrics.title)));
    }

    // PlayResX/Y are the script's coordinate space, and they are 1920x1080
    // because that is what the recorder encodes by default: a subtitle authored
    // in the script resolution of the video it will be muxed into needs no
    // rescale, so Alignment 2 and MarginV mean the same thing here as they do
    // on the finished frame. Any other choice would be a guess that a player
    // silently corrects by scaling, which is how a bottom-centre caption ends
    // up somewhere else entirely.
    //
    // WrapStyle 0 is the only value that degrades safely. A lyric line longer
    // than the script width is expected -- the capture makes no attempt to
    // wrap prose -- and style 0 (smart wrapping) folds it back onto the frame,
    // whereas 1 and 2 break only on an explicit \N and simply run off the edge
    // with the text unreadable (verified against libass 0.17.5 at
    // PlayResX 640: style 0 reflowed onto five lines, style 2 clipped both
    // ends). The alternative to wrapping is the hard break this writer
    // refuses to invent; see assEventText.
    //
    // The two Format lines are the field lists, in the exact order the spec
    // fixes. Order is not cosmetic: a renderer reads these positionally, so a
    // transposed field does not error, it just puts Outline where Alignment
    // belongs and the file plays with garbage geometry.
    output += QStringLiteral(
            "ScriptType: v4.00+\n"
            "WrapStyle: 0\n"
            "PlayResX: 1920\n"
            "PlayResY: 1080\n"
            "ScaledBorderAndShadow: yes\n"
            "\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
            "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
            "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
            "MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,60,&H00FFFFFF,&H00808080,&H00000000,&H00000000,"
            "0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
            "\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");

    for (const auto& line : lyrics.lines) {
        if (line.text.empty()) {
            continue;
        }

        // The same widening exportToSrt applies, so a line whose endTime
        // precedes its startTime is never written backwards. The floor is
        // stricter than SRT's: a Dialogue whose End <= Start is discarded
        // outright by most players, so without it a sub-centisecond line would
        // not merely look wrong, it would vanish.
        const qint64 startCs = toAssCentiseconds(line.startTime);
        const qint64 endCs =
                line.endTime > line.startTime ? toAssCentiseconds(line.endTime) : startCs + 100;
        output += QStringLiteral("Dialogue: 0,%1,%2,Default,,0,0,0,,%3\n")
                          .arg(formatAssTime(startCs), formatAssTime(std::max(startCs + 1, endCs)),
                               assEventText(line));
    }

    QString error;
    if (!writeExportFile(path, output.toUtf8(), error)) {
        emit exportFailed(error);
        return;
    }
    emit exportFinished(path);
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
