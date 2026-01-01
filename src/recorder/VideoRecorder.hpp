#pragma once
// VideoRecorder.hpp - FFmpeg-based video recording

#include "EncoderSettings.hpp"
#include "FFmpegUtils.hpp"
#include "FrameGrabber.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace vc {

enum class RecordingState { Stopped, Starting, Recording, Stopping, Error };

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

    // Start recording with settings
    Result<void> start(const EncoderSettings& settings);
    Result<void> start(const fs::path& outputPath); // Uses default settings

    // Stop recording
    Result<void> stop();

    // Submit frames
    void submitVideoFrame(std::vector<u8>&& data,
                          u32 width,
                          u32 height,
                          i64 timestamp);
    void submitVideoFrame(const u8* data, u32 width, u32 height, i64 timestamp);
    void submitAudioSamples(const f32* data,
                            u32 samples,
                            u32 channels,
                            u32 sampleRate);

    // State
    RecordingState state() const {
        return state_;
    }
    bool isRecording() const {
        return state_ == RecordingState::Recording;
    }
    const RecordingStats& stats() const {
        return stats_;
    }

    // Settings
    const EncoderSettings& settings() const {
        return settings_;
    }

    // Signals
    Signal<RecordingState> stateChanged;
    Signal<const RecordingStats&> statsUpdated;
    Signal<std::string> error;

private:
    // Encoding thread
    void encodingThread();
    void processVideoFrame(const GrabbedFrame& frame);
    void processAudioBuffer();
    void flushEncoders();

    // FFmpeg setup
    Result<void> initFFmpeg();
    Result<void> initVideoStream();
    Result<void> initAudioStream();
    void cleanupFFmpeg();

    // Encoding
    bool encodeVideoFrame(AVFrame* frame);
    bool encodeAudioFrame(AVFrame* frame);
    bool writePacket(AVPacket* packet, AVStream* stream);

    // State
    std::atomic<RecordingState> state_{RecordingState::Stopped};
    EncoderSettings settings_;
    RecordingStats stats_;

    // Threading
    std::thread encodingThread_;
    std::atomic<bool> shouldStop_{false};
    FrameGrabber frameGrabber_;

    // Audio buffer
    std::vector<f32> audioBuffer_;
    std::mutex audioMutex_;
    u32 audioSampleRate_{48000};
    u32 audioChannels_{2};

    // FFmpeg contexts
    AVFormatContextPtr formatCtx_;
    AVCodecContextPtr videoCodecCtx_;
    AVCodecContextPtr audioCodecCtx_;
    AVStream* videoStream_{nullptr};
    AVStream* audioStream_{nullptr};
    SwsContextPtr swsCtx_;
    SwrContextPtr swrCtx_;

    AVFramePtr videoFrame_;
    AVFramePtr audioFrame_;
    AVPacketPtr packet_;

    i64 videoFrameCount_{0};
    i64 audioFrameCount_{0};
    i64 startTime_{0};

    std::mutex ffmpegMutex_;
};

} // namespace vc
