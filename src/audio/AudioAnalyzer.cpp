#include "AudioAnalyzer.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-existent-header"
#include <kissfft.h>
#pragma GCC diagnostic pop

namespace vc {

// Custom FFT implementation (Cooley-Tukey algorithm)
// Could be replaced with KissFFT for SIMD optimization
// KissFFT provides lightweight, portable, SIMD-optimized FFT
// See: https://github.com/mborgerding/kissfft
// Benefits: faster performance, reduced maintenance, well-tested standard
// library Current: 146 lines of custom implementation maintained for control

static void fft(std::vector<std::complex<f32>>& x) {
    const usize N = x.size();
    if (N <= 1)
        return;

    for (usize i = 1, j = 0; i < N; ++i) {
        usize bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }

    for (usize len = 2; len <= N; len <<= 1) {
        f32 angle = -2.0f * std::numbers::pi_v<f32> / static_cast<f32>(len);
        std::complex<f32> wlen(std::cos(angle), std::sin(angle));

        for (usize i = 0; i < N; i += len) {
            std::complex<f32> w(1.0f, 0.0f);

            for (usize j = 0; j < len / 2; ++j) {
                std::complex<f32> u = x[i + j];
                std::complex<f32> t = w * x[i + j + len / 2];

                x[i] = u + t;
                x[i + j + len / 2] = u - t;
                w *= wlen;
            }
        }
    }
}

AudioAnalyzer::AudioAnalyzer()
    : fftBuffer_(FFT_SIZE),
      windowFunction_(FFT_SIZE),
      magnitudes_(SPECTRUM_SIZE),
      pcmBuffer_(FFT_SIZE * 2),
      energyHistory_(43) {
    for (usize i = 0; i < FFT_SIZE; ++i) {
        windowFunction_[i] =
                0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<f32> * i /
                                        (FFT_SIZE - 1)));
    }
}

void AudioAnalyzer::reset() {
    std::fill(smoothedMagnitudes_.begin(), smoothedMagnitudes_.end(), 0.0f);
    std::fill(energyHistory_.begin(), energyHistory_.end(), 0.0f);
    avgEnergy_ = 0.0f;
    energyHistoryPos_ = 0;
}

AudioSpectrum AudioAnalyzer::analyze(std::span<const f32> samples,
                                     u32 sampleRate,
                                     u32 channels) {
    AudioSpectrum spectrum;

    if (samples.empty())
        return spectrum;

    f32 leftSum = 0.0f, rightSum = 0.0f;
    usize leftCount = 0, rightCount = 0;

    std::vector<f32> monoSamples(
            std::min(samples.size() / channels, static_cast<usize>(FFT_SIZE)));

    for (usize i = 0, j = 0; i < samples.size() && j < monoSamples.size();
         i += channels, ++j) {
        f32 left = samples[i];
        f32 right = channels > 1 ? samples[i + 1] : left;

        monoSamples[j] = (left + right) * 0.5f;

        leftSum += std::abs(left);
        rightSum += std::abs(right);
        ++leftCount;
        ++rightCount;
    }

    spectrum.leftLevel = leftCount > 0 ? leftSum / leftCount : 0.0f;
    spectrum.rightLevel = rightCount > 0 ? rightSum / rightCount : 0.0f;

    pcmBuffer_.resize(samples.size());
    std::copy(samples.begin(), samples.end(), pcmBuffer_.begin());

    performFFT(monoSamples);

    for (usize i = 0; i < SPECTRUM_SIZE; ++i) {
        smoothedMagnitudes_[i] =
                smoothedMagnitudes_[i] * (1.0f - smoothingFactor_) +
                magnitudes_[i] * smoothingFactor_;
        spectrum.magnitudes[i] = smoothedMagnitudes_[i];
    }

    f32 energy = std::accumulate(
            magnitudes_.begin(), magnitudes_.begin() + 64, 0.0f);
    spectrum.beatIntensity = energy;
    spectrum.beatDetected = detectBeat(energy);

    return spectrum;
}

void AudioAnalyzer::performFFT(std::span<const f32> input) {
    std::fill(fftBuffer_.begin(),
              fftBuffer_.end(),
              std::complex<f32>(0.0f, 0.0f));

    usize copyLen = std::min(input.size(), static_cast<usize>(FFT_SIZE));
    for (usize i = 0; i < copyLen; ++i) {
        fftBuffer_[i] = std::complex<f32>(input[i] * windowFunction_[i], 0.0f);
    }

    fft(fftBuffer_);

    for (usize i = 0; i < SPECTRUM_SIZE; ++i) {
        magnitudes_[i] = std::abs(fftBuffer_[i]) / static_cast<f32>(FFT_SIZE);
    }
}

f32 AudioAnalyzer::detectBeat(f32 currentEnergy) {
    energyHistory_[energyHistoryPos_] = currentEnergy;
    energyHistoryPos_ = (energyHistoryPos_ + 1) % energyHistory_.size();

    avgEnergy_ = std::accumulate(
                         energyHistory_.begin(), energyHistory_.end(), 0.0f) /
                 static_cast<f32>(energyHistory_.size());

    return currentEnergy > avgEnergy_ * beatThreshold_;
}

} // namespace vc
