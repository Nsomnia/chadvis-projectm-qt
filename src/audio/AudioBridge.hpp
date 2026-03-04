/**
 * @file AudioBridge.hpp
 * @brief Bridge between JUCE audio engine and ProjectM visualizer
 */

#pragma once

#include "audio/juce/JuceTypes.hpp"
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>

// Forward declarations
namespace vc::visualizer::projectm {
    class Engine;
}

namespace vc::audio {

/**
 * @brief Bridge that connects JUCE audio output to ProjectM visualization
 * 
 * Features:
 * - Thread-safe PCM data transfer
 * - Configurable buffer sizes
 * - FFT preprocessing option
 * - Multiple visualization modes
 */
class AudioBridge {
public:
    AudioBridge();
    ~AudioBridge();

    /**
     * @brief Initialize with sample rate and buffer size
     */
    bool initialize(int sampleRate, int bufferSize, int channels = 2);

    /**
     * @brief Set the ProjectM engine to feed
     */
    void setProjectMEngine(visualizer::projectm::Engine* engine);

    /**
     * @brief Process audio samples from JUCE
     * Call this from the audio thread
     */
    void processAudio(const float* const* channelData, int numSamples, int numChannels);

    /**
     * @brief Process interleaved audio
     */
    void processAudioInterleaved(const float* interleavedData, int numSamples, int numChannels);

    /**
     * @brief Get the latest PCM data for visualization
     * Thread-safe, can be called from any thread
     */
    void getPCMData(float* left, float* right, int size) const;

    /**
     * @brief Get the latest spectrum data
     */
    void getSpectrumData(float* magnitude, int size) const;

    /**
     * @brief Enable/disable the bridge
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_.load(); }

    /**
     * @brief Set visualization mode
     */
    enum class Mode {
        PCM,        // Raw waveform
        Spectrum,   // FFT spectrum
        Both        // Both PCM and spectrum
    };
    void setMode(Mode mode);
    Mode getMode() const { return mode_; }

    /**
     * @brief Set audio gain for visualization
     */
    void setGain(float gain);
    float getGain() const { return gain_.load(); }

private:
    void updateProjectM();
    void performFFT(const float* data, int size);

    visualizer::projectm::Engine* projectMEngine_{nullptr};

    int sampleRate_{44100};
    int bufferSize_{512};
    int numChannels_{2};

    // Circular buffers for PCM data
    mutable std::mutex pcmMutex_;
    std::vector<float> pcmLeft_;
    std::vector<float> pcmRight_;
    int pcmWritePos_{0};

    // Spectrum data
    mutable std::mutex spectrumMutex_;
    std::vector<float> spectrumData_;

    // Settings
    std::atomic<bool> enabled_{true};
    std::atomic<float> gain_{1.0f};
    Mode mode_{Mode::Both};

    bool initialized_{false};
};

} // namespace vc::audio
