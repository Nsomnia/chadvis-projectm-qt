#include "audio/AudioAnalyzer.hpp"
#include "pffft/pffft.h"
#include "core/Logger.hpp"

#include <array>
#include <complex>

namespace vc {

namespace {

class PffftSetupOwner {
public:
    explicit PffftSetupOwner(int size) : setup_(pffft_new_setup(size, PFFFT_REAL)) {
        if (!setup_) {
            LOG_ERROR("Failed to initialize PFFFT setup for FFT_SIZE={}", size);
        }
    }

    ~PffftSetupOwner() {
        if (setup_) {
            pffft_destroy_setup(setup_);
        }
    }

    PffftSetupOwner(const PffftSetupOwner&) = delete;
    PffftSetupOwner& operator=(const PffftSetupOwner&) = delete;

    PFFFT_Setup* get() const {
        return setup_;
    }

private:
    PFFFT_Setup* setup_ = nullptr;
};

PffftSetupOwner& pffftSetup() {
    static PffftSetupOwner setup(FFT_SIZE);
    return setup;
}

}

AudioAnalyzer::AudioAnalyzer() {
	energyHistory_.resize(60, 0.0f);
}

void AudioAnalyzer::reset() {
    std::scoped_lock lock(mutex_);
    pcmBuffer_.clear();
    std::fill(energyHistory_.begin(), energyHistory_.end(), 0.0f);
    energyHistoryPos_ = 0;
    avgEnergy_ = 0.0f;
    runningEnergySum_ = 0.0f;
    std::fill(smoothedMagnitudes_.begin(), smoothedMagnitudes_.end(), 0.0f);
}

AudioSpectrum AudioAnalyzer::analyze(std::span<const vc::f32> samples,
                                     u32 sampleRate,
                                     u32 channels) {
    std::scoped_lock lock(mutex_);
    AudioSpectrum spectrum;

    if (samples.empty() || channels == 0 || sampleRate == 0)
        return spectrum;

    // 1. Calculate RMS levels for left/right
    f32 leftSum = 0, rightSum = 0;
    usize totalFrames = samples.size() / channels;

    if (totalFrames == 0)
        return spectrum;

    for (usize i = 0; i < totalFrames; ++i) {
        if (channels >= 2) {
            leftSum += samples[i * channels] * samples[i * channels];
            rightSum += samples[i * channels + 1] * samples[i * channels + 1];
        } else {
            leftSum += samples[i] * samples[i];
            rightSum = leftSum;
        }
    }

    spectrum.leftLevel = std::sqrt(leftSum / totalFrames);
    spectrum.rightLevel = std::sqrt(rightSum / totalFrames);

    // 2. Prepare mono data for FFT (using circular buffer for O(1) push)
    for (usize i = 0; i < totalFrames; ++i) {
        f32 mono;
        if (channels >= 2) {
            mono = (samples[i * channels] + samples[i * channels + 1]) * 0.5f;
        } else {
            mono = samples[i];
        }

        pcmBuffer_.push_back(mono);  // O(1) - no erase needed!
    }

    if (pcmBuffer_.size() < FFT_SIZE)
        return spectrum;

    // 3. Perform FFT
    std::array<f32, SPECTRUM_SIZE> currentMagnitudes{};
	performFFT(pcmBuffer_, currentMagnitudes);

    // 4. Smoothing and normalization
    f32 currentEnergy = 0.0f;
    for (usize i = 0; i < SPECTRUM_SIZE; ++i) {
        // Simple smoothing
        smoothedMagnitudes_[i] =
                smoothedMagnitudes_[i] * smoothingFactor_ +
                currentMagnitudes[i] * (1.0f - smoothingFactor_);
        spectrum.magnitudes[i] = smoothedMagnitudes_[i];
        currentEnergy += currentMagnitudes[i];
    }

    // 5. Beat detection
    spectrum.beatIntensity = detectBeat(currentEnergy / SPECTRUM_SIZE);
    spectrum.beatDetected =
            spectrum.beatIntensity > 1.1f; // Adjust threshold as needed

    return spectrum;
}

void AudioAnalyzer::performFFT(const CircularBuffer<vc::f32, FFT_SIZE>& input,
		std::span<vc::f32> magnitudes) {
    std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
    const auto& setup = pffftSetup();
    auto* pffft = setup.get();
    if (!pffft)
        return;

    alignas(16) std::array<f32, FFT_SIZE> inputBuffer{};
    alignas(16) std::array<f32, FFT_SIZE> outputBuffer{};
    alignas(16) std::array<f32, FFT_SIZE> workBuffer{};

    for (usize i = 0; i < FFT_SIZE; ++i) {
        inputBuffer[i] = input[i];
    }

    pffft_transform_ordered(pffft, inputBuffer.data(), outputBuffer.data(),
                            workBuffer.data(), PFFFT_FORWARD);

    for (usize i = 0; i < SPECTRUM_SIZE; ++i) {
        f32 re = outputBuffer[i * 2];
        f32 im = outputBuffer[i * 2 + 1];
        f32 mag = std::sqrt(re * re + im * im);
        magnitudes[i] = mag / (FFT_SIZE / 2);
    }
}

vc::f32 AudioAnalyzer::detectBeat(vc::f32 currentEnergy) {
    // Optimized beat detection using running sum (O(1) instead of O(N))
    // Subtract old value, add new value
    runningEnergySum_ -= energyHistory_[energyHistoryPos_];
    runningEnergySum_ += currentEnergy;
    
    energyHistory_[energyHistoryPos_] = currentEnergy;
    energyHistoryPos_ = (energyHistoryPos_ + 1) % energyHistory_.size();
    
    avgEnergy_ = runningEnergySum_ / energyHistory_.size();

    if (avgEnergy_ > 0.0001f) {
        f32 ratio = currentEnergy / avgEnergy_;
        if (ratio > beatThreshold_) {
            return ratio;
        }
    }
    return 0.0f;
}

} // namespace vc
