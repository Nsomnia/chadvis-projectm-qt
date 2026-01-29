/**
 * @file VideoRecorderThread.hpp
 * @brief Recording thread management and encoding loop.
 *
 * This file defines the VideoRecorderThread class which handles the
 * asynchronous encoding process. It consumes video frames from a
 * FrameGrabber and audio samples from a buffer, feeding them to
 * VideoRecorderFFmpeg for encoding.
 *
 * @section Patterns
 * - Producer-Consumer: Consumes frames/samples produced by the main/audio
 * threads.
 * - RAII: Manages the lifecycle of the encoding thread using std::jthread.
 * - Thread-Safe Stats: Uses mutex-protected stats for safe cross-thread access.
 */

#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "FrameGrabber.hpp"
#include "VideoRecorderCore.hpp"
#include "VideoRecorderFFmpeg.hpp"

namespace vc {

class VideoRecorderThread {
public:
    VideoRecorderThread(VideoRecorder& parent, const EncoderSettings& settings);
    ~VideoRecorderThread();

    void start();
    void stop();

    // Data input
    void pushVideoFrame(GrabbedFrame frame);
    void pushAudioSamples(const f32* data,
                          usize size,
                          u32 channels,
                          u32 sampleRate);
    
    // Thread-safe stats access
    RecordingStats getStats() const;

private:
    using TimePoint = std::chrono::steady_clock::time_point;
    
    void threadLoop(std::stop_token stopToken);
    void updateStats(TimePoint startTime,
                     u64& lastFramesWritten,
                     TimePoint& lastUpdate,
                     bool isFinal = false);

    VideoRecorder& parent_;
    EncoderSettings settings_;

    std::jthread thread_;
    std::atomic<bool> shouldStop_{false};

    FrameGrabber frameGrabber_;
    VideoRecorderFFmpeg ffmpeg_;

    // Audio buffering
    std::vector<f32> audioBuffer_;
    std::mutex audioMutex_;
    u32 audioSampleRate_{48000};
    u32 audioChannels_{2};
    
    // Thread-safe stats
    mutable std::mutex statsMutex_;
    RecordingStats stats_;
};

} // namespace vc
