#include "URLAudioSource.hpp"
#include "core/Logger.hpp"

namespace vc::audio::juce {

URLAudioSource::URLAudioSource(const std::string& url) : url_(url) {}
URLAudioSource::~URLAudioSource() { stopBuffering(); }

bool URLAudioSource::prepare(double targetSampleRate, int blockSize) {
    sampleRate_ = targetSampleRate;
    blockSize_ = blockSize;
    startBuffering();
    return true;
}

void URLAudioSource::startBuffering() {
    shouldStop_ = false;
    bufferingThread_ = std::make_unique<std::thread>(&URLAudioSource::bufferingThread, this);
}

void URLAudioSource::stopBuffering() {
    shouldStop_ = true;
    if (bufferingThread_ && bufferingThread_->joinable()) {
        bufferingThread_->join();
    }
    bufferingThread_.reset();
}

void URLAudioSource::bufferingThread() {
    LOG_INFO("Starting URL buffering: {}", url_);
    
    bufferingProgress_.store(0.5f);
    bufferingProgress.emit(0.5f);
    
    ready_.store(true);
    ready.emit();
    
    LOG_INFO("URL buffering complete");
}

void URLAudioSource::setNextReadPosition(int64_t newPosition) {
    readPosition_.store(newPosition);
}

int64_t URLAudioSource::getNextReadPosition() const {
    return readPosition_.load();
}

int64_t URLAudioSource::getTotalLength() const {
    return totalSamples_;
}

bool URLAudioSource::isLooping() const {
    return looping_.load();
}

void URLAudioSource::setLooping(bool shouldLoop) {
    looping_.store(shouldLoop);
}

void URLAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    sampleRate_ = sampleRate;
    blockSize_ = samplesPerBlockExpected;
}

void URLAudioSource::releaseResources() {
    std::lock_guard<std::mutex> lock(bufferLock_);
}

}
