#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace vc::audio::juce {

using Duration = std::chrono::milliseconds;
using SampleRate = double;
using SampleCount = int64_t;

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    Error
};

struct AudioAnalysisData {
    std::vector<float> magnitude;
    std::vector<float> phase;
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    int fftSize{512};
    double sampleRate{44100.0};
    bool isValid{false};
};

struct AudioSourceInfo {
    SampleCount totalSamples{0};
    SampleRate sampleRate{0.0};
    int numChannels{0};
    int bitsPerSample{0};
    std::string formatName;
    double durationSeconds{0.0};
    bool isValid{false};
};

}
