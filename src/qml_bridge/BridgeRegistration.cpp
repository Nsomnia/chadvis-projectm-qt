/**
 * @file BridgeRegistration.cpp
 * @brief Implementation of bridge registration
 */

#include "BridgeRegistration.hpp"
#include "AudioBridge.hpp"
#include "PlaylistBridge.hpp"
#include "VisualizerBridge.hpp"
#include "VisualizerItem.hpp"
#include "RecordingBridge.hpp"
#include "PresetBridge.hpp"
#include "LyricsBridge.hpp"
#include "SunoBridge.hpp"
#include "ThemeBridge.hpp"
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
}

void registerBridges(QQmlApplicationEngine* engine,
    vc::AudioEngine* audioEngine,
    vc::VisualizerWindow* visualizer,
    vc::VideoRecorder* recorder,
    vc::PresetManager* presetManager,
    vc::LyricsSync* lyricsSync,
    vc::suno::SunoController* sunoController)
{
    s_audioBridge = AudioBridge::create(engine, nullptr);
    s_playlistBridge = PlaylistBridge::create(engine, nullptr);
    s_visualizerBridge = VisualizerBridge::create(engine, nullptr);
    s_recordingBridge = RecordingBridge::create(engine, nullptr);
    s_presetBridge = PresetBridge::create(engine, nullptr);
    s_lyricsBridge = LyricsBridge::create(engine, nullptr);
    s_sunoBridge = SunoBridge::create(engine, nullptr);
    s_themeBridge = ThemeBridge::create(engine, nullptr);

    if (audioEngine) {
        s_audioBridge->setAudioEngine(audioEngine);
        s_playlistBridge->setAudioEngine(audioEngine);
    }

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

	qmlRegisterSingletonInstance("ChadVis", 1, 0, "AudioBridge", s_audioBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "PlaylistBridge", s_playlistBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "VisualizerBridge", s_visualizerBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "RecordingBridge", s_recordingBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "PresetBridge", s_presetBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "LyricsBridge", s_lyricsBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "SunoBridge", s_sunoBridge);
	qmlRegisterSingletonInstance("ChadVis", 1, 0, "Theme", s_themeBridge);

	qmlRegisterType<VisualizerItem>("ChadVis", 1, 0, "VisualizerItem");

	if (audioEngine) {
		VisualizerItem::setGlobalAudioEngine(audioEngine);
	}
	if (presetManager) {
		VisualizerItem::setGlobalPresetManager(presetManager);
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

}
