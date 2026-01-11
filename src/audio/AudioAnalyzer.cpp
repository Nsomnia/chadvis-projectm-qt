#include "audio/AudioAnalyzer.hpp"
#include "kiss_fft.h"
#include "kiss_fftr.h"
#include "core/Logger.hpp"

#include <array>
#include <complex>

namespace vc {

AudioAnalyzer::AudioAnalyzer() {
    energyHistory_.resize(60, 0.0f); // ~1 second at 60fps
    pcmBuffer_.reserve(FFT_SIZE);
}

void AudioAnalyzer::reset() {
    pcmBuffer_.clear();
    std::fill(energyHistory_.begin(), energyHistory_.end(), 0.0f);
    energyHistoryPos_ = 0;
    avgEnergy_ = 0.0f;
    std::fill(smoothedMagnitudes_.begin(), smoothedMagnitudes_.end(), 0.0f);
}

AudioSpectrum AudioAnalyzer::analyze(std::span<const vc::f32> samples,
                                     u32 sampleRate,
                                     u32 channels) {
    AudioSpectrum spectrum;

    if (samples.empty())
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

    // 2. Prepare mono data for FFT
    for (usize i = 0; i < totalFrames; ++i) {
        f32 mono;
        if (channels >= 2) {
            mono = (samples[i * channels] + samples[i * channels + 1]) * 0.5f;
        } else {
            mono = samples[i];
        }

        pcmBuffer_.push_back(mono);
        if (pcmBuffer_.size() > FFT_SIZE) {
            pcmBuffer_.erase(pcmBuffer_.begin());
        }
    }

    if (pcmBuffer_.size() < FFT_SIZE)
        return spectrum;

    // 3. Perform FFT
    std::array<f32, SPECTRUM_SIZE> currentMagnitudes;
    performKissFFT(pcmBuffer_, currentMagnitudes);

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

void AudioAnalyzer::performKissFFT(std::span<const vc::f32> input,
                                   std::span<vc::f32> magnitudes) {
    // Initialize config once
    static kiss_fftr_cfg cfg =
            kiss_fftr_alloc(static_cast<int>(FFT_SIZE), 0, nullptr, nullptr);

    if (!cfg)
        return;

    std::array<kiss_fft_cpx, FFT_SIZE / 2 + 1> out;
    kiss_fftr(cfg, input.data(), out.data());

    for (usize i = 0; i < SPECTRUM_SIZE; ++i) {
        // Compute magnitude: sqrt(re^2 + im^2)
        float mag = std::sqrt(out[i].r * out[i].r + out[i].i * out[i].i);
        // Normalize by FFT size
        magnitudes[i] = mag / (FFT_SIZE / 2);
    }
}

vc::f32 AudioAnalyzer::detectBeat(vc::f32 currentEnergy) {
    // Basic beat detection: current energy vs average energy in history
    avgEnergy_ = std::accumulate(
                         energyHistory_.begin(), energyHistory_.end(), 0.0f) /
                 energyHistory_.size();

    energyHistory_[energyHistoryPos_] = currentEnergy;
    energyHistoryPos_ = (energyHistoryPos_ + 1) % energyHistory_.size();

    if (avgEnergy_ > 0.0001f) {
        f32 ratio = currentEnergy / avgEnergy_;
        if (ratio > beatThreshold_) {
            return ratio;
        }
    }
    return 0.0f;
}

} // namespace vc
