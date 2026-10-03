#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "EncoderSettings.hpp"
#include "VideoRecorderFFmpeg.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

namespace vc {

class AudioQueue;
class VideoRecorderThread;

/// Why the recorder is in its current state.
enum class RecordingState {
  Stopped,     ///< Idle: nothing running. The initial state, and the resting
               ///< state after stop().
  Starting,    ///< A worker is being brought up. Transient and synchronous.
  Recording,   ///< Reached *only* when the muxer was genuinely opened and its
               ///< header written.
  Stopping,    ///< stop() was asked for.
  Finalizing,  ///< Draining encoders and writing the trailer.
  /// The recording never began: the output could not be opened, a codec could
  /// not be opened, or the header could not be written. Holds no worker, and is
  /// a legal state to start from, so one failure cannot brick the recorder.
  Error
};

struct RecordingStats {
  Duration elapsed{0};
  u64 framesWritten{0};
  u64 framesDropped{0};
  /// Audio frames the resampler refused, which were therefore left out of the
  /// file. Distinct from framesDropped, which is the *capture* queue
  /// overflowing: both cost audio, but one is upstream of the encoder and one is
  /// inside it, and only this one means the file itself has a gap in it.
  ///
  /// Published live by the same one-second stats block that publishes
  /// framesDropped, and finalised by stop(). It used to be assigned *only* at
  /// stop, which made it unreadable for the whole recording: a value of 0 could
  /// not be distinguished from "not measured yet", and a bridge polling it saw
  /// a counter that jumped from 0 to N at teardown.
  u64 audioFramesDropped{0};
  u64 bytesWritten{0};
  f64 avgFps{0.0};
  f64 encodingFps{0.0};
  std::string currentFile;
};

/**
 * @brief Compose what `VideoRecorder::stop()` tells the user, from its inputs.
 *
 * A pure function of three values: whether the muxer damaged the file, how many
 * frames reached it, and what the resampler cost. Empty means "nothing to say" --
 * a clean recording reports success and this is the empty string.
 *
 * Extracted from `stop()` because the composition *is* the behaviour, and inside
 * `stop()` it was unreachable: the two facts come from a worker that has already
 * been joined and released, so producing them meant a real recording, a real
 * queue and a real race, and the one combination worth testing -- both problems
 * at once -- was structurally impossible to arrange, because `encodeAudio`
 * short-circuits once the muxer has refused anything.
 *
 * Nothing here logs. `stop()` keeps every `LOG_*` it had, because a log line and
 * a user-facing sentence are different audiences and collapsing them is how the
 * two drift apart.
 *
 * The wording distinction is the load-bearing part. "The file is incomplete" means
 * *corruption*: the container is damaged and will not play to the end. A resampler
 * failure does not do that -- the container, the index and the audio stream all
 * stay valid and the result decodes, with gaps in the sound. Reusing the
 * corruption wording for a degradation would be a lie in the other direction: a
 * user told the file is incomplete, who finds it plays fine, will not trust
 * anything this application says afterwards. So the degraded case is described as
 * what it is, and the severity is graded -- gaps in the audio, versus no audio at
 * all. When both happened they are named in one string, in that order, because
 * two messages the user has to correlate in time is how one of them gets missed.
 */
std::string composeStopReason(bool writeFailed, u64 framesWritten,
                              const VideoRecorderFFmpeg::AudioResampleReport& audio);

class VideoRecorder {
public:
  VideoRecorder();
  ~VideoRecorder();

  /**
   * @brief Begin a recording, optionally muxing a karaoke subtitle track.
   *
   * @param assSubtitle  A complete Advanced SubStation Alpha document, UTF-8,
   *                     as built by `vc::LyricsExport::toAssDocument`. Opaque
   *                     here on purpose: nothing in `src/recorder/` names a
   *                     lyrics type, and the caller -- the layer that has both
   *                     the lyrics and the recorder -- decides whether one
   *                     exists. Empty means "no lyrics", and is the default so
   *                     every existing call site and test is unaffected.
   *
   * Borrowed, not retained, and the borrow is confined to this call: it is
   * forwarded straight to `VideoRecorderThread::start` and consumed by
   * `VideoRecorderFFmpeg::init`, which copies what it needs before returning.
   * Both of those run synchronously on the calling thread, *before*
   * `VideoRecorderThread::start` constructs its JThread, so no view can
   * outlive the buffer that owns it and none can reach the encoding thread.
   * A missing or unusable document is never a start failure -- the muxer drops
   * the track and the file records normally.
   */
  Result<void> start(const EncoderSettings& settings,
                     std::string_view assSubtitle = {});
  Result<void> start(const fs::path& outputPath,
                     std::string_view assSubtitle = {});
  Result<void> stop();

  void submitVideoFrame(std::vector<u8>&& data, u32 width, u32 height, i64 timestamp);
  void submitVideoFrame(const u8* data, u32 width, u32 height, i64 timestamp);

  void setAudioQueue(AudioQueue* queue);

    /// Test seam, forwarded to the encoding worker. See
    /// VideoRecorderFFmpeg::simulateWriteFailureForTesting: the refusal is
    /// injected at the write, not declared, so the immediate case is "the very
    /// first packet is refused" rather than a flag set behind production's back.
    void simulateWriteFailureForTesting();

    /// Test seam, forwarded to the encoding worker. See
    /// VideoRecorderFFmpeg::simulateResampleFailureForTesting.
    void simulateResampleFailureForTesting(u32 frames);

  RecordingState state() const { return state_; }
  bool isRecording() const { return state_ == RecordingState::Recording; }
  const RecordingStats& stats() const { return stats_; }
  RecordingStats getCurrentStats() const;
  const EncoderSettings& settings() const { return settings_; }

  Signal<RecordingState> stateChanged;
  Signal<const RecordingStats&> statsUpdated;
  Signal<std::string> error;

private:
  friend class VideoRecorderThread;

  std::atomic<RecordingState> state_{RecordingState::Stopped};
  // Keep the configured queue even before a worker exists; start() forwards it
  // to the newly-created worker.  This makes attachment order irrelevant.
  std::atomic<AudioQueue*> audioQueue_{nullptr};
  EncoderSettings settings_;
  RecordingStats stats_;
  std::unique_ptr<VideoRecorderThread> worker_;
};

}
