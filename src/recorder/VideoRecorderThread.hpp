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

private:
    void threadLoop(std::stop_token stopToken);
    void updateStats(TimePoint startTime);

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
};

} // namespace vc
