/**
 * @file BridgeRegistration.hpp
 * @brief Central registration point for all QML bridge singletons
 *
 * Single-responsibility: Bridge initialization and registration
 * Called during Application startup to connect C++ backend to QML
 *
 * @version 1.0.0
 */

#pragma once

#include <QQmlEngine>

namespace vc {
class AudioEngine;
class VisualizerWindow;
class VideoRecorder;
class Playlist;
class PresetManager;
class LyricsSync;
namespace suno {
class SunoController;
}
}

namespace qml_bridge {
class AudioBridge;
class PlaylistBridge;
class VisualizerBridge;
class RecordingBridge;
class PresetBridge;
class LyricsBridge;
class SunoBridge;

void registerBridges(QQmlEngine* engine,
    vc::AudioEngine* audioEngine,
    vc::VisualizerWindow* visualizer,
    vc::VideoRecorder* recorder,
    vc::PresetManager* presetManager = nullptr,
    vc::LyricsSync* lyricsSync = nullptr,
    vc::suno::SunoController* sunoController = nullptr);

AudioBridge* getAudioBridge();
PlaylistBridge* getPlaylistBridge();
VisualizerBridge* getVisualizerBridge();
RecordingBridge* getRecordingBridge();
PresetBridge* getPresetBridge();
LyricsBridge* getLyricsBridge();
SunoBridge* getSunoBridge();

} // namespace qml_bridge
