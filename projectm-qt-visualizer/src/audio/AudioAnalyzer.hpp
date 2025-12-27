#pragma once
// AudioAnalyzer.hpp - FFT analysis for visualizer data
// Math that makes pretty colors go brrr

#include "util/Types.hpp"
#include <array>
#include <complex>
#include <vector>

namespace vc {

// FFT size - must be power of 2
constexpr usize FFT_SIZE = 2048;
constexpr usize SPECTRUM_SIZE = FFT_SIZE / 2;

// Frequency band data for visualizer
struct AudioSpectrum {
    std::array<f32, SPECTRUM_SIZE> magnitudes{};
    f32 leftLevel{0.0f};
    f32 rightLevel{0.0f};
    f32 beatIntensity{0.0f};
    bool beatDetected{false};
};

class AudioAnalyzer {
public:
    AudioAnalyzer();
    
    // Process audio samples and return spectrum
    AudioSpectrum analyze(std::span<const f32> samples, u32 sampleRate, u32 channels);
    
    // Get raw PCM data for ProjectM (interleaved stereo)
    const std::vector<f32>& pcmData() const { return pcmBuffer_; }
    
    // Reset state
    void reset();
    
private:
    void performFFT(std::span<const f32> input);
    void applyWindow(std::span<f32> samples);
    f32 detectBeat(f32 currentEnergy);
    
    // FFT buffers
    std::vector<std::complex<f32>> fftBuffer_;
    std::vector<f32> windowFunction_;
    std::vector<f32> magnitudes_;
    
    // PCM buffer for ProjectM
    std::vector<f32> pcmBuffer_;
    
    // Beat detection state
    f32 avgEnergy_{0.0f};
    f32 beatThreshold_{1.5f};
    std::vector<f32> energyHistory_;
    usize energyHistoryPos_{0};
    
    // Smoothing
    std::array<f32, SPECTRUM_SIZE> smoothedMagnitudes_{};
    f32 smoothingFactor_{0.3f};
};

} // namespace vc
