#pragma once
// WhisperTranscriber.hpp - fill in lyrics (and their word timings) from audio.
//
// WHY THIS EXISTS
// Suno's own karaoke path is one product and it fails often: an uploaded track, a
// clip generated before aligned lyrics existed, or a 403 on the alignment route
// leaves a song with TEXT and no timing, or with no text at all. The karaoke
// renderer and the ASS burn-in can already consume word timings; nothing in the
// tree could produce them locally. `lyrics/LyricTiming.hpp` states the gap
// outright: an aligner needs an acoustic model, and it refuses to fabricate one
// by assigning the Nth onset to the Nth word.
//
// This is that acoustic model, reached the cheapest honest way: whisper.cpp's own
// CLI (`whisper-cli`, previously `main`) as a child process. Not linked, not
// vendored. A subprocess keeps a multi-hundred-megabyte model and a C++ library
// with its own build out of this one, keeps the licence boundary clean, and means
// the feature is absent rather than broken when the binary is missing.
//
// GRANULARITY, HONESTLY STATED
// whisper reports SEGMENTS. With `-ml 1` (default here) a segment is about one
// word, which is what the karaoke path wants. When it is not - a fast run of
// syllables comes back as one segment - the words inside that segment are spread
// evenly across the segment's [start, end]. That is interpolation inside a range
// whisper itself measured, NOT onset detection, and it is labelled as such
// (`WhisperWord::interpolated`) so a caller can decline to show it as precise.
//
// WHAT THIS DOES NOT DO
// It does not align whisper's text to Suno's lyrics. Two texts from two sources
// are a forced-alignment problem (that is slower-whisper's job, not whisper.cpp's),
// and guessing a mapping would produce exactly the fabricated timing
// LyricTiming.hpp refuses. So this is for a song with NO timing, and for a song
// with NO text. Filling in missing timings for text you already have is a
// different, harder task and is not claimed here.

#include "LyricsData.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QProcess;

namespace vc::lyrics {

/// One word inside a whisper segment.
struct WhisperWord {
    std::string text;
    f32 start{0.0f}; ///< seconds from the start of the file
    f32 end{0.0f};
    /// True when this word's bounds were divided out of a multi-word segment
    /// rather than measured. Never silently equal to a measured word.
    bool interpolated{false};
};

/// One whisper segment (`-ml 1` makes these roughly words).
struct WhisperSegment {
    std::string text;
    f32 start{0.0f};
    f32 end{0.0f};
    std::vector<WhisperWord> words;
};

struct WhisperTranscript {
    std::vector<WhisperSegment> segments;
    std::string language; ///< from `result.language`, else `params.language`
    std::string model;    ///< from `model.type`, e.g. "large-v3-turbo"
    bool truncated{false}; ///< whisper stopped at its own audio limit

    [[nodiscard]] bool empty() const { return segments.empty(); }
    [[nodiscard]] std::size_t wordCount() const;
    /// End of the last segment, 0 when empty. The natural LyricsData duration.
    [[nodiscard]] f32 lastEnd() const;
};

/// What to run and how.
struct WhisperOptions {
    /// Explicit CLI path. Empty means "probe PATH" (see findExecutable).
    QString executable;
    /// Path to a ggml model (`ggml-large-v3-turbo.bin`). Required.
    QString modelPath;
    /// ISO code, or "auto" to let whisper detect it.
    QString language{QStringLiteral("auto")};
    /// 0 = let whisper decide from the machine.
    int threads{0};
    /// Send `-ml 1` so segments are word-sized. Turning this off asks for whole
    /// clauses, which is cheaper and loses the karaoke timing.
    bool wordTimestamps{true};
};

/// Parse whisper.cpp's `-oj` output (`{"transcription":[{"offsets":{…},…}]}`).
///
/// Separate from the process driver on purpose: it is the only part with a
/// contract worth testing, and it is testable with no binary and no model.
/// `error` receives a sentence fit to show a user when the parse fails.
[[nodiscard]] std::optional<WhisperTranscript>
parseWhisperJson(const QByteArray& json, QString* error = nullptr);

/// Convert a transcript to the renderer's model: one LyricsLine per segment,
/// words attached, `isSynced` set only where timing actually exists.
[[nodiscard]] LyricsData toLyricsData(const WhisperTranscript& transcript,
                                      const std::string& songId = {});

/// Drives one transcription at a time.
///
/// One at a time is deliberate: whisper is CPU-bound and already saturates the
/// cores it is given, so a queue of parallel jobs would make every one of them
/// slower on the reference machine (2 physical cores).
class WhisperTranscriber : public QObject {
    Q_OBJECT

public:
    explicit WhisperTranscriber(QObject* parent = nullptr);
    ~WhisperTranscriber() override;

    void setOptions(const WhisperOptions& options);
    [[nodiscard]] const WhisperOptions& options() const { return options_; }

    /// The CLI to run: `options.executable` when it exists, else the first of
    /// `whisper-cli` / `whisper.cpp` / `whisper` found on PATH. Empty when none.
    [[nodiscard]] QString findExecutable() const;

    /// The PATH probe alone, with no options involved. Exposed because a caller
    /// that has not chosen a model yet can still answer "is the CLI installed"
    /// without constructing a transcriber to ask it.
    [[nodiscard]] static QString probeExecutable();

    /// Both a CLI and a readable model file are present.
    [[nodiscard]] bool isAvailable() const;

    /// Start on a local audio file (any format the CLI's own FFmpeg reads).
    /// A second call while running is refused with `failed`, never queued.
    void transcribe(const QString& audioPath);

    /// Stop the child process. Safe to call when nothing is running.
    void cancel();

    [[nodiscard]] bool isRunning() const { return running_; }

signals:
    void started(const QString& audioPath);
    /// `transcript` is emitted by value: it is small, and a caller that stores
    /// it must not be handed a reference into a QProcess callback.
    void finished(const vc::lyrics::WhisperTranscript& transcript);
    void failed(const QString& reason);
    /// whisper-cli's own stderr percentage, 0-100.
    void progressChanged(int percent);

private:
    void onFinished(int exitCode, int exitStatus);
    void onReadyRead();

    WhisperOptions options_;
    QProcess* process_{nullptr};
    QString audioPath_;
    QString tempDir_;
    QString outputPrefix_;
    QByteArray logTail_;
    bool running_{false};
};

} // namespace vc::lyrics
