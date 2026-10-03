#include "VideoRecorderCore.hpp"
#include "VideoRecorderThread.hpp"
#include "audio/AudioQueue.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include <fmt/core.h>

namespace vc {

namespace {

/// The corruption half. Its own function only so the "at frame N" sentence can
/// be formatted once and stay identical wherever it is composed.
std::string corruptionClause(const u64 framesWritten) {
  return fmt::format(
      "Recording failed at frame {} -- the file is incomplete. The most "
      "common cause is a full volume.",
      framesWritten);
}

/// The degradation half, graded by whether anything survived.
std::string audioClause(const VideoRecorderFFmpeg::AudioResampleReport& audio) {
  return audio.anyFrameWritten
             ? fmt::format(
                   "The audio is degraded: {} frames could not be resampled and "
                   "were left out, so there are gaps in the sound. The video and "
                   "the rest of the file are intact and it plays normally.",
                   audio.droppedFrames)
             : fmt::format(
                   "The recording has no audio: all {} audio frames could not "
                   "be resampled. The video is intact and plays normally.",
                   audio.droppedFrames);
}

} // namespace

std::string composeStopReason(bool writeFailed, const u64 framesWritten,
                              const VideoRecorderFFmpeg::AudioResampleReport& audio) {
  std::string reasons;

  if (writeFailed) {
    reasons = corruptionClause(framesWritten);
  }

  if (audio.droppedFrames > 0) {
    const std::string degraded = audioClause(audio);
    reasons = reasons.empty() ? degraded : reasons + " " + degraded;
  }

  return reasons;
}

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder() {
  (void)stop();
  audioQueue_.store(nullptr, std::memory_order_release);
}

Result<void> VideoRecorder::start(const EncoderSettings& settings,
                                 std::string_view assSubtitle) {
  // Error is an acceptable starting state, not a rejection. It is where a
  // recording that failed to *begin* now ends up, and it holds no worker, so
  // refusing to start from it would leave the recorder permanently unusable
  // after one unwritable output directory -- a worse failure than the one it
  // reported. Anything else really is a recording in progress.
  if (state_ != RecordingState::Stopped && state_ != RecordingState::Error) {
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

  // Defensive: nothing should be reachable in Error with a live worker, but if it
  // ever is, the worker's destructor finalises and frees it before we make a new
  // one. Freeing here keeps the invariant "Error holds no worker" true by
  // construction rather than by convention.
  worker_.reset();

  worker_ = std::make_unique<VideoRecorderThread>(*this, settings_);
  if (AudioQueue* queue = audioQueue_.load(std::memory_order_acquire)) {
    worker_->setAudioQueue(queue);
  }
  // Straight through, no copy and no member: worker_->start() consumes the view
  // synchronously on this thread before it creates its JThread, and
  // ffmpeg_.init() copies the script and every cue body out of it. Anything else
  // would either be a redundant copy of a few KB or a dangling borrow.
  if (auto started = worker_->start(assSubtitle); !started) {
    // The worker holds a muxer that may be half built. Releasing it runs
    // ~VideoRecorderThread -> stop() -> flush()/cleanup(), and both are now gated
    // on headerWritten_, so nothing is written into a context that never had a
    // header. Without this release the encoder would survive until the next
    // start(), which then made a second worker on top of it.
    worker_.reset();

    state_ = RecordingState::Error;
    // Counters zeroed *before* the state change is announced, because
    // RecordingBridge::onStateChanged snapshots the stats at exactly that
    // moment; announcing first would let it cache the Starting snapshot and then
    // overwrite it a moment later. currentFile is deliberately kept -- the path
    // the recorder tried is useful in the error, and it is not a recording.
    stats_.framesWritten = 0;
    stats_.bytesWritten = 0;
    stats_.framesDropped = 0;
    stats_.elapsed = Duration{0};
    stateChanged.emitSignal(state_);
    statsUpdated.emitSignal(stats_);

    LOG_ERROR("Recording failed to start: {}", started.error().message);
    return started;
  }

  // Only now. The old code set Recording unconditionally, so an init that had
  // just failed left the UI with an active recording, a running timer and no
  // file -- and the caller had a Result<void>::ok() it had to disbelieve.
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
  // Error is reachable here too: a start that failed leaves the recorder in it,
  // and stop() has to move it back to Stopped or the UI has no way back to idle.
  // There is no worker in that state, so the body below is a clean no-op that
  // still performs the transitions the UI listens for.
  if (state_ != RecordingState::Recording && state_ != RecordingState::Error) {
    return Result<void>::ok();
  }

  LOG_DEBUG("VideoRecorder::stop() requested");
  state_ = RecordingState::Stopping;
  stateChanged.emitSignal(state_);

  state_ = RecordingState::Finalizing;
  stateChanged.emitSignal(state_);

  // Read before the worker is released; both are snapshots taken in its stop().
  bool writeFailed = false;
  VideoRecorderFFmpeg::AudioResampleReport audioResample;
  if (worker_) {
    worker_->stop();
    writeFailed = worker_->writeFailed();
    audioResample = worker_->audioResample();
    // Preserve the worker's final counters before releasing it.  The bridge
    // reads these after the Stopped transition, so final frame/file stats must
    // not fall back to the zeroed parent snapshot.
    stats_ = worker_->getStats();
    worker_.reset();
  }

  state_ = RecordingState::Stopped;
  stateChanged.emitSignal(state_);

  // One string, naming both problems when both happened, so the user is not left
  // correlating two separate messages. Composed by a pure function so the wording
  // -- in particular the corruption/degradation distinction -- is testable
  // without a worker, a queue or a fault injection; every LOG_* stays here, next
  // to the snapshot it describes. See composeStopReason.
  //
  // Returned as a failure so RecordingBridge::stopRecording's existing
  // `if (!result) emit recordingError(...)` puts it in front of the user with no
  // change to the bridge. The file exists and opens, which is exactly why this has
  // to be said out loud: a truncated recording is otherwise indistinguishable
  // from a good one until someone watches it to the end.
  const std::string reasons =
      composeStopReason(writeFailed, stats_.framesWritten, audioResample);

  if (writeFailed) {
    LOG_ERROR("Recording stopped with an incomplete file at {} frames",
              stats_.framesWritten);
  }

  if (audioResample.droppedFrames > 0) {
    LOG_WARN("Recording stopped with a degraded audio track: {} frames dropped",
             audioResample.droppedFrames);
  }

  if (!reasons.empty()) {
    return Result<void>::err(reasons);
  }

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

void VideoRecorder::simulateWriteFailureForTesting() {
  if (worker_) {
    worker_->simulateWriteFailureForTesting();
  }
}

void VideoRecorder::simulateResampleFailureForTesting(u32 frames) {
  if (worker_) {
    worker_->simulateResampleFailureForTesting(frames);
  }
}

void VideoRecorder::setAudioQueue(AudioQueue* queue) {
  audioQueue_.store(queue, std::memory_order_release);
  if (worker_) {
    worker_->setAudioQueue(queue);
  }
}

}
