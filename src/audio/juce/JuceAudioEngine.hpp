#pragma once

#include "JuceTypes.hpp"
#include "util/Signal.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace vc::audio::juce {

class URLAudioSource;
class AnalyserSource;
class EffectsChain;

class JuceAudioEngine {
public:
    JuceAudioEngine();
    ~JuceAudioEngine();

    bool init();
    void shutdown();

    void play();
    void pause();
    void stop();
    void togglePlayPause();

    void seek(double seconds);
    void seekRelative(double secondsOffset);
    void setPosition(double normalizedPosition);
    void setVolume(float volume);
    void setLooping(bool shouldLoop);

    bool loadFile(const std::string& filePath);
    bool loadURL(const std::string& url);
    bool loadFromMemory(const void* data, size_t size, const std::string& formatHint = {});
    void unloadCurrentSource();

    PlaybackState state() const { return state_.load(); }
    double position() const;
    double duration() const;
    float volume() const { return volume_.load(); }
    bool isPlaying() const { return state_.load() == PlaybackState::Playing; }
    bool isLooping() const { return looping_.load(); }
    AudioSourceInfo sourceInfo() const;

    void copyMagnitudeData(float* dest, int fftSize) const;
    void copyPhaseData(float* dest, int fftSize) const;
    void copyPCMData(float* left, float* right, int numSamples) const;
    AudioAnalysisData getAnalysisData() const;

    double getCurrentSampleRate() const { return currentSampleRate_; }
    int getCurrentBlockSize() const { return currentBlockSize_; }

    vc::Signal<PlaybackState> stateChanged;
    vc::Signal<double> positionChanged;
    vc::Signal<double> durationChanged;
    vc::Signal<const AudioAnalysisData&> analysisReady;
    vc::Signal<std::string> errorOccurred;
    vc::Signal<float> bufferingProgress;

    vc::Signal<> sourceLoaded;
    vc::Signal<> sourceUnloaded;

private:
    void changeState(PlaybackState newState);
    void updatePosition();
    bool setupAudioSource();
    void initialiseAudioDevice();

    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<PlaybackState> state_{PlaybackState::Stopped};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> looping_{false};

    double currentSampleRate_{44100.0};
    int currentBlockSize_{512};
    int currentNumChannels_{2};

    AudioSourceInfo currentSourceInfo_;
    mutable std::mutex analysisMutex_;
    AudioAnalysisData lastAnalysis_;
};

}
