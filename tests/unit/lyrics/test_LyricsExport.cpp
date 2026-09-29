/**
 * @file test_LyricsExport.cpp
 * @brief Byte-exact tests for LyricsBridge::exportToAss, the ASS karaoke writer.
 *
 * LyricsBridge is the sole owner of lyrics serialization -- a second
 * SRT/LRC formatter used to live in LyricsData and was deleted for exactly
 * that reason -- so these tests pin the ASS bytes rather than re-deriving
 * expectations from the writer. Every assertion here is on the file that was
 * actually written, not on a return value.
 *
 * The karaoke arithmetic is checked against a real renderer, not against
 * documentation: libass 0.17.5 was driven over these exact shapes to confirm
 * (a) `\{` renders a literal brace while an unescaped `{` silently swallows the
 * rest of the line, (b) `\kf` sweeps smoothly with PrimaryColour as the sung
 * colour, and (c) WrapStyle 0 reflows an overlong line where style 2 clips it.
 * The centisecond sum in assLongWordSequenceKeepsTheTailHonest is the one the
 * others cannot see: a renderer shows the drift, only the arithmetic pins it.
 */

#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "qml_bridge/LyricsBridge.hpp"

using namespace vc;

namespace {

/// One line, three words, at 0.0/0.5, 0.5/0.9 and 0.9/1.4 seconds. The two
/// awkward boundaries (0.9 -> 90cs, 1.4 -> 140cs) are chosen to land on
/// centisecond values that a truncating conversion gets wrong.
LyricsData threeWordLine() {
    LyricsData data;
    data.source = "suno";
    data.title = "Test";
    data.artist = "Artist";
    data.isSynced = true;

    LyricsLine line;
    line.text = "one two three";
    line.startTime = 0.0f;
    line.endTime = 1.4f;
    line.isSynced = true;
    line.words.push_back({"one", 0.0f, 0.5f, 1.0f});
    line.words.push_back({"two", 0.5f, 0.9f, 1.0f});
    line.words.push_back({"three", 0.9f, 1.4f, 1.0f});

    data.lines.push_back(line);
    return data;
}

QByteArray readExported(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

/// The \kf durations of one event's text, in order.
QList<qint64> karaokeDurations(const QString& eventText) {
    QList<qint64> durations;
    static const QRegularExpression tag(QStringLiteral("\\{\\\\kf(\\d+)\\}"));
    auto matches = tag.globalMatch(eventText);
    while (matches.hasNext()) {
        durations.append(matches.next().captured(1).toLongLong());
    }
    return durations;
}

/// The text of every Dialogue event, in file order.
QStringList dialogueTexts(const QByteArray& contents) {
    QStringList texts;
    const QStringList lines = QString::fromUtf8(contents).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.startsWith(QStringLiteral("Dialogue: "))) {
            texts.append(
                    line.mid(QStringLiteral("Dialogue: 0,").length()).section(QLatin1Char(','), 8));
        }
    }
    return texts;
}

struct AssBraces {
    int unescapedOpen{0};
    int unescapedClose{0};
    int escapedOpen{0};
    int escapedClose{0};
};

/// Braces split into the ones that open an override block and the ones that
/// are literal lyric characters.
///
/// A whole-file brace *total* is the wrong instrument: a karaoke tag and an
/// escaped literal brace both contribute one, so the total conflates "the tags
/// are well formed" with "the escaper let a brace through" and pins a
/// coincidence. What has to hold is the separable property -- every unescaped
/// brace opens a tag, and the literal ones are all escaped.
///
/// A brace counts as escaped when preceded by an odd-length run of
/// backslashes, which stays correct if the writer ever does emit an escaped
/// backslash rather than passing one through.
AssBraces countAssBraces(const QByteArray& contents) {
    AssBraces counts;
    int backslashes = 0;
    for (const char ch : contents) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '{' || ch == '}') {
            if (backslashes % 2 == 1) {
                if (ch == '{') {
                    ++counts.escapedOpen;
                } else {
                    ++counts.escapedClose;
                }
            } else {
                if (ch == '{') {
                    ++counts.unescapedOpen;
                } else {
                    ++counts.unescapedClose;
                }
            }
        }
        backslashes = 0;
    }
    return counts;
}

} // namespace

class TestLyricsExport : public QObject {
    Q_OBJECT

private slots:

    /// The header is the part a hand-rolled writer gets wrong, and a file a
    /// player rejects is worthless, so it is checked structurally: every
    /// section present, every key the writer promises present, and both Format
    /// lines carrying exactly the fields the spec fixes -- in that order,
    /// because a renderer reads them positionally.
    void assHeaderIsCompleteAndWellFormed() {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(threeWordLine());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        QVERIFY(!ass.isEmpty());
        const QString text = QString::fromUtf8(ass);

        QVERIFY(text.startsWith(QStringLiteral("[Script Info]\n")));
        QVERIFY(text.contains(QStringLiteral("Title: Test\n")));
        QVERIFY(text.contains(QStringLiteral("ScriptType: v4.00+\n")));
        QVERIFY(text.contains(QStringLiteral("WrapStyle: 0\n")));
        QVERIFY(text.contains(QStringLiteral("PlayResX: 1920\n")));
        QVERIFY(text.contains(QStringLiteral("PlayResY: 1080\n")));
        QVERIFY(text.contains(QStringLiteral("ScaledBorderAndShadow: yes\n")));
        QVERIFY(text.contains(QStringLiteral("\n[V4+ Styles]\n")));
        QVERIFY(text.contains(QStringLiteral("\n[Events]\n")));

        // Exactly one of each section, in the order ASS requires.
        QCOMPARE(text.count(QStringLiteral("[Script Info]")), 1);
        QCOMPARE(text.count(QStringLiteral("[V4+ Styles]")), 1);
        QCOMPARE(text.count(QStringLiteral("[Events]")), 1);
        QVERIFY(text.indexOf(QStringLiteral("[Script Info]")) <
                text.indexOf(QStringLiteral("[V4+ Styles]")));
        QVERIFY(text.indexOf(QStringLiteral("[V4+ Styles]")) <
                text.indexOf(QStringLiteral("[Events]")));

        // The V4+ style list, complete and in the spec's order.
        //
        // Field counting, since this is the assertion that is easy to get wrong
        // twice: the ASS v4+ style format has exactly 23 fields, and splitting
        // the Format line on commas yields exactly 23 elements -- NOT 24. The
        // prefix is not extra: "Format: Name" *is* the Name field, because the
        // renderer reads these lines positionally against a field list it
        // already knows. The same applies to "Format: Layer" (10 fields) and
        // to the value row, where "Style: Default" supplies the Name value.
        const QString styleFormat = QStringLiteral(
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
                "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                "MarginL, MarginR, MarginV, Encoding");
        QVERIFY(text.contains(styleFormat));
        QCOMPARE(styleFormat.split(QLatin1Char(',')).size(), 23);

        // The 10-field event list. Section and Dialogue rows must agree on it,
        // since a renderer joins the two positionally.
        const QString eventFormat = QStringLiteral(
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text");
        QVERIFY(text.contains(eventFormat));
        QCOMPARE(eventFormat.split(QLatin1Char(',')).size(), 10);

        // The style row supplies exactly one value for each of those 23 fields,
        // and the named field positions are the spec's own indices.
        const int styleRow = text.indexOf(QStringLiteral("Style: Default,"));
        QVERIFY(styleRow > 0);
        const QString styleLine = text.mid(styleRow).section(QLatin1Char('\n'), 0, 0);
        const QStringList styleFields = styleLine.split(QLatin1Char(','));
        QCOMPARE(styleFields.size(), 23);
        QCOMPARE(styleFields.at(0), QStringLiteral("Style: Default")); // Name
        QCOMPARE(styleFields.at(1), QStringLiteral("Arial"));          // Fontname
        QCOMPARE(styleFields.at(2), QStringLiteral("60"));             // Fontsize

        // Alignment is field 19 (index 18) of the style, and 2 is bottom-centre:
        // the karaoke position, and the one whose MarginV means the same thing
        // at this PlayRes as it will on a 1920x1080 encode.
        QCOMPARE(styleFields.at(18), QStringLiteral("2")); // Alignment
        QCOMPARE(styleFields.at(22), QStringLiteral("1")); // Encoding

        // PrimaryColour (index 3) is the sung colour, SecondaryColour (index 4)
        // the unsung one -- verified against libass, which fills the swept
        // portion with Primary.
        QCOMPARE(styleFields.at(3), QStringLiteral("&H00FFFFFF"));
        QCOMPARE(styleFields.at(4), QStringLiteral("&H00808080"));
    }

    /// The karaoke sequence itself, byte for byte. \kf not \k, and the
    /// centiseconds are 50/40/50 -- note 0.9s is 90cs and 1.4s is 140cs, which
    /// a truncating conversion would write as 89 and 139 and pull the sweep a
    /// centisecond early at every word after the first.
    void assThreeWordLineEmitsSmoothKaraokeTags() {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(threeWordLine());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        QVERIFY(ass.endsWith("\n"));

        // One Dialogue event per LyricsLine, with the full nine commas that
        // ten positional fields require.
        QCOMPARE(ass.count("Dialogue: "), 1);
        QVERIFY(ass.contains("Dialogue: 0,00:00:00.00,00:00:01.40,Default,,0,0,0,,{\\kf50}one "
                             "{\\kf40}two {\\kf50}three\n"));
        QCOMPARE(ass.count("-->"), 0); // not an SRT; the arrow must not leak in

        const QStringList texts = dialogueTexts(ass);
        QCOMPARE(texts.size(), 1);
        QCOMPARE(karaokeDurations(texts.first()), QList<qint64>({50, 40, 50}));
        QVERIFY(!texts.first().contains(QStringLiteral("\\k ")));
        QCOMPARE(texts.first().count(QStringLiteral("{\\kf")), 3);
    }

    /// The regression guard for the defect that shipped this writer the first
    /// time: formatAssTime had three placeholders and four .arg() calls, so Qt
    /// substituted three, warned "Argument missing", and returned the string
    /// unchanged -- the centiseconds were dropped and every timestamp became
    /// the literal "00:00.00". ASS needs H:MM:SS.cc, so the whole feature
    /// degraded to whole-second granularity.
    ///
    /// This exists as a separate test because of where the hole was:
    /// assLongWordSequenceKeepsTheTailHonest sums the \kf *durations*, which
    /// never pass through formatAssTime, so the one test that validated the
    /// rounding rule was structurally incapable of noticing that the rendered
    /// timestamps threw it away. Durations and timestamps are two different
    /// outputs and need two different guards.
    ///
    /// The expectations are hand-computed golden constants chosen to exercise
    /// each field, deliberately NOT a reimplementation of formatAssTime -- a
    /// reimplementation would share the very assumption under test.
    void aTimestampWithNonZeroCentisecondsSurvives() {
        LyricsData data;
        data.isSynced = true;
        data.title = "Test";

        // Each pair is hand-converted: 1.40s -> 140cs -> 00:00:01.40,
        // 2.37s -> 237cs -> 00:00:02.37, and so on. The centisecond values are
        // all non-zero on purpose, so the buggy truncation to ".00" is
        // detectable on every single field.
        const std::vector<std::tuple<QString, f32, f32>> lines = {
                {"first", 1.40f, 2.37f},       // seconds + centiseconds
                {"second", 61.05f, 61.99f},    // minutes + centiseconds
                {"third", 3600.07f, 3601.11f}, // hours + minutes + centiseconds
        };
        for (const auto& [text, start, end] : lines) {
            LyricsLine line;
            line.text = text.toStdString();
            line.startTime = start;
            line.endTime = end;
            line.isSynced = true;
            data.lines.push_back(line);
        }

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);

        // The exact bytes, one golden row per field combination.
        QVERIFY(ass.contains("Dialogue: 0,00:00:01.40,00:00:02.37,Default,,0,0,0,,first\n"));
        QVERIFY(ass.contains("Dialogue: 0,00:01:01.05,00:01:01.99,Default,,0,0,0,,second\n"));
        QVERIFY(ass.contains("Dialogue: 0,01:00:00.07,01:00:01.11,Default,,0,0,0,,third\n"));

        // The string the bug actually produced. Absent here, and it is the
        // cheapest possible tripwire for the regression.
        QVERIFY(!ass.contains("00:00.00"));

        // Structural invariants, so a future placeholder/argument mismatch fails
        // here rather than silently reshaping every timestamp again.
        //
        // These run over the raw Dialogue lines, not over dialogueTexts(), which
        // deliberately returns only the Text field -- indexing Start/End out of
        // an already-extracted caption would be testing nothing.
        static const QRegularExpression shape(QStringLiteral("^\\d{2}:\\d{2}:\\d{2}\\.\\d{2}$"));
        int checked = 0;
        for (const QString& line : QString::fromUtf8(ass).split(QLatin1Char('\n'))) {
            if (!line.startsWith(QStringLiteral("Dialogue: "))) {
                continue;
            }
            // Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
            const QStringList fields = line.mid(QStringLiteral("Dialogue: ").length())
                                               .split(QLatin1Char(','));
            QVERIFY2(fields.size() >= 9,
                     qPrintable(QStringLiteral("Dialogue line has %1 fields, need 10")
                                    .arg(fields.size())));
            for (const int which : {1, 2}) {
                QVERIFY2(shape.match(fields.at(which)).hasMatch(),
                         qPrintable(QStringLiteral("timestamp not HH:MM:SS.cc: %1")
                                            .arg(fields.at(which))));
                // Every fixture value has non-zero centiseconds, so a field
                // ending in "00" means the centiseconds were dropped.
                QVERIFY2(fields.at(which).right(2) != QStringLiteral("00"),
                         qPrintable(QStringLiteral("centiseconds dropped: %1")
                                            .arg(fields.at(which))));
            }
            ++checked;
        }
        QCOMPARE(checked, 3);
    }

    /// No lyrics loaded is refused, exactly as the SRT and LRC writers refuse
    /// it, and nothing is written.
    void assEmptyLyricsFailsWithoutWriting() {
        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("No lyrics are loaded."));
        QVERIFY(!QFile::exists(path));

        // And the empty path is refused the same way, matching the siblings.
        sync.loadLyrics(threeWordLine());
        bridge.exportToAss(QString());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(failed.count(), 2);
    }

    /// A line with no word list still has to reach the player -- as an untagged
    /// caption. Dropping it would silently lose a lyric, and emitting a bare
    /// \kf tag for a line that has no words would be a tag stream with no text.
    void assLineWithNoWordsEmitsUntaggedText() {
        LyricsData data;
        data.isSynced = true;
        data.title = "Test";

        LyricsLine plain;
        plain.text = "no words here";
        plain.startTime = 1.0f;
        plain.endTime = 2.0f;
        plain.isSynced = true; // timed, but never aligned to words
        QVERIFY(plain.words.empty());

        LyricsLine empty;
        empty.text = ""; // the spacer the aligner emits between sections
        empty.startTime = 2.0f;
        empty.endTime = 2.0f;

        data.lines.push_back(plain);
        data.lines.push_back(empty);

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        // The empty line is skipped, as in SRT and LRC, so one event survives.
        QCOMPARE(ass.count("Dialogue: "), 1);
        QVERIFY(ass.contains(
                "Dialogue: 0,00:00:01.00,00:00:02.00,Default,,0,0,0,,no words here\n"));
        QVERIFY(!ass.contains("\\kf"));
    }

    /// Zero and inverted word spans, and a line whose end precedes its start.
    /// A Dialogue with End <= Start is discarded outright by most players, so
    /// the guarantee that matters is that every event is strictly forward and
    /// no \kf0 is ever emitted.
    void assZeroAndInvertedDurationsStayNonDegenerate() {
        LyricsData data;
        data.isSynced = true;
        data.title = "Test";

        LyricsLine line;
        line.text = "a b";
        line.startTime = 5.0f;
        line.endTime = 5.0f; // inverted-or-equal: widened, never written backwards
        line.isSynced = true;
        line.words.push_back({"a", 5.0f, 5.0f, 1.0f}); // zero span
        line.words.push_back({"b", 6.0f, 5.0f, 1.0f}); // end before start

        data.lines.push_back(line);

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        // 5.0s widened by the same one second the SRT writer uses.
        QVERIFY(ass.contains(
                "Dialogue: 0,00:00:05.00,00:00:06.00,Default,,0,0,0,,{\\kf1}a {\\kf1}b\n"));
        QVERIFY(!ass.contains("\\kf0"));
        QVERIFY(!ass.contains("\\kf-"));

        const QStringList texts = dialogueTexts(ass);
        QCOMPARE(texts.size(), 1);
        QCOMPARE(karaokeDurations(texts.first()), QList<qint64>({1, 1}));
    }

    /// Braces, a newline and a backslash in the lyric text. The braces are the
    /// load-bearing case: unescaped, `{Y` renders as `Y` and takes the rest of
    /// the line with it (libass 0.17.5, measured). The newline is a hard
    /// structural break in the file, so it is collapsed rather than escaped --
    /// see the line-breaking note in the .cpp.
    void assEscapesBracesAndNeutralisesNewlines() {
        LyricsData data;
        data.isSynced = true;
        data.title = "Test";

        LyricsLine line;
        line.text = "chorus {hook} and \\ more";
        line.startTime = 0.0f;
        line.endTime = 1.0f;
        line.isSynced = true;
        line.words.push_back({"chorus", 0.0f, 0.3f, 1.0f});
        line.words.push_back({"{hook}", 0.3f, 0.6f, 1.0f});
        line.words.push_back({"and", 0.6f, 0.8f, 1.0f});
        line.words.push_back({"\\ more", 0.8f, 1.0f, 1.0f});

        data.lines.push_back(line);

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        QVERIFY(ass.contains("{\\kf30}chorus {\\kf30}\\{hook\\} {\\kf20}and {\\kf20}\\ more\n"));

        // Structural proof that no unescaped brace survived, counted the way
        // that is actually meaningful. This fixture has four karaoke tags and
        // one escaped literal brace, so the raw total is five of each, and
        // asserting "four" was simply a miscount -- worse, a raw total asserts
        // a coincidence, because tag braces and literal braces are then
        // indistinguishable. The separable property is that the four unescaped
        // opens are exactly the four tags, the two escaped ones are exactly the
        // literal { and }, and the sides balance.
        const AssBraces braces = countAssBraces(ass);
        QCOMPARE(braces.unescapedOpen, 4); // the four {\kfNN} tags
        QCOMPARE(braces.unescapedClose, 4);
        QCOMPARE(braces.escapedOpen, 1);  // the literal { of {hook}
        QCOMPARE(braces.escapedClose, 1); // the literal } of {hook}
        QCOMPARE(ass.count('{'), 5);      // 4 tags + 1 escaped literal
        QCOMPARE(ass.count('}'), 5);
        QCOMPARE(ass.count("\\{hook\\}"), 1);
        QVERIFY(ass.endsWith("\n"));
    }

    /// A newline inside a lyric line, and a title carrying one, must not be
    /// able to forge a header key or split an event in two.
    void assNewlinesInTextAndTitleCannotForgeAHeaderKey() {
        LyricsData data;
        data.isSynced = true;
        data.title = "Bad\nTitle"; // remote payload, not a trusted string

        LyricsLine line;
        line.text = "first half\nsecond half";
        line.startTime = 0.0f;
        line.endTime = 1.0f;
        line.isSynced = true;

        data.lines.push_back(line);

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        // The forged key never reaches the header: the newline is collapsed,
        // so there is exactly one Title key in the whole file.
        QVERIFY(ass.contains("Title: Bad Title\n"));
        QCOMPARE(ass.count("Title: "), 1);
        QVERIFY(!ass.contains("Title: Bad\nTitle"));

        // Still exactly one event, so the embedded newline did not split it.
        QCOMPARE(ass.count("Dialogue: "), 1);
        QVERIFY(ass.contains(
                "Dialogue: 0,00:00:00.00,00:00:01.00,Default,,0,0,0,,first half second half\n"));
        // No hard line break was invented: the writer does not decide where
        // text wraps.
        QVERIFY(!ass.contains("\\N"));
    }

    /// The rounding rule, pinned where it is actually observable. A player
    /// derives a tag's position by accumulating the durations before it, so a
    /// writer that rounds each word's span independently drifts by up to half a
    /// centisecond per word. 200 words of 0.333s is 6.6s of span, i.e. 6660
    /// centiseconds; independent rounding gives 200 * 33 = 6600, a 60cs (0.6s)
    /// error that lands on the last word of the line.
    void assLongWordSequenceKeepsTheTailHonest() {
        constexpr int wordCount = 200;
        constexpr f32 wordSeconds = 0.333f;

        LyricsData data;
        data.isSynced = true;
        data.title = "Test";

        LyricsLine line;
        line.startTime = 0.0f;
        line.endTime = wordSeconds * wordCount;
        line.isSynced = true;
        for (int i = 0; i < wordCount; ++i) {
            const std::string token = "w" + std::to_string(i);
            line.words.push_back({token, wordSeconds * i, wordSeconds * (i + 1), 1.0f});
            line.text += (i == 0 ? "" : " ") + token;
        }
        data.lines.push_back(line);

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QStringList texts = dialogueTexts(readExported(path));
        QCOMPARE(texts.size(), 1);
        const QList<qint64> durations = karaokeDurations(texts.first());
        QCOMPARE(durations.size(), wordCount);

        qint64 accumulated = 0;
        for (const qint64 duration : durations) {
            QVERIFY(duration >= 1);
            accumulated += duration;
        }

        // The line's own end stamp, independently converted.
        const qint64 expected = std::llround(line.endTime * 100.0f);
        QCOMPARE(expected, 6660);
        QCOMPARE(accumulated, expected);

        // The discriminating statement: an independent per-word rounding emits
        // this instead, and it is 60 centiseconds -- 0.6s -- short by the last
        // word, which is the drift the whole rounding rule exists to avoid.
        const qint64 naive = static_cast<qint64>(wordCount) * std::llround(wordSeconds * 100.0f);
        QCOMPARE(naive, 6600);
        QVERIFY(accumulated != naive);
    }

    /// The text rebuilt from the word timings is the source line, so the
    /// karaoke tags and the SRT/LRC exports of one song cannot disagree about
    /// what the words are. This is what pins the writer's own spacing.
    void wordTimingRebuildsExactlyTheSourceLineText() {
        LyricsData data = threeWordLine();
        // A payload that already spaced its tokens, which the writer must not
        // double up against.
        data.lines[0].words[0].text = "one  ";
        data.lines[0].words[1].text = "two";

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        QStringList texts = dialogueTexts(readExported(path));
        QCOMPARE(texts.size(), 1);

        // Strip the tags back out and the sentence is line.text verbatim.
        static const QRegularExpression tag(QStringLiteral("\\{\\\\kf\\d+\\}"));
        const QString rebuilt = texts.first().remove(tag).trimmed();
        QCOMPARE(rebuilt, QString::fromStdString(data.lines[0].text));
    }

    /// A degenerate remote payload must still yield a file a player can open:
    /// every event forward in time, every duration positive, no unescaped
    /// brace, and the section structure intact.
    void assDegenerateRemotePayloadStillProducesAValidFile() {
        LyricsData data = threeWordLine();

        LyricsLine spacer;
        spacer.text = "";
        spacer.startTime = 2.0f;
        spacer.endTime = 2.0f;

        LyricsLine inverted;
        inverted.text = "backwards";
        inverted.startTime = 3.0f;
        inverted.endTime = 2.0f; // end precedes start
        inverted.isSynced = true;
        inverted.words.push_back({"backwards", 3.0f, 2.0f, 1.0f});

        LyricsLine untimed;
        untimed.text = "no timing at all";
        untimed.startTime = 4.0f;
        untimed.endTime = 0.0f;

        data.lines.push_back(spacer);
        data.lines.push_back(inverted);
        data.lines.push_back(untimed);

        LyricsSync sync(nullptr);
        qml_bridge::LyricsBridge::setLyricsSync(&sync);
        qml_bridge::LyricsBridge bridge;
        sync.loadLyrics(data);
        QCOMPARE(sync.getState(), LyricsSyncState::Ready);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.ass"));
        QSignalSpy finished(&bridge, &qml_bridge::LyricsBridge::exportFinished);
        QSignalSpy failed(&bridge, &qml_bridge::LyricsBridge::exportFailed);

        bridge.exportToAss(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);

        const QByteArray ass = readExported(path);
        // Three of the four source lines carry text; the spacer is skipped.
        QCOMPARE(ass.count("Dialogue: "), 3);
        // Same fixture and same first line as
        // assThreeWordLineEmitsSmoothKaraokeTags, so the first word is 50cs
        // there and here. An earlier revision of this expected 100, which
        // contradicted that passing test rather than describing the writer.
        QVERIFY(ass.contains("{\\kf50}one {\\kf40}two {\\kf50}three\n"));
        QVERIFY(ass.contains("{\\kf1}backwards\n"));
        // An untimed line (endTime 0.0, before its start) is widened by the same
        // one second exportToSrt uses, and lands 4.00 -> 5.00, never backwards.
        QVERIFY(ass.contains(
                "Dialogue: 0,00:00:04.00,00:00:05.00,Default,,0,0,0,,no timing at all\n"));

        // Every event starts strictly before it ends. Comparing the formatted
        // strings is only meaningful because ASS timestamps are fixed width
        // (HH:MM:SS.cc), which is asserted directly by
        // aTimestampWithNonZeroCentisecondsSurvives -- the defect that produced
        // the truncated "00:00.00" made this comparison vacuous, since every
        // event collapsed to the same value.
        //
        // Over the raw Dialogue lines, not over dialogueTexts(): that helper
        // returns only the Text field, so Start/End are not addressable in it.
        int events = 0;
        for (const QString& line : QString::fromUtf8(ass).split(QLatin1Char('\n'))) {
            if (!line.startsWith(QStringLiteral("Dialogue: "))) {
                continue;
            }
            const QStringList fields = line.mid(QStringLiteral("Dialogue: ").length())
                                               .split(QLatin1Char(','));
            QVERIFY2(fields.size() >= 9,
                     qPrintable(QStringLiteral("Dialogue line has %1 fields, need 10")
                                    .arg(fields.size())));
            QCOMPARE(fields.at(1).size(), 11);
            QCOMPARE(fields.at(2).size(), 11);
            QVERIFY2(fields.at(1) < fields.at(2),
                     qPrintable(QStringLiteral("event is not forward in time: %1 -> %2")
                                    .arg(fields.at(1), fields.at(2))));
            ++events;
        }
        QCOMPARE(events, 3);
        QVERIFY(!ass.contains("\\kf0"));
        QVERIFY(!ass.contains("\\N"));
        // Four karaoke tags across the three events, no literal braces in this
        // fixture, and the two sides balance.
        const AssBraces braces = countAssBraces(ass);
        QCOMPARE(braces.unescapedOpen, 4);
        QCOMPARE(braces.unescapedClose, 4);
        QCOMPARE(braces.escapedOpen, 0);
        QCOMPARE(braces.escapedClose, 0);
    }
};

int runTestLyricsExport(int argc, char** argv) {
    TestLyricsExport test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_LyricsExport.moc"
