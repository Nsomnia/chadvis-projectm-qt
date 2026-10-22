#pragma once
// VisualizerAudioTap - decode the playing file again, for the visualizer only.
//
// The documented way to tap decoded audio from QMediaPlayer is QAudioBufferOutput,
// and Qt's own reference page closes with the constraint that matters here:
//
//     "QAudioBufferOutput is only supported with the FFmpeg backend."
//
// macOS defaults to Qt's native (darwin) backend, and a Qt multimedia build that
// ships only `libdarwinmediaplugin.dylib` has no FFmpeg backend to fall back to.
// Measured on this machine's Qt 6.12.0: `QT_MEDIA_BACKEND=ffmpeg` logs
// `Available backends QList("darwin")`, falls back, and a 1.2 s tone produces
// exactly zero `audioBufferReceived` emissions while playing normally. The result
// is the reported bug: audio plays, the visualizer renders, and projectM never
// receives a sample.
//
// This class is the fallback and nothing more. QMediaPlayer stays the only audio
// output and the master clock; the tap decodes the same *local* file with the
// repository's own FFmpeg decoder and pushes PCM into the shared AudioQueue so
// projectM (and, while recording, the encoder) sees it. It runs on the GUI thread
// and decodes one 4096-frame chunk per pump, so there is no thread and no lock.
//
// It is deliberately dormant when the player's own buffer output works: the engine
// clears its source on the first delivered buffer, so a platform with the FFmpeg
// backend pays nothing.

#include "recorder/AudioFileDecoder.hpp"
#include "util/Types.hpp"

#include <QtGlobal>

#include <memory>
#include <string>

namespace vc {

class AudioQueue;

class VisualizerAudioTap {
public:
    explicit VisualizerAudioTap(AudioQueue& queue);
    ~VisualizerAudioTap();

    VisualizerAudioTap(const VisualizerAudioTap&) = delete;
    VisualizerAudioTap& operator=(const VisualizerAudioTap&) = delete;

    /// Point the tap at the local file the player selected. A different path
    /// closes the previous decoder and starts over; re-setting the same path is a
    /// no-op, so a playlist re-selection does not restart the decode.
    void setSource(const fs::path& path);

    /// No local file (a remote entry): go dormant and release the decoder.
    void clearSource();

    /// Feed the visualizer up to ~400 ms ahead of @p positionMs (the player's
    /// clock). A seek or a drift beyond 1.5 s repositions the decoder first.
    /// No-op while dormant, ended, or failed.
    void pump(qint64 positionMs);

    [[nodiscard]] bool isActive() const noexcept;

private:
    /// How far ahead of the player the tap decodes. Large enough that one slow
    /// GUI tick cannot starve projectM, small enough that a beat is never fed
    /// visibly late.
    static constexpr qint64 kLeadMs = 400;
    /// Beyond this the player and the decode have diverged (a seek, a gapless
    /// swap, a stalled queue). Re-seek rather than decode through the gap.
    static constexpr qint64 kRealignMs = 1500;
    /// Bounds the work in one tick. Four chunks is 340 ms of audio at 48 kHz,
    /// which both catches up after a tick and costs about a millisecond.
    static constexpr int kMaxChunksPerTick = 4;

    AudioQueue& queue_;
    std::unique_ptr<AudioFileDecoder> decoder_;
    std::string source_;
    u64 framesPushed_{0};
    bool ended_{false};
    bool failed_{false};
    /// One positive line per session when the tap takes over. The engine's
    /// existing "no PCM in the visualizer queue" warning names the symptom; this
    /// names the cause and the file, which is what the next reader needs.
    bool announced_{false};
};

} // namespace vc
