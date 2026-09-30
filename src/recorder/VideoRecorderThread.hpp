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
#include <string_view>
#include <vector>
#include "FrameGrabber.hpp"
#include "VideoRecorderCore.hpp"
#include "VideoRecorderFFmpeg.hpp"
#include "util/JThread.hpp"

namespace vc {

class AudioQueue;

class VideoRecorderThread {
public:
    VideoRecorderThread(VideoRecorder& parent, const EncoderSettings& settings);
    ~VideoRecorderThread();

    /**
     * @brief Open the muxer, then start the encoding thread.
     *
     * @param assSubtitle  See `VideoRecorder::start`. Forwarded verbatim to
     *                     `VideoRecorderFFmpeg::init`, which copies what it
     *                     needs and returns.
     *
     * Runs on the *caller's* thread, not a new one, and that is the whole
     * lifetime argument for the borrowed view: `ffmpeg_.init` consumes it
     * synchronously on line one of the body, and `thread_` -- whose lambda
     * captures only `this` -- is not constructed until afterwards. Nothing that
     * reaches the encoding thread refers to the caller's buffer; what the worker
     * reads later is `VideoRecorderFFmpeg`'s own copies, published by the
     * thread creation that happens after they were written.
     */
    Result<void> start(std::string_view assSubtitle = {});
    void stop();

    /// Did the muxer refuse a packet for a reason that damages the file?
    ///
    /// Snapshotted from the encoder in stop(), because cleanup() resets the
    /// encoder's own flag to keep the instance reusable. Recorded per recording
    /// rather than read live, so VideoRecorder::stop can report it after the
    /// worker has been torn down.
    bool writeFailed() const { return writeFailed_; }

    /// Test seam, forwarded to the encoder. See
    /// VideoRecorderFFmpeg::simulateWriteFailureForTesting for why the failure
    /// cannot be provoked directly.
    void simulateWriteFailureForTesting() {
        ffmpeg_.simulateWriteFailureForTesting();
    }

    // Data input
    void pushVideoFrame(GrabbedFrame frame);

    // Set audio queue for lock-free consumption
    void setAudioQueue(AudioQueue* queue) {
        audioQueue_.store(queue, std::memory_order_release);
    }

    // Thread-safe stats access
    RecordingStats getStats() const;
    std::string getOutputPath() const { return actualOutputPath_; }

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    void threadLoop(StopToken stopToken);
    void updateStats(TimePoint startTime,
                     u64& lastFramesWritten,
                     TimePoint& lastUpdate,
                     bool isFinal = false);

    VideoRecorder& parent_;
    EncoderSettings settings_;
    std::string actualOutputPath_;

    JThread thread_;
    std::atomic<bool> shouldStop_{false};

    FrameGrabber frameGrabber_;
    VideoRecorderFFmpeg ffmpeg_;

    // Lock-free audio queue (replaces mutex-protected buffer).  The pointer is
    // atomic because the recorder may be attached before the worker starts or
    // while its encoding thread is already running.
    std::atomic<AudioQueue*> audioQueue_{nullptr};

    // Thread-safe stats
    mutable std::mutex statsMutex_;
    RecordingStats stats_;

    // Set in stop() from ffmpeg_, before cleanup() resets it. See writeFailed().
    bool writeFailed_{false};
};

} // namespace vc
