/**
 * @file JuceAudioEngine.cpp
 * @brief JUCE-based audio engine implementation
 *
 * Full implementation using JUCE framework for professional audio
 * playback, analysis, and device management.
 */

#include "JuceAudioEngine.hpp"
#include "AnalyserSource.hpp"
#include "core/Logger.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace vc::audio::juce {

using namespace juce;

//==============================================================================
// JUCE Implementation Details
//==============================================================================

struct JuceAudioEngine::Impl : public AudioIODeviceCallback {
    Impl() = default;
    ~Impl() override { shutdown(); }

    bool initialise() {
        LOG_INFO("Initializing JUCE audio system");

        // Initialize format managers
        formatManager.registerBasicFormats();

        // Setup audio device
        deviceManager.initialiseWithDefaultDevices(0, 2);

        auto* device = deviceManager.getCurrentAudioDevice();
        if (device) {
            currentSampleRate_ = device->getCurrentSampleRate();
            currentBlockSize_ = device->getCurrentBufferSizeSamples();
            LOG_INFO("Audio device: {} @ {}Hz, {} samples/block",
                device->getName().toStdString(),
                currentSampleRate_,
                currentBlockSize_);
        } else {
            LOG_WARN("No audio device available, using defaults");
            currentSampleRate_ = 44100.0;
            currentBlockSize_ = 512;
        }

        // Create analyser
        analyser_ = std::make_unique<AnalyserSource>();
        analyser_->prepareToPlay(currentBlockSize_, currentSampleRate_);

        // Register callback
        deviceManager.addAudioCallback(this);

        initialised_ = true;
        return true;
    }

    void shutdown() {
        if (!initialised_) return;

        LOG_INFO("Shutting down JUCE audio system");

        deviceManager.removeAudioCallback(this);

        stopInternal();

        transportSource.reset();
        readerSource.reset();
        analyser_.reset();

        deviceManager.closeAudioDevice();
        initialised_ = false;
    }

    bool loadFile(const std::string& path) {
        LOG_INFO("Loading file: {}", path);

        stopInternal();

        File file(path);
        if (!file.existsAsFile()) {
            LOG_ERROR("File not found: {}", path);
            return false;
        }

        auto* reader = formatManager.createReaderFor(file);
        if (!reader) {
            LOG_ERROR("Cannot read file: {}", path);
            return false;
        }

        // Create new source
        readerSource = std::make_unique<AudioFormatReaderSource>(reader, true);
        transportSource = std::make_unique<AudioTransportSource>();
        transportSource->setSource(readerSource.get(), 0, nullptr,
                                    reader->sampleRate,
                                    reader->numChannels);

        // Update info
        sourceInfo_.totalSamples = reader->lengthInSamples;
        sourceInfo_.sampleRate = reader->sampleRate;
        sourceInfo_.numChannels = reader->numChannels;
        sourceInfo_.bitsPerSample = reader->bitsPerSample;
        sourceInfo_.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        sourceInfo_.formatName = reader->getFormatName().toStdString();
        sourceInfo_.isValid = true;

        currentSampleRate_ = reader->sampleRate;

        // Prepare analyser
        analyser_->prepareToPlay(currentBlockSize_, currentSampleRate_);

        LOG_INFO("Loaded: {} channels, {} Hz, {:.2f}s",
            sourceInfo_.numChannels,
            sourceInfo_.sampleRate,
            sourceInfo_.durationSeconds);

        return true;
    }

    bool loadURL(const std::string& urlStr) {
        LOG_INFO("Loading URL: {}", urlStr);

        URL url(urlStr);
        if (url.isWellFormed()) {
            // Try to create reader from URL
            auto* reader = formatManager.createReaderFor(url.createInputStream(false));
            if (reader) {
                readerSource = std::make_unique<AudioFormatReaderSource>(reader, true);
                transportSource = std::make_unique<AudioTransportSource>();
                transportSource->setSource(readerSource.get());

                sourceInfo_.sampleRate = reader->sampleRate;
                sourceInfo_.numChannels = reader->numChannels;
                sourceInfo_.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
                sourceInfo_.isValid = true;

                return true;
            }
        }

        LOG_ERROR("Failed to load URL: {}", urlStr);
        return false;
    }

    bool loadFromMemory(const void* data, size_t size, const std::string& formatHint) {
        LOG_INFO("Loading from memory: {} bytes", size);

        MemoryBlock block(data, size);
        auto stream = std::make_unique<MemoryInputStream>(block, false);

        auto* reader = formatManager.createReaderFor(std::move(stream));
        if (!reader) {
            LOG_ERROR("Cannot decode memory audio");
            return false;
        }

        readerSource = std::make_unique<AudioFormatReaderSource>(reader, true);
        transportSource = std::make_unique<AudioTransportSource>();
        transportSource->setSource(readerSource.get());

        sourceInfo_.sampleRate = reader->sampleRate;
        sourceInfo_.numChannels = reader->numChannels;
        sourceInfo_.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        sourceInfo_.isValid = true;

        return true;
    }

    void play() {
        if (transportSource && !transportSource->isPlaying()) {
            transportSource->start();
            LOG_DEBUG("Playback started");
        }
    }

    void pause() {
        if (transportSource && transportSource->isPlaying()) {
            transportSource->stop();
            LOG_DEBUG("Playback paused");
        }
    }

    void stop() {
        stopInternal();
    }

    void stopInternal() {
        if (transportSource) {
            transportSource->stop();
            transportSource->setPosition(0.0);
        }
    }

    void seek(double seconds) {
        if (transportSource) {
            transportSource->setPosition(seconds);
        }
    }

    double position() const {
        return transportSource ? transportSource->getCurrentPosition() : 0.0;
    }

    double duration() const {
        return transportSource ? transportSource->getLengthInSeconds() : 0.0;
    }

    void setVolume(float vol) {
        if (transportSource) {
            transportSource->setGain(std::clamp(vol, 0.0f, 1.0f));
        }
    }

    float volume() const {
        return transportSource ? transportSource->getGain() : 1.0f;
    }

    void setLooping(bool loop) {
        if (readerSource) {
            readerSource->setLooping(loop);
            looping_ = loop;
        }
    }

    bool isPlaying() const {
        return transportSource && transportSource->isPlaying();
    }

    bool isLooping() const {
        return looping_;
    }

    //==========================================================================
    // AudioIODeviceCallback implementation
    //==========================================================================

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const AudioIODeviceCallbackContext& context) override {
        // Clear output
        for (int i = 0; i < numOutputChannels; ++i) {
            if (outputChannelData[i]) {
                FloatVectorOperations::clear(outputChannelData[i], numSamples);
            }
        }

        // Process audio
        if (transportSource && transportSource->isPlaying()) {
            AudioSourceChannelInfo info;
            info.buffer = &tempBuffer;
            info.startSample = 0;
            info.numSamples = numSamples;

            // Ensure buffer size
            tempBuffer.setSize(numOutputChannels, numSamples, false, false, true);

            transportSource->getNextAudioBlock(info);

            // Copy to output
            for (int i = 0; i < numOutputChannels; ++i) {
                if (outputChannelData[i] && tempBuffer.getReadPointer(i)) {
                    FloatVectorOperations::copy(outputChannelData[i],
                                                  tempBuffer.getReadPointer(i),
                                                  numSamples);
                }
            }

            // Feed analyser
            if (analyser_ && numOutputChannels >= 2) {
                analyser_->processBlock(
                    const_cast<float*>(tempBuffer.getReadPointer(0)),
                    const_cast<float*>(tempBuffer.getReadPointer(1)),
                    numSamples
                );
            }
        }
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override {
        currentSampleRate_ = device->getCurrentSampleRate();
        currentBlockSize_ = device->getCurrentBufferSizeSamples();

        if (analyser_) {
            analyser_->prepareToPlay(currentBlockSize_, currentSampleRate_);
        }

        tempBuffer.setSize(2, currentBlockSize_);

        LOG_DEBUG("Audio device starting: {} @ {}Hz",
            device->getName().toStdString(),
            currentSampleRate_);
    }

    void audioDeviceStopped() override {
        LOG_DEBUG("Audio device stopped");
    }

    void audioDeviceError(const String& errorMessage) override {
        LOG_ERROR("Audio device error: {}", errorMessage.toStdString());
    }

    //==========================================================================
    // Members
    //==========================================================================

    AudioDeviceManager deviceManager;
    AudioFormatManager formatManager;
    std::unique_ptr<AudioFormatReaderSource> readerSource;
    std::unique_ptr<AudioTransportSource> transportSource;
    std::unique_ptr<AnalyserSource> analyser_;

    AudioSourceInfo sourceInfo_;
    double currentSampleRate_{44100.0};
    int currentBlockSize_{512};
    bool initialised_{false};
    bool looping_{false};

    AudioBuffer<float> tempBuffer{2, 512};
};

//==============================================================================
// JuceAudioEngine Implementation
//==============================================================================

JuceAudioEngine::JuceAudioEngine() = default;
JuceAudioEngine::~JuceAudioEngine() { shutdown(); }

bool JuceAudioEngine::init() {
    LOG_INFO("Initializing JUCE audio engine");
    impl_ = std::make_unique<Impl>();
    return impl_->initialise();
}

void JuceAudioEngine::shutdown() {
    if (impl_) {
        impl_->shutdown();
        impl_.reset();
    }
    LOG_INFO("JUCE audio engine shut down");
}

void JuceAudioEngine::play() {
    if (state_.load() == PlaybackState::Paused ||
        state_.load() == PlaybackState::Stopped) {
        if (impl_) impl_->play();
        changeState(PlaybackState::Playing);
    }
}

void JuceAudioEngine::pause() {
    if (state_.load() == PlaybackState::Playing) {
        if (impl_) impl_->pause();
        changeState(PlaybackState::Paused);
    }
}

void JuceAudioEngine::stop() {
    if (impl_) impl_->stop();
    changeState(PlaybackState::Stopped);
}

void JuceAudioEngine::togglePlayPause() {
    switch (state_.load()) {
        case PlaybackState::Playing: pause(); break;
        case PlaybackState::Paused:
        case PlaybackState::Stopped: play(); break;
        default: break;
    }
}

void JuceAudioEngine::seek(double seconds) {
    if (impl_) impl_->seek(seconds);
}

void JuceAudioEngine::seekRelative(double offset) {
    seek(position() + offset);
}

void JuceAudioEngine::setPosition(double normalized) {
    auto dur = duration();
    if (dur > 0.0) seek(normalized * dur);
}

void JuceAudioEngine::setVolume(float vol) {
    volume_.store(std::clamp(vol, 0.0f, 1.0f));
    if (impl_) impl_->setVolume(vol);
}

void JuceAudioEngine::setLooping(bool loop) {
    looping_.store(loop);
    if (impl_) impl_->setLooping(loop);
}

bool JuceAudioEngine::loadFile(const std::string& path) {
    if (!impl_) return false;

    bool result = impl_->loadFile(path);
    if (result) {
        currentSourceInfo_ = impl_->sourceInfo_;
        sourceLoaded.emit();
        updatePosition();
    }
    return result;
}

bool JuceAudioEngine::loadURL(const std::string& url) {
    if (!impl_) return false;

    bool result = impl_->loadURL(url);
    if (result) {
        currentSourceInfo_ = impl_->sourceInfo_;
        sourceLoaded.emit();
    }
    return result;
}

bool JuceAudioEngine::loadFromMemory(const void* data, size_t size,
                                      const std::string& hint) {
    if (!impl_) return false;

    bool result = impl_->loadFromMemory(data, size, hint);
    if (result) {
        currentSourceInfo_ = impl_->sourceInfo_;
        sourceLoaded.emit();
    }
    return result;
}

void JuceAudioEngine::unloadCurrentSource() {
    if (impl_) impl_->stop();
    changeState(PlaybackState::Stopped);
    currentSourceInfo_ = {};
    sourceUnloaded.emit();
}

double JuceAudioEngine::position() const {
    return impl_ ? impl_->position() : 0.0;
}

double JuceAudioEngine::duration() const {
    return impl_ ? impl_->duration() : 0.0;
}

AudioSourceInfo JuceAudioEngine::sourceInfo() const {
    return currentSourceInfo_;
}

void JuceAudioEngine::copyMagnitudeData(float* dest, int size) const {
    if (impl_ && impl_->analyser_) {
        impl_->analyser_->copyMagnitudeData(dest, size);
    }
}

void JuceAudioEngine::copyPhaseData(float* dest, int size) const {
    if (impl_ && impl_->analyser_) {
        impl_->analyser_->copyPhaseData(dest, size);
    }
}

void JuceAudioEngine::copyPCMData(float* left, float* right, int n) const {
    if (impl_ && impl_->analyser_) {
        impl_->analyser_->copyPCMData(left, right, n);
    }
}

AudioAnalysisData JuceAudioEngine::getAnalysisData() const {
    if (impl_ && impl_->analyser_) {
        return impl_->analyser_->getAnalysisData();
    }
    return lastAnalysis_;
}

void JuceAudioEngine::changeState(PlaybackState newState) {
    auto old = state_.exchange(newState);
    if (old != newState) {
        stateChanged.emit(newState);
    }
}

void JuceAudioEngine::updatePosition() {
    positionChanged.emit(position());
}

bool JuceAudioEngine::setupAudioSource() {
    return impl_ != nullptr;
}

void JuceAudioEngine::initialiseAudioDevice() {
    // Handled in Impl::initialise
}

} // namespace vc::audio::juce
