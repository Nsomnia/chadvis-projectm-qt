#pragma once

#include "audio/juce/JuceTypes.hpp"
#include <projectM-4/projectM.h>

#include <mutex>
#include <vector>

namespace vc {

class ProjectMBridge {
public:
    explicit ProjectMBridge(projectm_ptr projectM);
    ~ProjectMBridge() = default;

    void updateFromAnalyser(audio::juce::AnalyserSource* analyser);
    void injectPCM(const float* left, const float* right, size_t numSamples);
    void injectSpectrum(const float* magnitude, size_t numBins);
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    void setPCMSize(size_t size);
    size_t getPCMSize() const { return pcmSize_; }

private:
    void initializePCMBuffer();
    void copyToProjectM();
    
    projectm_ptr projectM_;
    std::vector<float> pcmLeft_;
    std::vector<float> pcmRight_;
    std::vector<float> spectrumData_;
    
    size_t pcmSize_{512};
    bool enabled_{true};
    mutable std::mutex dataMutex_;
};

}
