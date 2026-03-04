/**
 * @file URLAudioSource.hpp
 * @brief URL-based audio source with buffering for streaming
 */

#pragma once

#include "JuceTypes.hpp"
#include "util/Signal.hpp"

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace vc::audio::juce {

/**
 * @brief Audio source that downloads and buffers URL content
 * 
 * Features:
 * - Asynchronous download with progress reporting
 * - Memory buffering for seamless playback
 * - Support for any format JUCE can read (MP3, WAV, FLAC, OGG, etc.)
 * - Seekable after download completes
 */
class URLAudioSource : public juce::AudioSource {
public:
    explicit URLAudioSource(const std::string& url);
    ~URLAudioSource() override;

    // AudioSource interface
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // Position control
    void setNextReadPosition(int64_t newPosition);
    int64_t getNextReadPosition() const;
    int64_t getTotalLength() const;
    bool isLooping() const;
    void setLooping(bool shouldLoop);

    // Status
    float getBufferingProgress() const { return bufferingProgress_.load(); }
    bool isReady() const { return ready_.load(); }
    bool hasError() const { return hasError_.load(); }
    std::string getErrorMessage() const { return errorMessage_; }
    AudioSourceInfo getSourceInfo() const { return sourceInfo_; }
    bool isLoading() const { return loading_.load(); }

    // Signals
    vc::Signal<float> bufferingProgress;
    vc::Signal<> ready;
    vc::Signal<std::string> errorOccurred;

private:
    void startDownload();
    void stopDownload();
    void downloadThread();
    
    std::string url_;
    
    // Audio data
    std::unique_ptr<juce::AudioFormatReader> reader_;
    std::unique_ptr<juce::AudioBuffer<float>> audioBuffer_;
    juce::AudioFormatManager formatManager_;
    
    // Playback state
    std::atomic<int64_t> readPosition_{0};
    std::atomic<bool> looping_{false};
    std::atomic<float> bufferingProgress_{0.0f};
    std::atomic<bool> ready_{false};
    std::atomic<bool> hasError_{false};
    std::atomic<bool> loading_{false};
    
    double sampleRate_{44100.0};
    int blockSize_{512};
    
    AudioSourceInfo sourceInfo_;
    std::string errorMessage_;
    
    // Threading
    mutable std::mutex bufferLock_;
    std::unique_ptr<std::thread> downloadThread_;
    std::atomic<bool> shouldStop_{false};
};

}
