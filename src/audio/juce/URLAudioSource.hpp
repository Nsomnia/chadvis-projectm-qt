#pragma once

#include "JuceTypes.hpp"
#include "util/Signal.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace vc::audio::juce {

class URLAudioSource {
public:
    explicit URLAudioSource(const std::string& url);
    ~URLAudioSource();

    bool prepare(double targetSampleRate, int blockSize);
    
    void setNextReadPosition(int64_t newPosition);
    int64_t getNextReadPosition() const;
    int64_t getTotalLength() const;
    bool isLooping() const;
    void setLooping(bool shouldLoop);

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
    void releaseResources();

    float getBufferingProgress() const { return bufferingProgress_.load(); }
    bool isReady() const { return ready_.load(); }
    bool hasError() const { return hasError_.load(); }
    std::string getErrorMessage() const { return errorMessage_; }
    
    AudioSourceInfo getSourceInfo() const { return sourceInfo_; }

    vc::Signal<float> bufferingProgress;
    vc::Signal<> ready;
    vc::Signal<std::string> errorOccurred;

private:
    void startBuffering();
    void stopBuffering();
    void bufferingThread();
    bool downloadAndBuffer();
    
    std::string url_;
    
    std::atomic<int64_t> readPosition_{0};
    std::atomic<bool> looping_{false};
    std::atomic<float> bufferingProgress_{0.0f};
    std::atomic<bool> ready_{false};
    std::atomic<bool> hasError_{false};
    std::atomic<bool> shouldStop_{false};
    
    double sampleRate_{44100.0};
    int blockSize_{512};
    int64_t totalSamples_{0};
    
    AudioSourceInfo sourceInfo_;
    std::string errorMessage_;
    
    mutable std::mutex bufferLock_;
    std::unique_ptr<std::thread> bufferingThread_;
};

}
