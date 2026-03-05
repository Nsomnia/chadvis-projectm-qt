/**
 * @file AudioBridge.cpp
 * @brief Implementation of JUCE → ProjectM audio bridge
 */

#include "AudioBridge.hpp"
#include "visualizer/projectm/Engine.hpp"
#include "core/Logger.hpp"

#include <cmath>
#include <algorithm>

namespace vc::audio {

AudioBridge::AudioBridge() = default;
AudioBridge::~AudioBridge() = default;

bool AudioBridge::initialize(int sampleRate, int bufferSize, int channels) {
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;
    numChannels_ = channels;

    // Allocate PCM buffers (typically 512 samples for ProjectM)
    constexpr int kProjectMBufferSize = 512;
    pcmLeft_.resize(kProjectMBufferSize, 0.0f);
    pcmRight_.resize(kProjectMBufferSize, 0.0f);
    spectrumData_.resize(kProjectMBufferSize, 0.0f);

    initialized_ = true;
    LOG_INFO("AudioBridge initialized: {} Hz, {} samples, {} channels",
             sampleRate, bufferSize, channels);
    
    return true;
}

void AudioBridge::setProjectMEngine(pm::Engine* engine) {
    projectMEngine_ = engine;
}

void AudioBridge::processAudio(const float* const* channelData, int numSamples, int numChannels) {
    if (!enabled_.load() || !initialized_) return;

    const float gain = gain_.load();

    std::lock_guard<std::mutex> lock(pcmMutex_);

    // ProjectM expects 512 samples typically
    const int targetSize = static_cast<int>(pcmLeft_.size());

    if (numSamples >= targetSize) {
        // Take the most recent samples
        const int offset = numSamples - targetSize;
        
        for (int i = 0; i < targetSize; ++i) {
            if (numChannels >= 2) {
                pcmLeft_[i] = channelData[0][offset + i] * gain;
                pcmRight_[i] = channelData[1][offset + i] * gain;
            } else if (numChannels == 1) {
                pcmLeft_[i] = channelData[0][offset + i] * gain;
                pcmRight_[i] = pcmLeft_[i];
            }
        }
    } else {
        // Shift existing data and append new
        const int shift = targetSize - numSamples;
        
        for (int i = 0; i < shift; ++i) {
            pcmLeft_[i] = pcmLeft_[i + numSamples];
            pcmRight_[i] = pcmRight_[i + numSamples];
        }
        
        for (int i = 0; i < numSamples; ++i) {
            if (numChannels >= 2) {
                pcmLeft_[shift + i] = channelData[0][i] * gain;
                pcmRight_[shift + i] = channelData[1][i] * gain;
            } else if (numChannels == 1) {
                pcmLeft_[shift + i] = channelData[0][i] * gain;
                pcmRight_[shift + i] = pcmLeft_[shift + i];
            }
        }
    }

    updateProjectM();
}

void AudioBridge::processAudioInterleaved(const float* interleavedData, int numSamples, int numChannels) {
    if (!enabled_.load() || !initialized_) return;

    const float gain = gain_.load();

    std::lock_guard<std::mutex> lock(pcmMutex_);

    const int targetSize = static_cast<int>(pcmLeft_.size());

    if (numSamples >= targetSize) {
        const int offset = numSamples - targetSize;
        
        for (int i = 0; i < targetSize; ++i) {
            const int idx = (offset + i) * numChannels;
            if (numChannels >= 2) {
                pcmLeft_[i] = interleavedData[idx] * gain;
                pcmRight_[i] = interleavedData[idx + 1] * gain;
            } else {
                pcmLeft_[i] = interleavedData[idx] * gain;
                pcmRight_[i] = pcmLeft_[i];
            }
        }
    } else {
        const int shift = targetSize - numSamples;
        
        for (int i = 0; i < shift; ++i) {
            pcmLeft_[i] = pcmLeft_[i + numSamples];
            pcmRight_[i] = pcmRight_[i + numSamples];
        }
        
        for (int i = 0; i < numSamples; ++i) {
            const int idx = i * numChannels;
            if (numChannels >= 2) {
                pcmLeft_[shift + i] = interleavedData[idx] * gain;
                pcmRight_[shift + i] = interleavedData[idx + 1] * gain;
            } else {
                pcmLeft_[shift + i] = interleavedData[idx] * gain;
                pcmRight_[shift + i] = pcmLeft_[shift + i];
            }
        }
    }

    updateProjectM();
}

void AudioBridge::getPCMData(float* left, float* right, int size) const {
    std::lock_guard<std::mutex> lock(pcmMutex_);
    
    const int copySize = std::min(size, static_cast<int>(pcmLeft_.size()));
    std::copy_n(pcmLeft_.data(), copySize, left);
    std::copy_n(pcmRight_.data(), copySize, right);
    
    // Fill remaining with zeros if needed
    if (size > copySize) {
        std::fill(left + copySize, left + size, 0.0f);
        std::fill(right + copySize, right + size, 0.0f);
    }
}

void AudioBridge::getSpectrumData(float* magnitude, int size) const {
    std::lock_guard<std::mutex> lock(spectrumMutex_);
    
    const int copySize = std::min(size, static_cast<int>(spectrumData_.size()));
    std::copy_n(spectrumData_.data(), copySize, magnitude);
    
    if (size > copySize) {
        std::fill(magnitude + copySize, magnitude + size, 0.0f);
    }
}

void AudioBridge::setEnabled(bool enabled) {
    enabled_.store(enabled);
}

void AudioBridge::setMode(Mode mode) {
    mode_ = mode;
}

void AudioBridge::setGain(float gain) {
    gain_.store(std::clamp(gain, 0.0f, 10.0f));
}

void AudioBridge::updateProjectM() {
    if (!projectMEngine_) return;

    // Feed PCM data to ProjectM
    // ProjectM expects interleaved or separate channel data
    // Combine left and right for interleaved
    std::vector<float> interleaved;
    interleaved.reserve(pcmLeft_.size() * 2);
    for (size_t i = 0; i < pcmLeft_.size(); ++i) {
        interleaved.push_back(pcmLeft_[i]);
        interleaved.push_back(pcmRight_[i]);
    }
    projectMEngine_->addPCMDataInterleaved(interleaved.data(), static_cast<u32>(pcmLeft_.size()), 2);
}

void AudioBridge::performFFT(const float* data, int size) {
    // FFT implementation would go here if we need spectrum analysis
    // For now, ProjectM handles its own FFT internally
}

} // namespace vc::audio
