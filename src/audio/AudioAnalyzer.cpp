#include "AudioAnalyzer.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace vc {

constexpr usize FFT_SIZE = 2048;
constexpr usize SPECTRUM_SIZE = FFT_SIZE / 2;

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

    AudioSpectrum analyze(std::span<const vc::f32> samples,
                          u32 sampleRate,
                          u32 channels);

    const std::vector<vc::f32>& pcmData() const {
        return pcmBuffer_;
    }

    void reset();

private:
    vc::f32 detectBeat(vc::f32 currentEnergy);

    std::vector<vc::f32> pcmBuffer_;

    vc::f32 avgEnergy_{0.0f};
    vc::f32 beatThreshold_{1.5f};
    std::vector<vc::f32> energyHistory_;
    usize energyHistoryPos_{0};

    std::array<vc::f32, SPECTRUM_SIZE> smoothedMagnitudes_{};
    vc::f32 smoothingFactor_{0.3f};
};
} // namespace vc
