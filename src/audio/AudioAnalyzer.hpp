#pragma once
// AudioAnalyzer.hpp - FFT analysis for visualizer data
// Math that makes pretty colors go brrr
// Uses KissFFT for SIMD-optimized FFT (replaces custom implementation)

#include <algorithm>
#include <cmath>
#include <numeric>
#include "util/Types.hpp"

// Forward declarations for KissFFT types
namespace kissfft {
struct kiss_fft_state;
struct kiss_fft_cpx;
} // namespace kissfft

namespace vc {
// FFT size - must be power of 2
constexpr usize FFT_SIZE = 2048;
constexpr usize SPECTRUM_SIZE = FFT_SIZE / 2;

// Frequency band data for visualizer
struct AudioSpectrum {
    std::array<vc::f32, SPECTRUM_SIZE> magnitudes{};
    vc::f32 leftLevel{0.0f};
    vc::f32 rightLevel{0.0f};
    vc::f32 beatIntensity{0.0f};
    bool beatDetected{false};
};

class AudioAnalyzer {
public:
    AudioAnalyzer();

    // Process audio samples and return spectrum
    AudioSpectrum analyze(std::span<const vc::f32> samples,
                          u32 sampleRate,
                          u32 channels);

    // Get raw PCM data for ProjectM (interleaved stereo)
    const std::vector<vc::f32>& pcmData() const {
        return pcmBuffer_;
    }

    // Reset state
    void reset();

private:
    void performKissFFT(std::span<const vc::f32> input,
                        std::span<vc::f32> magnitudes);
    void applyWindow(std::span<vc::f32> samples);
    vc::f32 detectBeat(vc::f32 currentEnergy);

    // KissFFT scratch arrays (pre-allocated to avoid per-FFT allocation)
    kiss_fft_cpx scratchCx_[FFT_SIZE];
    kiss_fft_cpx scratchCxOut_[FFT_SIZE];

    // PCM buffer for ProjectM
    std::vector<vc::f32> pcmBuffer_;

    // Beat detection state
    vc::f32 avgEnergy_{0.0f};
    vc::f32 beatThreshold_{1.5f};
    std::vector<vc::f32> energyHistory_;
    usize energyHistoryPos_{0};

    // Smoothing
    std::array<vc::f32, SPECTRUM_SIZE> smoothedMagnitudes_{};
    vc::f32 smoothingFactor_{0.3f};
};
} // namespace vc
