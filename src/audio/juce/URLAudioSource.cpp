/**
 * @file URLAudioSource.cpp
 * @brief URL audio source implementation with JUCE streaming
 */

#include "URLAudioSource.hpp"
#include "core/Logger.hpp"

#include <JuceHeader.h>

namespace vc::audio::juce {

URLAudioSource::URLAudioSource(const std::string& url) 
    : url_(url) 
{
    // Register all common audio formats
    formatManager_.registerBasicFormats();
    formatManager_.registerFormat(new juce::MP3AudioFormat(), true);
    formatManager_.registerFormat(new juce::OggVorbisAudioFormat(), false);
    formatManager_.registerFormat(new juce::FLACAudioFormat(), false);
    formatManager_.registerFormat(new juce::WavAudioFormat(), false);
}

URLAudioSource::~URLAudioSource() {
    stopDownload();
}

void URLAudioSource::prepareToPlay(int samplesPerBlockExpected, double targetSampleRate) {
    sampleRate_ = targetSampleRate;
    blockSize_ = samplesPerBlockExpected;
    
    if (!ready_.load() && !loading_.load()) {
        startDownload();
    }
}

void URLAudioSource::releaseResources() {
    stopDownload();
    
    std::lock_guard<std::mutex> lock(bufferLock_);
    audioBuffer_.reset();
    reader_.reset();
}

void URLAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    if (!ready_.load() || !audioBuffer_) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    std::lock_guard<std::mutex> lock(bufferLock_);
    
    const int numChannels = audioBuffer_->getNumChannels();
    const int64_t totalSamples = audioBuffer_->getNumSamples();
    int64_t readPos = readPosition_.load();
    
    for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel) {
        const int sourceChannel = channel % numChannels;
        float* destData = bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample);
        
        int samplesRemaining = bufferToFill.numSamples;
        int destOffset = 0;
        
        while (samplesRemaining > 0) {
            const int64_t samplesAvailable = totalSamples - readPos;
            const int samplesToCopy = static_cast<int>(std::min(
                static_cast<int64_t>(samplesRemaining), samplesAvailable));
            
            if (samplesToCopy > 0) {
                const float* sourceData = audioBuffer_->getReadPointer(sourceChannel, readPos);
                std::memcpy(destData + destOffset, sourceData, samplesToCopy * sizeof(float));
            }
            
            destOffset += samplesToCopy;
            samplesRemaining -= samplesToCopy;
            readPos += samplesToCopy;
            
            // Handle looping or silence at end
            if (readPos >= totalSamples) {
                if (looping_.load()) {
                    readPos = 0;
                } else {
                    // Fill remaining with silence
                    while (samplesRemaining > 0) {
                        destData[destOffset++] = 0.0f;
                        samplesRemaining--;
                    }
                }
            }
        }
    }
    
    readPosition_.store(readPos);
}

void URLAudioSource::setNextReadPosition(int64_t newPosition) {
    std::lock_guard<std::mutex> lock(bufferLock_);
    if (audioBuffer_) {
        newPosition = std::clamp(newPosition, int64_t(0), 
                                 static_cast<int64_t>(audioBuffer_->getNumSamples()));
    }
    readPosition_.store(newPosition);
}

int64_t URLAudioSource::getNextReadPosition() const {
    return readPosition_.load();
}

int64_t URLAudioSource::getTotalLength() const {
    return audioBuffer_ ? audioBuffer_->getNumSamples() : 0;
}

bool URLAudioSource::isLooping() const {
    return looping_.load();
}

void URLAudioSource::setLooping(bool shouldLoop) {
    looping_.store(shouldLoop);
}

void URLAudioSource::startDownload() {
    shouldStop_ = false;
    loading_.store(true);
    downloadThread_ = std::make_unique<std::thread>(&URLAudioSource::downloadThread, this);
}

void URLAudioSource::stopDownload() {
    shouldStop_ = true;
    if (downloadThread_ && downloadThread_->joinable()) {
        downloadThread_->join();
    }
    downloadThread_.reset();
}

void URLAudioSource::downloadThread() {
    LOG_INFO("Starting URL download: {}", url_);
    bufferingProgress_.store(0.0f);
    bufferingProgress.emit(0.0f);
    
    try {
        // Create JUCE URL
        juce::URL juceUrl(url_);
        
        // Create input stream with timeout
        std::unique_ptr<juce::InputStream> stream(juceUrl.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(30000)
                .withResponseHeaders(nullptr)
        ));
        
        if (!stream) {
            hasError_.store(true);
            errorMessage_ = "Failed to create input stream for URL";
            LOG_ERROR("{}", errorMessage_);
            errorOccurred.emit(errorMessage_);
            loading_.store(false);
            return;
        }
        
        bufferingProgress_.store(0.1f);
        bufferingProgress.emit(0.1f);
        
        // Try to detect format from URL or content
        juce::String extension = juce::File(url_.c_str()).getFileExtension().toLowerCase();
        if (extension.isEmpty()) {
            extension = ".mp3"; // Default to MP3
        }
        
        // Create reader from stream
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager_.createReaderFor(std::move(stream)));
        
        if (!reader) {
            hasError_.store(true);
            errorMessage_ = "Failed to decode audio from URL";
            LOG_ERROR("{}", errorMessage_);
            errorOccurred.emit(errorMessage_);
            loading_.store(false);
            return;
        }
        
        bufferingProgress_.store(0.3f);
        bufferingProgress.emit(0.3f);
        
        // Read entire audio into memory
        const int64_t totalSamples = reader->lengthInSamples;
        const int numChannels = reader->numChannels;
        const double fileSampleRate = reader->sampleRate;
        
        LOG_INFO("Audio info: {} channels, {} samples, {} Hz", 
                 numChannels, totalSamples, fileSampleRate);
        
        // Create buffer
        auto buffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, totalSamples);
        
        // Read in chunks for progress reporting
        constexpr int64_t chunkSize = 65536;
        int64_t samplesRead = 0;
        
        while (samplesRead < totalSamples && !shouldStop_.load()) {
            const int64_t samplesToRead = std::min(chunkSize, totalSamples - samplesRead);
            const int actualRead = reader->read(buffer.get(), 0, samplesToRead, samplesRead, true, true);
            
            if (actualRead == 0) {
                break;
            }
            
            samplesRead += actualRead;
            
            // Update progress (30% to 90%)
            const float progress = 0.3f + 0.6f * (static_cast<float>(samplesRead) / totalSamples);
            bufferingProgress_.store(progress);
            bufferingProgress.emit(progress);
        }
        
        if (shouldStop_.load()) {
            LOG_INFO("Download cancelled");
            loading_.store(false);
            return;
        }
        
        // Store buffer
        {
            std::lock_guard<std::mutex> lock(bufferLock_);
            audioBuffer_ = std::move(buffer);
        }
        
        // Update source info
        sourceInfo_.totalSamples = totalSamples;
        sourceInfo_.sampleRate = fileSampleRate;
        sourceInfo_.numChannels = numChannels;
        sourceInfo_.bitsPerSample = reader->bitsPerSample;
        sourceInfo_.durationSeconds = totalSamples / fileSampleRate;
        sourceInfo_.formatName = extension.toStdString();
        sourceInfo_.isValid = true;
        
        bufferingProgress_.store(1.0f);
        bufferingProgress.emit(1.0f);
        
        ready_.store(true);
        loading_.store(false);
        ready.emit();
        
        LOG_INFO("URL download complete: {} seconds", sourceInfo_.durationSeconds);
        
    } catch (const std::exception& e) {
        hasError_.store(true);
        errorMessage_ = std::string("Download error: ") + e.what();
        LOG_ERROR("{}", errorMessage_);
        errorOccurred.emit(errorMessage_);
        loading_.store(false);
    }
}

}
