#include "VideoRecorderCore.hpp"
#include "VideoRecorderThread.hpp"
#include "audio/AudioQueue.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder() {
  (void)stop();
  audioQueue_.store(nullptr, std::memory_order_release);
}

Result<void> VideoRecorder::start(const EncoderSettings& settings,
                                 std::string_view assSubtitle) {
  if (state_ != RecordingState::Stopped) {
    return Result<void>::err("Recording already in progress");
  }

  if (auto result = settings.validate(); !result) {
    return result;
  }

  settings_ = settings;
  (void)file::ensureDir(settings_.outputPath.parent_path());

  state_ = RecordingState::Starting;
  stateChanged.emitSignal(state_);

  stats_ = RecordingStats{};
  stats_.currentFile = settings_.outputPath.string();
  statsUpdated.emitSignal(stats_);

  worker_ = std::make_unique<VideoRecorderThread>(*this, settings_);
  if (AudioQueue* queue = audioQueue_.load(std::memory_order_acquire)) {
    worker_->setAudioQueue(queue);
  }
  // Straight through, no copy and no member: worker_->start() consumes the view
  // synchronously on this thread before it creates its JThread, and
  // ffmpeg_.init() copies the script and every cue body out of it. Anything else
  // would either be a redundant copy of a few KB or a dangling borrow.
  worker_->start(assSubtitle);

  state_ = RecordingState::Recording;
  stateChanged.emitSignal(state_);

  stats_.currentFile = worker_->getOutputPath();
  statsUpdated.emitSignal(stats_);

  LOG_INFO("Recording started: {}", stats_.currentFile);
  return Result<void>::ok();
}

Result<void> VideoRecorder::start(const fs::path& outputPath,
                                 std::string_view assSubtitle) {
  auto settings = EncoderSettings::fromConfig();
  if (auto container = EncoderSettings::containerFromPath(outputPath)) {
    settings.container = *container;
  }
  settings.outputPath = EncoderSettings::outputPathForContainer(
    outputPath, settings.container);
  return start(settings, assSubtitle);
}

Result<void> VideoRecorder::stop() {
  if (state_ != RecordingState::Recording) {
    return Result<void>::ok();
  }

  LOG_DEBUG("VideoRecorder::stop() requested");
  state_ = RecordingState::Stopping;
  stateChanged.emitSignal(state_);

  state_ = RecordingState::Finalizing;
  stateChanged.emitSignal(state_);

  if (worker_) {
    worker_->stop();
    // Preserve the worker's final counters before releasing it.  The bridge
    // reads these after the Stopped transition, so final frame/file stats must
    // not fall back to the zeroed parent snapshot.
    stats_ = worker_->getStats();
    worker_.reset();
  }

  state_ = RecordingState::Stopped;
  stateChanged.emitSignal(state_);

  LOG_INFO("Recording stopped. Frames: {}, Dropped: {}",
    stats_.framesWritten, stats_.framesDropped);

  return Result<void>::ok();
}

void VideoRecorder::submitVideoFrame(std::vector<u8>&& data,
  u32 width, u32 height, i64 timestamp) {
  if (state_ != RecordingState::Recording || !worker_)
    return;

  GrabbedFrame frame;
  frame.width = width;
  frame.height = height;
  frame.timestamp = timestamp;
  frame.data = std::move(data);

  worker_->pushVideoFrame(std::move(frame));
}

void VideoRecorder::submitVideoFrame(const u8* data,
  u32 width, u32 height, i64 timestamp) {
  if (state_ != RecordingState::Recording || !worker_)
    return;

  GrabbedFrame frame;
  frame.width = width;
  frame.height = height;
  frame.timestamp = timestamp;
  frame.data.assign(data, data + width * height * 4);

  worker_->pushVideoFrame(std::move(frame));
}

RecordingStats VideoRecorder::getCurrentStats() const {
  if (worker_ && state_ == RecordingState::Recording) {
    return worker_->getStats();
  }
  return stats_;
}

void VideoRecorder::setAudioQueue(AudioQueue* queue) {
  audioQueue_.store(queue, std::memory_order_release);
  if (worker_) {
    worker_->setAudioQueue(queue);
  }
}

}
