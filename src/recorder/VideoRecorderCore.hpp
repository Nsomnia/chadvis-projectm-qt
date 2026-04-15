#pragma once
#include <atomic>
#include <memory>
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

  Result<void> start(const EncoderSettings& settings);
  Result<void> start(const fs::path& outputPath);
  Result<void> stop();

  void submitVideoFrame(std::vector<u8>&& data, u32 width, u32 height, i64 timestamp);
  void submitVideoFrame(const u8* data, u32 width, u32 height, i64 timestamp);
  void submitAudioSamples(const f32* data, u32 samples, u32 channels, u32 sampleRate);

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
  EncoderSettings settings_;
  RecordingStats stats_;
  std::unique_ptr<VideoRecorderThread> worker_;
};

}
