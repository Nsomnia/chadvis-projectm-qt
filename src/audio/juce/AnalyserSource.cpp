#include "AnalyserSource.hpp"
#include "core/Logger.hpp"

#include <cmath>

namespace vc::audio::juce {

AnalyserSource::AnalyserSource() {
    setFFTSize(512);
}

AnalyserSource::~AnalyserSource() = default;

void AnalyserSource::setFFTSize(int fftSize) {
    int order = 0;
    int size = 1;
    while (size < fftSize && size < kMaxFFTSize) {
        size *= 2;
        order++;
    }
    
    fftSize_ = size;
    fftOrder_ = order;
    
    std::lock_guard<std::mutex> lock(analysisLock_);
    
    magnitudeData_.resize(fftSize_, 0.0f);
    phaseData_.resize(fftSize_, 0.0f);
    pcmLeft_.resize(kMaxFFTSize, 0.0f);
    pcmRight_.resize(kMaxFFTSize, 0.0f);
    
    LOG_DEBUG("FFT analyzer initialized: size={}, order={}", fftSize_, fftOrder_);
}

void AnalyserSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    blockSize_ = samplesPerBlockExpected;
    sampleRate_ = sampleRate;
    
    std::lock_guard<std::mutex> lock(analysisLock_);
    pcmLeft_.resize(std::max(blockSize_, fftSize_), 0.0f);
    pcmRight_.resize(std::max(blockSize_, fftSize_), 0.0f);
}

void AnalyserSource::processBlock(float* left, float* right, int numSamples) {
    std::lock_guard<std::mutex> lock(analysisLock_);
    
    int samplesToCopy = std::min(numSamples, static_cast<int>(pcmLeft_.size()));
    
    if (left) {
        std::copy(left, left + samplesToCopy, pcmLeft_.begin());
    }
    if (right) {
        std::copy(right, right + samplesToCopy, pcmRight_.begin());
    }
    
    performFFT(pcmLeft_.data(), std::min(numSamples, fftSize_));
    updateAnalysisData();
}

void AnalyserSource::performFFT(const float* channelData, int numSamples) {
    int halfSize = fftSize_ / 2;
    for (int i = 0; i < halfSize; ++i) {
        magnitudeData_[i] = std::abs(channelData[i % numSamples]);
        phaseData_[i] = 0.0f;
    }
    spectrumReady.emit(magnitudeData_.data(), halfSize);
}

void AnalyserSource::updateAnalysisData() {
    AudioAnalysisData data;
    data.magnitude = magnitudeData_;
    data.phase = phaseData_;
    data.pcmLeft = pcmLeft_;
    data.pcmRight = pcmRight_;
    data.fftSize = fftSize_;
    data.sampleRate = sampleRate_;
    data.isValid = true;
    analysisReady.emit(data);
}

void AnalyserSource::releaseResources() {
    std::lock_guard<std::mutex> lock(analysisLock_);
    std::fill(magnitudeData_.begin(), magnitudeData_.end(), 0.0f);
    std::fill(phaseData_.begin(), phaseData_.end(), 0.0f);
    std::fill(pcmLeft_.begin(), pcmLeft_.end(), 0.0f);
    std::fill(pcmRight_.begin(), pcmRight_.end(), 0.0f);
}

void AnalyserSource::copyMagnitudeData(float* dest, int fftSize) const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    int samplesToCopy = std::min(fftSize, fftSize_ / 2);
    std::copy(magnitudeData_.begin(), magnitudeData_.begin() + samplesToCopy, dest);
}

void AnalyserSource::copyPhaseData(float* dest, int fftSize) const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    int samplesToCopy = std::min(fftSize, fftSize_ / 2);
    std::copy(phaseData_.begin(), phaseData_.begin() + samplesToCopy, dest);
}

void AnalyserSource::copyPCMData(float* left, float* right, int numSamples) const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    int samplesToCopy = std::min(numSamples, static_cast<int>(pcmLeft_.size()));
    if (left) std::copy(pcmLeft_.begin(), pcmLeft_.begin() + samplesToCopy, left);
    if (right) std::copy(pcmRight_.begin(), pcmRight_.begin() + samplesToCopy, right);
}

AudioAnalysisData AnalyserSource::getAnalysisData() const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    AudioAnalysisData data;
    data.magnitude = magnitudeData_;
    data.phase = phaseData_;
    data.pcmLeft = pcmLeft_;
    data.pcmRight = pcmRight_;
    data.fftSize = fftSize_;
    data.sampleRate = sampleRate_;
    data.isValid = true;
    return data;
}

}
