/**
 * @file VideoRecorderCore.hpp
 * @brief Main recording interface for the application.
 *
 * This file defines the VideoRecorder class which provides the public API
 * for starting, stopping, and submitting data to the video recording system.
 * It manages the high-level recording state and coordinates with the
 * VideoRecorderThread for asynchronous encoding.
 *
 * @section Patterns
 * - Facade: Provides a simple interface to the complex FFmpeg encoding system.
 * - Delegation: Offloads heavy encoding work to VideoRecorderThread.
 */

#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include "EncoderSettings.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

namespace vc {

// Forward declarations
class VideoRecorderThread;

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

    // Start/Stop
    Result<void> start(const EncoderSettings& settings);
    Result<void> start(const fs::path& outputPath);
    Result<void> stop();

    // Data submission (called from other threads)
    void submitVideoFrame(std::vector<u8>&& data,
                          u32 width,
                          u32 height,
                          i64 timestamp);
    void submitVideoFrame(const u8* data, u32 width, u32 height, i64 timestamp);
    void submitAudioSamples(const f32* data,
                            u32 samples,
                            u32 channels,
                            u32 sampleRate);

    // Getters
    RecordingState state() const {
        return state_;
    }
    bool isRecording() const {
        return state_ == RecordingState::Recording;
    }
    const RecordingStats& stats() const {
        return stats_;
    }
    const EncoderSettings& settings() const {
        return settings_;
    }

    // Signals
    Signal<RecordingState> stateChanged;
    Signal<const RecordingStats&> statsUpdated;
    Signal<std::string> error;

private:
    friend class VideoRecorderThread;

    std::atomic<RecordingState> state_{RecordingState::Stopped};
    EncoderSettings settings_;
    RecordingStats stats_;

    // The implementation logic runs here
    std::unique_ptr<VideoRecorderThread> worker_;
};

} // namespace vc
