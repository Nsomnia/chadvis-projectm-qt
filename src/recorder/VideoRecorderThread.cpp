// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Video recorder thread implementation with lock-free queue

#include "VideoRecorderThread.hpp"
#include "audio/AudioQueue.hpp"
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
        LOG_ERROR("Failed to initialize FFmpeg: {}", res.error().message);
        parent_.error.emitSignal(res.error().message);
        return;
    }
    
    // Reset stats atomically
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = RecordingStats{};
        stats_.currentFile = settings_.outputPath.string();
    }
    
    thread_ = std::jthread([this](std::stop_token st) { threadLoop(st); });
    LOG_INFO("Recording thread started: {}", settings_.outputPath.string());
}

void VideoRecorderThread::stop() {
    shouldStop_ = true;
    frameGrabber_.stop();
    thread_.request_stop();
    if (thread_.joinable())
        thread_.join();

    // Final flush
    u64 bytes = 0;
    ffmpeg_.flush(bytes);
    
    // Update final stats
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.bytesWritten += bytes;
    }
    
    ffmpeg_.cleanup();
    LOG_INFO("Recording thread stopped");
}

void VideoRecorderThread::pushVideoFrame(GrabbedFrame frame) {
    frameGrabber_.pushFrame(std::move(frame));
}

RecordingStats VideoRecorderThread::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void VideoRecorderThread::threadLoop(std::stop_token stopToken) {
    LOG_DEBUG("Encoding thread started");
    auto startTime = std::chrono::steady_clock::now();
    auto lastStatsUpdate = startTime;
    u64 lastFramesWritten = 0;
    
    while (!stopToken.stop_requested()) {
        GrabbedFrame frame;
        bool hasVideo = frameGrabber_.getNextFrame(frame, 10);

        u64 bytesWritten = 0;
        bool hadError = false;

        if (hasVideo) {
            if (ffmpeg_.encodeVideo(frame, bytesWritten)) {
                std::lock_guard<std::mutex> lock(statsMutex_);
                ++stats_.framesWritten;
                stats_.bytesWritten += bytesWritten;
            } else {
                hadError = true;
                LOG_WARN("Failed to encode video frame");
        }
    }

    // Pop audio from lock-free queue (no mutex)
    if (audioQueue_) {
        static constexpr usize AUDIO_BATCH_SIZE = 4096;
        alignas(64) float audioBatch[AUDIO_BATCH_SIZE * 2];
        u32 popped = audioQueue_->popRecBatch(audioBatch, AUDIO_BATCH_SIZE);
        if (popped > 0) {
            std::vector<f32> audioBuffer(audioBatch, audioBatch + popped * 2);
            if (!ffmpeg_.encodeAudio(audioBuffer, 2, bytesWritten)) {
                hadError = true;
                LOG_WARN("Failed to encode audio samples");
            } else {
                std::lock_guard<std::mutex> statsLock(statsMutex_);
                stats_.bytesWritten += bytesWritten;
            }
        }
    }

    // Update dropped frames count
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.framesDropped = frameGrabber_.droppedFrames();
        }

        // Stop condition: requested stop AND queues empty
        if (shouldStop_ && !hasVideo && !frameGrabber_.hasFrames()) {
            break;
        }

        // Stats update every second
        auto now = std::chrono::steady_clock::now();
        if (now - lastStatsUpdate >= std::chrono::seconds(1)) {
            updateStats(startTime, lastFramesWritten, lastStatsUpdate);
            lastStatsUpdate = now;
        }
        
        // Emit error signal if there were encoding issues
        if (hadError) {
            parent_.error.emitSignal("Encoding error occurred - check logs");
        }
    }
    
    // Final stats update
    updateStats(startTime, lastFramesWritten, lastStatsUpdate, true);
    
    LOG_DEBUG("Encoding thread finishing");
}

void VideoRecorderThread::updateStats(TimePoint startTime,
                                      u64& lastFramesWritten,
                                      TimePoint& lastUpdate,
                                      bool isFinal) {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    // Calculate elapsed time
    stats_.elapsed = std::chrono::duration_cast<Duration>(now - startTime);
    
    // Calculate FPS over the last interval
    auto intervalSecs = std::chrono::duration<f64>(now - lastUpdate).count();
    if (intervalSecs > 0) {
        u64 framesDelta = stats_.framesWritten - lastFramesWritten;
        stats_.avgFps = static_cast<f64>(framesDelta) / intervalSecs;
        
        // Also calculate overall encoding FPS
        auto totalSecs = std::chrono::duration<f64>(stats_.elapsed).count();
        if (totalSecs > 0) {
            stats_.encodingFps = static_cast<f64>(stats_.framesWritten) / totalSecs;
        }
    }
    
    lastFramesWritten = stats_.framesWritten;
    
    // Emit stats update signal (thread-safe via Qt queued connection in UI)
    if (!isFinal || stats_.framesWritten > 0) {
        parent_.statsUpdated.emitSignal(stats_);
    }
}

} // namespace vc
