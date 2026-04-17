#pragma once
// AudioAnalyzer.hpp - FFT analysis for visualizer data
// Math that makes pretty colors go brrr

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include "util/Types.hpp"

namespace vc {

constexpr usize FFT_SIZE = 2048;
constexpr usize SPECTRUM_SIZE = FFT_SIZE / 2;

// Simple circular buffer for O(1) push/pop
// Replaces std::vector to avoid O(N) erase() in hot path
template<typename T, usize Size>
class CircularBuffer {
public:
    void push_back(T value) {
        buffer_[head_] = value;
        head_ = (head_ + 1) % Size;
        if (count_ < Size) ++count_;
    }
    
    void clear() {
        head_ = 0;
        count_ = 0;
    }
    
    usize size() const { return count_; }
    bool empty() const { return count_ == 0; }
    
    T operator[](usize index) const {
        usize pos = (head_ + Size - count_ + index) % Size;
        return buffer_[pos];
    }
    
    // Get contiguous span for FFT (may need two spans if wrapped)
    std::pair<std::span<const T>, std::span<const T>> getSpans() const {
        usize start = (head_ + Size - count_) % Size;
        if (start + count_ <= Size) {
            // Contiguous
            return {std::span(&buffer_[start], count_), std::span<const T>()};
        } else {
            // Wrapped
            usize firstPart = Size - start;
            return {std::span(&buffer_[start], firstPart), 
                    std::span(buffer_.data(), count_ - firstPart)};
        }
    }
    
private:
    std::array<T, Size> buffer_{};
    usize head_ = 0;
    usize count_ = 0;
};

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

    AudioSpectrum analyze(std::span<const vc::f32> samples,
                          u32 sampleRate,
                          u32 channels);

    // Get PCM data for external use (returns copy for API compatibility)
    std::vector<vc::f32> pcmData() const {
        std::vector<vc::f32> result;
        result.reserve(pcmBuffer_.size());
        for (usize i = 0; i < pcmBuffer_.size(); ++i) {
            result.push_back(pcmBuffer_[i]);
        }
        return result;
    }

    void reset();

private:
	void performFFT(const CircularBuffer<vc::f32, FFT_SIZE>& input,
		std::span<vc::f32> magnitudes);
    vc::f32 detectBeat(vc::f32 currentEnergy);

    // Circular buffer for O(1) push/pop (fixes O(N) vector erase)
    CircularBuffer<vc::f32, FFT_SIZE> pcmBuffer_;

    vc::f32 avgEnergy_{0.0f};
    vc::f32 beatThreshold_{1.5f};
    std::vector<vc::f32> energyHistory_;
    usize energyHistoryPos_{0};

    std::array<vc::f32, SPECTRUM_SIZE> smoothedMagnitudes_{};
    vc::f32 smoothingFactor_{0.3f};
    
    // Running sum for O(1) beat detection (optimization)
    vc::f32 runningEnergySum_{0.0f};
};
} // namespace vc
