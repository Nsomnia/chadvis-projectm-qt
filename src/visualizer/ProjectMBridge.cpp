#include "ProjectMBridge.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <cmath>

namespace vc {

ProjectMBridge::ProjectMBridge(projectm_ptr projectM)
    : projectM_(projectM)
{
    initializePCMBuffer();
}

void ProjectMBridge::initializePCMBuffer() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    pcmLeft_.resize(pcmSize_, 0.0f);
    pcmRight_.resize(pcmSize_, 0.0f);
    spectrumData_.resize(pcmSize_, 0.0f);
}

void ProjectMBridge::setPCMSize(size_t size) {
    pcmSize_ = size;
    initializePCMBuffer();
}

void ProjectMBridge::updateFromAnalyser(audio::juce::AnalyserSource* analyser) {
    if (!enabled_ || !analyser) return;
    
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    analyser->copyPCMData(pcmLeft_.data(), pcmRight_.data(), 
                          static_cast<int>(pcmSize_));
    analyser->copyMagnitudeData(spectrumData_.data(), 
                                static_cast<int>(pcmSize_));
    
    copyToProjectM();
}

void ProjectMBridge::injectPCM(const float* left, const float* right, size_t numSamples) {
    if (!enabled_ || !left || !right) return;
    
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    size_t samplesToCopy = std::min(numSamples, pcmSize_);
    
    if (numSamples >= pcmSize_) {
        std::copy(left, left + samplesToCopy, pcmLeft_.begin());
        std::copy(right, right + samplesToCopy, pcmRight_.begin());
    } else {
        std::fill(pcmLeft_.begin(), pcmLeft_.end(), 0.0f);
        std::fill(pcmRight_.begin(), pcmRight_.end(), 0.0f);
        std::copy(left, left + samplesToCopy, pcmLeft_.begin());
        std::copy(right, right + samplesToCopy, pcmRight_.begin());
    }
    
    copyToProjectM();
}

void ProjectMBridge::injectSpectrum(const float* magnitude, size_t numBins) {
    if (!enabled_ || !magnitude) return;
    
    std::lock_guard<std::mutex> lock(dataMutex_);
    
    size_t binsToCopy = std::min(numBins, spectrumData_.size());
    std::copy(magnitude, magnitude + binsToCopy, spectrumData_.begin());
    
    if (binsToCopy < spectrumData_.size()) {
        std::fill(spectrumData_.begin() + binsToCopy, spectrumData_.end(), 0.0f);
    }
}

void ProjectMBridge::copyToProjectM() {
    if (!projectM_) return;
    
    float interleavedPCM[1024];
    size_t halfSize = std::min(pcmSize_, static_cast<size_t>(512));
    
    for (size_t i = 0; i < halfSize; ++i) {
        interleavedPCM[i * 2] = pcmLeft_[i];
        interleavedPCM[i * 2 + 1] = pcmRight_[i];
    }
    
    projectm_pcm_add_float(projectM_, interleavedPCM, static_cast<int>(halfSize), 
                           PROJECTM_STEREO);
}

}
