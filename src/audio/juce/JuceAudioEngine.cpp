#include "JuceAudioEngine.hpp"
#include "core/Logger.hpp"

namespace vc::audio::juce {

JuceAudioEngine::JuceAudioEngine() = default;
JuceAudioEngine::~JuceAudioEngine() { shutdown(); }

bool JuceAudioEngine::init() {
    LOG_INFO("Initializing JUCE audio engine");
    impl_ = std::make_unique<Impl>();
    return true;
}

void JuceAudioEngine::shutdown() {
    impl_.reset();
    LOG_INFO("JUCE audio engine shut down");
}

void JuceAudioEngine::play() {
    if (state_.load() == PlaybackState::Paused || 
        state_.load() == PlaybackState::Stopped) {
        changeState(PlaybackState::Playing);
    }
}

void JuceAudioEngine::pause() {
    if (state_.load() == PlaybackState::Playing) {
        changeState(PlaybackState::Paused);
    }
}

void JuceAudioEngine::stop() {
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
}

void JuceAudioEngine::setLooping(bool loop) {
    looping_.store(loop);
}

bool JuceAudioEngine::loadFile(const std::string& path) {
    LOG_INFO("Loading file: {}", path);
    return false;
}

bool JuceAudioEngine::loadURL(const std::string& url) {
    LOG_INFO("Loading URL: {}", url);
    return false;
}

bool JuceAudioEngine::loadFromMemory(const void* data, size_t size, 
                                      const std::string& hint) {
    LOG_INFO("Loading from memory: {} bytes", size);
    return false;
}

void JuceAudioEngine::unloadCurrentSource() {
    changeState(PlaybackState::Stopped);
    sourceUnloaded.emit();
}

double JuceAudioEngine::position() const { return 0.0; }
double JuceAudioEngine::duration() const { return 0.0; }
AudioSourceInfo JuceAudioEngine::sourceInfo() const { return currentSourceInfo_; }

void JuceAudioEngine::copyMagnitudeData(float* dest, int size) const {}
void JuceAudioEngine::copyPhaseData(float* dest, int size) const {}
void JuceAudioEngine::copyPCMData(float* left, float* right, int n) const {}
AudioAnalysisData JuceAudioEngine::getAnalysisData() const { return lastAnalysis_; }

void JuceAudioEngine::changeState(PlaybackState newState) {
    auto old = state_.exchange(newState);
    if (old != newState) stateChanged.emit(newState);
}

void JuceAudioEngine::updatePosition() {
    positionChanged.emit(position());
}

bool JuceAudioEngine::setupAudioSource() { return false; }
void JuceAudioEngine::initialiseAudioDevice() {}

struct JuceAudioEngine::Impl {};

}
