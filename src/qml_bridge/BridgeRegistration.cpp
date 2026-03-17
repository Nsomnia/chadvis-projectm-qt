/**
 * @file BridgeRegistration.cpp
 * @brief Implementation of bridge registration
 */

#include "BridgeRegistration.hpp"
#include "AudioBridge.hpp"
#include "PlaylistBridge.hpp"
#include "VisualizerBridge.hpp"
#include "RecordingBridge.hpp"
#include <QQmlEngine>

namespace qml_bridge {

namespace {
    AudioBridge* s_audioBridge = nullptr;
    PlaylistBridge* s_playlistBridge = nullptr;
    VisualizerBridge* s_visualizerBridge = nullptr;
    RecordingBridge* s_recordingBridge = nullptr;
}

void registerBridges(QQmlEngine* engine,
                     vc::AudioEngine* audioEngine,
                     vc::VisualizerWindow* visualizer,
                     vc::VideoRecorder* recorder)
{
    s_audioBridge = AudioBridge::create(engine, nullptr);
    s_playlistBridge = PlaylistBridge::create(engine, nullptr);
    s_visualizerBridge = VisualizerBridge::create(engine, nullptr);
    s_recordingBridge = RecordingBridge::create(engine, nullptr);

    if (audioEngine) {
        AudioBridge::setAudioEngine(audioEngine);
        PlaylistBridge::setAudioEngine(audioEngine);
    }

    if (visualizer) {
        VisualizerBridge::setVisualizerWindow(visualizer);
    }

    if (recorder) {
        RecordingBridge::setVideoRecorder(recorder);
    }

    qmlRegisterSingletonInstance("ChadVis", 1, 0, "AudioBridge", s_audioBridge);
    qmlRegisterSingletonInstance("ChadVis", 1, 0, "PlaylistBridge", s_playlistBridge);
    qmlRegisterSingletonInstance("ChadVis", 1, 0, "VisualizerBridge", s_visualizerBridge);
    qmlRegisterSingletonInstance("ChadVis", 1, 0, "RecordingBridge", s_recordingBridge);
}

AudioBridge* getAudioBridge() { return s_audioBridge; }
PlaylistBridge* getPlaylistBridge() { return s_playlistBridge; }
VisualizerBridge* getVisualizerBridge() { return s_visualizerBridge; }
RecordingBridge* getRecordingBridge() { return s_recordingBridge; }

}
