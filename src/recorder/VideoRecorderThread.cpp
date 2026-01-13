#include "VideoRecorderThread.hpp"
#include "core/Logger.hpp"

namespace vc {

VideoRecorderThread::VideoRecorderThread(VideoRecorder& parent,
                                         const EncoderSettings& settings)
    : parent_(parent), settings_(settings) {
    frameGrabber_.setSize(settings.video.width, settings.video.height);
}

VideoRecorderThread::~VideoRecorderThread() {
    stop();
}

void VideoRecorderThread::start() {
    frameGrabber_.start();
    if (auto res = ffmpeg_.init(settings_); !res) {
        parent_.error.emitSignal(res.error().message);
        return;
    }
    thread_ = std::jthread([this](std::stop_token st) { threadLoop(st); });
}

void VideoRecorderThread::stop() {
    shouldStop_ = true;
    frameGrabber_.stop();
    thread_.request_stop();
    if (thread_.joinable())
        thread_.join();

    // Final flush
    u64 bytes = 0;
    ffmpeg_.flush(bytes); // We ignore bytes here as stats are final
    ffmpeg_.cleanup();
}

void VideoRecorderThread::pushVideoFrame(GrabbedFrame frame) {
    frameGrabber_.pushFrame(std::move(frame));
}

void VideoRecorderThread::pushAudioSamples(const f32* data,
                                           usize size,
                                           u32 channels,
                                           u32 sampleRate) {
    std::lock_guard lock(audioMutex_);
    audioSampleRate_ = sampleRate;
    audioChannels_ = channels;
    audioBuffer_.insert(audioBuffer_.end(), data, data + size);
}

void VideoRecorderThread::threadLoop(std::stop_token stopToken) {
    LOG_DEBUG("Encoding thread started");
    auto startTime = std::chrono::steady_clock::now();
    auto lastStatsUpdate = startTime;

    while (!stopToken.stop_requested()) {
        GrabbedFrame frame;
        bool hasVideo = frameGrabber_.getNextFrame(frame, 10);

        u64 bytesWritten = 0;

        if (hasVideo) {
            if (ffmpeg_.encodeVideo(frame, bytesWritten)) {
                // Video frame written
            }
        }

        {
            std::lock_guard lock(audioMutex_);
            if (!audioBuffer_.empty()) {
                ffmpeg_.encodeAudio(audioBuffer_, audioChannels_, bytesWritten);
            }
        }

        // Stop condition: requested stop AND queues empty
        if (shouldStop_ && !hasVideo && !frameGrabber_.hasFrames()) {
            break;
        }

        // Stats update
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatsUpdate >= std::chrono::seconds(1)) {
            // We need to update parent stats carefully
            // In a real refactor, stats should be thread-safe or passed via
            // message For now, we assume parent stats are only read by UI on
            // main thread but we should probably use a mutex or atomic updates
            lastStatsUpdate = now;
        }
    }
    LOG_DEBUG("Encoding thread finishing");
}

} // namespace vc
