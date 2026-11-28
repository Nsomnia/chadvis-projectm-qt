/**
 * @file test_WhisperTranscriber.cpp
 * @brief The parts of the whisper bridge that have a contract: the CLI's JSON,
 *        and the conversion into the renderer's model.
 *
 * No binary and no model are needed, and none are faked. The process driver is
 * deliberately not exercised here: a test that spawns a real `whisper-cli` would
 * pass or fail on whether the developer happens to have a 1.5 GB model on disk,
 * which tells the next person nothing.
 *
 * The interpolation flag is pinned from both directions, because it is the one
 * place this module could quietly lie about precision: a one-word segment is
 * measured and must read as measured, a two-word segment is divided and must say
 * so.
 */

#include <QtTest>

#include <QFileInfo>

#include "lyrics/WhisperTranscriber.hpp"

using namespace vc::lyrics;

namespace {

/// Two segments: one word, then two. Shaped like real `-oj -ml 1` output,
/// trimmed to the keys that are read.
const char* kTwoSegments = R"({
  "model": {"type": "large-v3-turbo"},
  "params": {"language": "en", "model": "ggml-large-v3-turbo.bin"},
  "result": {"language": "en"},
  "transcription": [
    {"timestamps": {"from": "00:00:00,500", "to": "00:00:01,000"},
     "offsets": {"from": 500, "to": 1000}, "text": " Hello"},
    {"timestamps": {"from": "00:00:01,000", "to": "00:00:02,000"},
     "offsets": {"from": 1000, "to": 2000}, "text": " world again"}
  ]
})";

/// Offsets absent: the timestamp spelling is the fallback, and it is the one a
/// non-English or older build emits.
const char* kTimestampsOnly = R"({
  "transcription": [
    {"timestamps": {"from": "00:01:02,250", "to": "00:01:03,000"}, "text": " one"}
  ]
})";

} // namespace

class TestWhisperTranscriber : public QObject {
    Q_OBJECT

private slots:
    void parsesOffsetsWordsAndLanguage() {
        QString error;
        const auto transcript = parseWhisperJson(QByteArray(kTwoSegments), &error);
        QVERIFY2(transcript.has_value(), qPrintable(error));
        QVERIFY(error.isEmpty());

        QCOMPARE(static_cast<int>(transcript->segments.size()), 2);
        QCOMPARE(QString::fromStdString(transcript->language), QStringLiteral("en"));
        QCOMPARE(QString::fromStdString(transcript->model), QStringLiteral("large-v3-turbo"));

        QCOMPARE(QString::fromStdString(transcript->segments[0].text), QStringLiteral("Hello"));
        QCOMPARE(transcript->segments[0].start, 0.5f);
        QCOMPARE(transcript->segments[0].end, 1.0f);
        QCOMPARE(transcript->segments[1].end, 2.0f);

        // 1 + 2 words across two segments.
        QCOMPARE(transcript->wordCount(), std::size_t{3});
        QCOMPARE(transcript->segments[1].words.size(), std::size_t{2});
        QCOMPARE(QString::fromStdString(transcript->segments[1].words[1].text),
                 QStringLiteral("again"));
        QCOMPARE(transcript->lastEnd(), 2.0f);
    }

    void measuredWordIsNotFlaggedInterpolated() {
        const auto transcript = parseWhisperJson(QByteArray(kTwoSegments));
        QVERIFY(transcript.has_value());
        // A single-word segment IS the measured range.
        QVERIFY(!transcript->segments[0].words.at(0).interpolated);
        // A two-word segment is divided across it, and says so.
        QVERIFY(transcript->segments[1].words.at(0).interpolated);
        QVERIFY(transcript->segments[1].words.at(1).interpolated);
    }

    void fallsBackToTimestamps() {
        QString error;
        const auto transcript = parseWhisperJson(QByteArray(kTimestampsOnly), &error);
        QVERIFY2(transcript.has_value(), qPrintable(error));
        QCOMPARE(static_cast<int>(transcript->segments.size()), 1);
        QCOMPARE(transcript->segments[0].start, 62.25f);
        QCOMPARE(transcript->segments[0].end, 63.0f);
        // No language block in this body: empty, never the string "unknown".
        QVERIFY(transcript->language.empty());
    }

    void refusesWhatIsNotATranscript() {
        QString error;
        QVERIFY(!parseWhisperJson(QByteArray("not json at all"), &error).has_value());
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(!parseWhisperJson(QByteArray(R"({"nope": 1})"), &error).has_value());
        QVERIFY(error.contains(QStringLiteral("transcription")));

        // Null error pointer is legal: the parse must not depend on it.
        QVERIFY(!parseWhisperJson(QByteArray("[1,2,3]")).has_value());
    }

    void emptyTranscriptIsARealAnswer() {
        // Silence, or music with no words. Not a failure.
        const auto silent = parseWhisperJson(QByteArray(R"({"transcription": []})"));
        QVERIFY(silent.has_value());
        QVERIFY(silent->empty());
        QCOMPARE(silent->wordCount(), std::size_t{0});
        QCOMPARE(silent->lastEnd(), 0.0f);

        // A blank segment is dropped rather than becoming an empty lyric line.
        const auto blank =
                parseWhisperJson(QByteArray(R"({"transcription": [{"text": "   "}]})"));
        QVERIFY(blank.has_value());
        QVERIFY(blank->empty());
    }

    void convertsToRenderableLyrics() {
        const auto transcript = parseWhisperJson(QByteArray(kTwoSegments));
        QVERIFY(transcript.has_value());
        const LyricsData lyrics = toLyricsData(*transcript, "clip-1");

        QCOMPARE(QString::fromStdString(lyrics.source), QStringLiteral("whisper"));
        QCOMPARE(QString::fromStdString(lyrics.songId), QStringLiteral("clip-1"));
        QVERIFY(lyrics.isSynced);
        QCOMPARE(lyrics.lineCount(), std::size_t{2});
        QCOMPARE(lyrics.duration, 2.0f);
        QCOMPARE(lyrics.lines[1].words.size(), std::size_t{2});
        QCOMPARE(QString::fromStdString(lyrics.lines[1].text), QStringLiteral("world again"));

        // Confidence carries the interpolation flag through to the renderer.
        QCOMPARE(lyrics.lines[0].words[0].confidence, 1.0f);
        QCOMPARE(lyrics.lines[1].words[0].confidence, 0.5f);

        // The lines the renderer resolves are the ones built above.
        QCOMPARE(lyrics.findLineIndex(1.5f), 1);
        QCOMPARE(lyrics.findLineIndex(0.2f), -1);
    }

    void optionsDefaultToProbingPath() {
        // No options set: findExecutable must answer without crashing, and an
        // empty answer simply means the CLI is not installed here.
        const WhisperTranscriber transcriber;
        const QString executable = transcriber.findExecutable();
        QVERIFY(executable.isEmpty() || QFileInfo::exists(executable));
        QVERIFY(!transcriber.isRunning());

        WhisperOptions options;
        options.executable = QStringLiteral("/definitely/not/here");
        options.modelPath = QStringLiteral("/definitely/not/a/model.bin");
        WhisperTranscriber configured;
        configured.setOptions(options);
        QCOMPARE(configured.findExecutable(), options.executable);
        QVERIFY(!configured.isAvailable());
    }
};

int runTestWhisperTranscriber(int argc, char** argv) {
    TestWhisperTranscriber test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_WhisperTranscriber.moc"
