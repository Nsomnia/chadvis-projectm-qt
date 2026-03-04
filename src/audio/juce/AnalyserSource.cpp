/**
 * @file AnalyserSource.cpp
 * @brief FFT audio analysis implementation
 */

#include "AnalyserSource.hpp"
#include "core/Logger.hpp"

#include <cmath>
#include <algorithm>

namespace vc::audio::juce {

AnalyserSource::AnalyserSource() {
    magnitudeData_.resize(kMaxFFTSize);
    phaseData_.resize(kMaxFFTSize);
    pcmLeft_.resize(kMaxFFTSize);
    pcmRight_.resize(kMaxFFTSize);
}

AnalyserSource::~AnalyserSource() = default;

void AnalyserSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    blockSize_ = samplesPerBlockExpected;
    sampleRate_ = sampleRate;

    // Resize buffers
    pcmLeft_.resize(samplesPerBlockExpected * 2);
    pcmRight_.resize(samplesPerBlockExpected * 2);

    LOG_DEBUG("Analyser prepared: {} samples/block @ {} Hz",
        samplesPerBlockExpected, sampleRate);
}

void AnalyserSource::processBlock(float* left, float* right, int numSamples) {
    if (numSamples <= 0) return;

    std::lock_guard<std::mutex> lock(analysisLock_);

    // Store PCM data (circular buffer style - keep most recent)
    int copyCount = std::min(numSamples, static_cast<int>(pcmLeft_.size()));
    std::copy(left, left + copyCount, pcmLeft_.begin());
    std::copy(right, right + copyCount, pcmRight_.begin());

    // Perform FFT analysis
    performFFT(left, numSamples);

    updateAnalysisData();
}

void AnalyserSource::releaseResources() {
    std::fill(magnitudeData_.begin(), magnitudeData_.end(), 0.0f);
    std::fill(phaseData_.begin(), phaseData_.end(), 0.0f);
}

void AnalyserSource::copyMagnitudeData(float* dest, int size) const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    int copyCount = std::min(size, static_cast<int>(magnitudeData_.size()));
    std::copy(magnitudeData_.begin(), magnitudeData_.begin() + copyCount, dest);
}

void AnalyserSource::copyPhaseData(float* dest, int size) const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    int copyCount = std::min(size, static_cast<int>(phaseData_.size()));
    std::copy(phaseData_.begin(), phaseData_.begin() + copyCount, dest);
}

void AnalyserSource::copyPCMData(float* left, float* right, int numSamples) const {
    std::lock_guard<std::mutex> lock(analysisLock_);
    int copyCount = std::min(numSamples, static_cast<int>(pcmLeft_.size()));
    std::copy(pcmLeft_.begin(), pcmLeft_.begin() + copyCount, left);
    std::copy(pcmRight_.begin(), pcmRight_.begin() + copyCount, right);
}

AudioAnalysisData AnalyserSource::getAnalysisData() const {
    std::lock_guard<std::mutex> lock(analysisLock_);

    AudioAnalysisData data;
    data.fftSize = fftSize_;
    data.sampleRate = sampleRate_;
    data.isValid = true;

    data.magnitude.assign(magnitudeData_.begin(), magnitudeData_.begin() + fftSize_);
    data.phase.assign(phaseData_.begin(), phaseData_.begin() + fftSize_);
    data.pcmLeft.assign(pcmLeft_.begin(), pcmLeft_.begin() + std::min(fftSize_, static_cast<int>(pcmLeft_.size())));
    data.pcmRight.assign(pcmRight_.begin(), pcmRight_.begin() + std::min(fftSize_, static_cast<int>(pcmRight_.size())));

    return data;
}

void AnalyserSource::setFFTSize(int fftSize) {
    fftSize_ = std::min(fftSize, kMaxFFTSize);

    // Calculate FFT order (log2 of size)
    fftOrder_ = static_cast<int>(std::log2(fftSize_));

    magnitudeData_.resize(fftSize_);
    phaseData_.resize(fftSize_);

    LOG_DEBUG("FFT size set to: {} (order {})", fftSize_, fftOrder_);
}

void AnalyserSource::performFFT(const float* channelData, int numSamples) {
    // Simple DFT for magnitude estimation (fast enough for visualization)
    // For production, would use FFTW or JUCE's FFT

    int n = std::min(numSamples, fftSize_);

    for (int k = 0; k < n / 2; ++k) {
        float real = 0.0f;
        float imag = 0.0f;

        for (int i = 0; i < n; ++i) {
            float angle = -2.0f * M_PI * k * i / n;
            real += channelData[i] * std::cos(angle);
            imag += channelData[i] * std::sin(angle);
        }

        magnitudeData_[k] = std::sqrt(real * real + imag * imag) / n;
        phaseData_[k] = std::atan2(imag, real);
    }
}

void AnalyserSource::updateAnalysisData() {
    // Data is already updated in performFFT
    analysisReady.emit(getAnalysisData());
}

} // namespace vc::audio::juce
