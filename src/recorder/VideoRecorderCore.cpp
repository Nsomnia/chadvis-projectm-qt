#include "VideoRecorderCore.hpp"
#include "VideoRecorderThread.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder() {
    stop();
}

Result<void> VideoRecorder::start(const EncoderSettings& settings) {
    if (state_ != RecordingState::Stopped) {
        return Result<void>::err("Recording already in progress");
    }

    if (auto result = settings.validate(); !result) {
        return result;
    }

    settings_ = settings;
    file::ensureDir(settings_.outputPath.parent_path());

    state_ = RecordingState::Starting;
    stateChanged.emitSignal(state_);

    stats_ = RecordingStats{};
    stats_.currentFile = settings_.outputPath.string();
    statsUpdated.emitSignal(stats_);

    worker_ = std::make_unique<VideoRecorderThread>(*this, settings_);
    worker_->start();

    state_ = RecordingState::Recording;
    stateChanged.emitSignal(state_);

    LOG_INFO("Recording started: {}", settings_.outputPath.string());
    return Result<void>::ok();
}

Result<void> VideoRecorder::start(const fs::path& outputPath) {
    auto settings = EncoderSettings::fromConfig();
    settings.outputPath = outputPath;
    return start(settings);
}

Result<void> VideoRecorder::stop() {
    if (state_ != RecordingState::Recording) {
        return Result<void>::ok();
    }

    LOG_DEBUG("VideoRecorder::stop() requested");
    state_ = RecordingState::Stopping;
    stateChanged.emitSignal(state_);

    if (worker_) {
        worker_->stop();
        worker_.reset();
    }

    state_ = RecordingState::Stopped;
    stateChanged.emitSignal(state_);

    LOG_INFO("Recording stopped. Frames: {}, Dropped: {}",
             stats_.framesWritten,
             stats_.framesDropped);

    return Result<void>::ok();
}

void VideoRecorder::submitVideoFrame(std::vector<u8>&& data,
                                     u32 width,
                                     u32 height,
                                     i64 timestamp) {
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
                                     u32 width,
                                     u32 height,
                                     i64 timestamp) {
    if (state_ != RecordingState::Recording || !worker_)
        return;

    GrabbedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.timestamp = timestamp;
    frame.data.assign(data, data + width * height * 4);

    worker_->pushVideoFrame(std::move(frame));
}

void VideoRecorder::submitAudioSamples(const f32* data,
                                       u32 samples,
                                       u32 channels,
                                       u32 sampleRate) {
    if (state_ != RecordingState::Recording || !worker_)
        return;

    worker_->pushAudioSamples(data, samples * channels, channels, sampleRate);
}

} // namespace vc
