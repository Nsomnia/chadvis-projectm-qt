/**
 * @file VideoRecorderThread.hpp
 * @brief Recording thread management and encoding loop.
 * @version 1.1.0
 * @last-edited 2026-03-29 12:00:00
 *
 * This file defines the VideoRecorderThread class which handles the
 * asynchronous encoding process. It consumes video frames from a
 * FrameGrabber and audio samples from a lock-free queue, feeding them to
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

class AudioQueue;

class VideoRecorderThread {
public:
    VideoRecorderThread(VideoRecorder& parent, const EncoderSettings& settings);
    ~VideoRecorderThread();

    void start();
    void stop();

    // Data input
    void pushVideoFrame(GrabbedFrame frame);

    // Set audio queue for lock-free consumption
    void setAudioQueue(AudioQueue* queue) { audioQueue_ = queue; }

    // Thread-safe stats access
    RecordingStats getStats() const;
    std::string getOutputPath() const { return actualOutputPath_; }

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    void threadLoop(std::stop_token stopToken);
    void updateStats(TimePoint startTime,
                     u64& lastFramesWritten,
                     TimePoint& lastUpdate,
                     bool isFinal = false);

    VideoRecorder& parent_;
    EncoderSettings settings_;
    std::string actualOutputPath_;

    std::jthread thread_;
    std::atomic<bool> shouldStop_{false};

    FrameGrabber frameGrabber_;
    VideoRecorderFFmpeg ffmpeg_;

    // Lock-free audio queue (replaces mutex-protected buffer)
    AudioQueue* audioQueue_{nullptr};

    // Thread-safe stats
    mutable std::mutex statsMutex_;
    RecordingStats stats_;
};

} // namespace vc
