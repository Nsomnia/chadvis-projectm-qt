/**
 * @file BridgeRegistration.cpp
 * @brief Implementation of bridge registration
 */

#include "BridgeRegistration.hpp"
#include "AudioBridge.hpp"
#include "PlaylistBridge.hpp"
#include "VisualizerBridge.hpp"
#include "VisualizerItem.hpp"
#include "VisualizerQFBO.hpp"
#include "RecordingBridge.hpp"
#include "PresetBridge.hpp"
#include "LyricsBridge.hpp"
#include "SunoBridge.hpp"
#include "ThemeBridge.hpp"
#include "OverlayBridge.hpp"
#include <QQmlApplicationEngine>

namespace qml_bridge {

namespace {
AudioBridge* s_audioBridge = nullptr;
PlaylistBridge* s_playlistBridge = nullptr;
VisualizerBridge* s_visualizerBridge = nullptr;
RecordingBridge* s_recordingBridge = nullptr;
PresetBridge* s_presetBridge = nullptr;
LyricsBridge* s_lyricsBridge = nullptr;
SunoBridge* s_sunoBridge = nullptr;
ThemeBridge* s_themeBridge = nullptr;
OverlayBridge* s_overlayBridge = nullptr;
}

void registerBridges(QQmlApplicationEngine* engine,
    vc::AudioEngine* audioEngine,
    vc::VisualizerWindow* visualizer,
    vc::VideoRecorder* recorder,
    vc::PresetManager* presetManager,
    vc::LyricsSync* lyricsSync,
    vc::suno::SunoController* sunoController)
{
    s_visualizerBridge = VisualizerBridge::create(engine, nullptr);
    s_recordingBridge = RecordingBridge::create(engine, nullptr);
    s_presetBridge = PresetBridge::create(engine, nullptr);
    s_lyricsBridge = LyricsBridge::create(engine, nullptr);
    s_sunoBridge = SunoBridge::create(engine, nullptr);
    s_themeBridge = ThemeBridge::create(engine, nullptr);
    s_overlayBridge = OverlayBridge::create(engine, nullptr);

    if (visualizer) {
        s_visualizerBridge->setVisualizerWindow(visualizer);
    }

    if (recorder) {
        s_recordingBridge->setVideoRecorder(recorder);
    }

    if (presetManager) {
        s_presetBridge->setPresetManager(presetManager);
        s_presetBridge->connectSignals();
    }

    if (lyricsSync && audioEngine) {
        s_lyricsBridge->setLyricsSync(lyricsSync);
        s_lyricsBridge->setAudioEngine(audioEngine);
        s_lyricsBridge->connectSignals();
    }

    if (sunoController) {
        s_sunoBridge->setSunoController(sunoController);
        s_sunoBridge->connectSignals();
    }

    if (audioEngine) {
        VisualizerItem::setGlobalAudioEngine(audioEngine);
        VisualizerQFBO::setGlobalAudioEngine(audioEngine);
    }
    if (presetManager) {
        VisualizerItem::setGlobalPresetManager(presetManager);
        VisualizerQFBO::setGlobalPresetManager(presetManager);
    }
}

AudioBridge* getAudioBridge() { return s_audioBridge; }
PlaylistBridge* getPlaylistBridge() { return s_playlistBridge; }
VisualizerBridge* getVisualizerBridge() { return s_visualizerBridge; }
RecordingBridge* getRecordingBridge() { return s_recordingBridge; }
PresetBridge* getPresetBridge() { return s_presetBridge; }
LyricsBridge* getLyricsBridge() { return s_lyricsBridge; }
SunoBridge* getSunoBridge() { return s_sunoBridge; }
ThemeBridge* getThemeBridge() { return s_themeBridge; }
OverlayBridge* getOverlayBridge() { return s_overlayBridge; }

}
