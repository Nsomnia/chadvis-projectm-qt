#pragma once

#include "JuceTypes.hpp"
#include "util/Signal.hpp"

#include <atomic>
#include <memory>
#include <vector>

namespace vc::audio::juce {

class AnalyserSource {
public:
    AnalyserSource();
    ~AnalyserSource();

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
    void processBlock(float* left, float* right, int numSamples);
    void releaseResources();

    void copyMagnitudeData(float* dest, int fftSize) const;
    void copyPhaseData(float* dest, int fftSize) const;
    void copyPCMData(float* left, float* right, int numSamples) const;
    
    AudioAnalysisData getAnalysisData() const;
    
    void setFFTSize(int fftSize);
    int getFFTSize() const { return fftSize_; }
    
    double getSampleRate() const { return sampleRate_; }

    vc::Signal<const float*, int> spectrumReady;
    vc::Signal<const AudioAnalysisData&> analysisReady;

private:
    void performFFT(const float* channelData, int numSamples);
    void updateAnalysisData();
    
    int fftSize_{512};
    int fftOrder_{9};
    double sampleRate_{44100.0};
    int blockSize_{512};
    
    std::vector<float> magnitudeData_;
    std::vector<float> phaseData_;
    std::vector<float> pcmLeft_;
    std::vector<float> pcmRight_;
    
    mutable std::mutex analysisLock_;
    
    static constexpr int kMaxFFTSize = 8192;
};

}
