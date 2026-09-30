#pragma once
#include <atomic>
#include <memory>
#include <string_view>
#include <vector>
#include "EncoderSettings.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

namespace vc {

class AudioQueue;
class VideoRecorderThread;

enum class RecordingState { Stopped, Starting, Recording, Stopping, Finalizing, Error };

struct RecordingStats {
  Duration elapsed{0};
  u64 framesWritten{0};
  u64 framesDropped{0};
  u64 bytesWritten{0};
  f64 avgFps{0.0};
  f64 encodingFps{0.0};
  std::string currentFile;
};

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
