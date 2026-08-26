# Sources.cmake - Source and QML file lists, organized by module.
# Consumed by TargetSetup.cmake.

set(UTIL_SOURCES
    src/util/Types.hpp
    src/util/Result.hpp
    src/util/Signal.hpp
    src/util/FileUtils.hpp
    src/util/FileUtils.cpp
)

set(CORE_SOURCES
    src/core/Logger.hpp
    src/core/Logger.cpp
    src/core/ConfigData.hpp
    src/core/ConfigParsers.hpp
    src/core/ConfigParsers.cpp
    src/core/ConfigLoader.hpp
    src/core/ConfigLoader.cpp
    src/core/Config.hpp
    src/core/Config.cpp
    src/core/CliArg.hpp
    src/core/CliArgs.inc
    src/core/CliUtils.hpp
    src/core/CliUtils.cpp
    src/core/Application.hpp
    src/core/Application.cpp
)

set(AUDIO_SOURCES
    src/audio/AudioEngine.hpp
    src/audio/AudioEngine.cpp
    src/audio/AudioAnalyzer.hpp
    src/audio/AudioAnalyzer.cpp
    src/audio/AudioQueue.hpp
    src/audio/AudioChunk.hpp
    src/audio/Playlist.hpp
    src/audio/Playlist.cpp
    src/audio/analysis/MediaMetadata.hpp
    src/audio/analysis/MediaMetadata.cpp
)

set(VISUALIZER_SOURCES
    src/visualizer/projectm/Config.hpp
    src/visualizer/projectm/Engine.hpp
    src/visualizer/projectm/Engine.cpp
    src/visualizer/projectm/Playlist.hpp
    src/visualizer/projectm/Playlist.cpp
    src/visualizer/projectm/Bridge.hpp
    src/visualizer/projectm/Bridge.cpp
    src/visualizer/PresetData.hpp
    src/visualizer/PresetScanner.hpp
    src/visualizer/PresetScanner.cpp
    src/visualizer/PresetPersistence.hpp
    src/visualizer/PresetPersistence.cpp
    src/visualizer/PresetManager.hpp
    src/visualizer/PresetManager.cpp
    src/visualizer/RatingManager.hpp
    src/visualizer/RatingManager.cpp
    src/visualizer/RenderTarget.hpp
    src/visualizer/RenderTarget.cpp
    src/visualizer/VisualizerRenderer.hpp
    src/visualizer/VisualizerRenderer.cpp
    src/visualizer/VisualizerWindow.hpp
    src/visualizer/VisualizerWindow.cpp
)

set(SUNO_SOURCES
    src/suno/SunoModels.hpp
    src/suno/SunoClient.hpp
    src/suno/SunoClient.cpp
    src/suno/SunoOrchestrator.hpp
    src/suno/SunoOrchestrator.cpp
    src/suno/SunoDatabase.hpp
    src/suno/SunoDatabase.cpp
    src/suno/SunoLyrics.hpp
    src/suno/SunoLyrics.cpp
    src/suno/SunoAuthManager.hpp
    src/suno/SunoAuthManager.cpp
    src/suno/SunoLibraryManager.hpp
    src/suno/SunoLibraryManager.cpp
    src/suno/SunoDownloader.hpp
    src/suno/SunoDownloader.cpp
    src/suno/SunoLyricsManager.hpp
    src/suno/SunoLyricsManager.cpp
    src/suno/SunoAuthFailure.hpp
    src/suno/ClipResolver.hpp
    src/suno/ClipResolver.cpp
)

set(RECORDER_SOURCES
    src/recorder/EncoderSettings.hpp
    src/recorder/EncoderSettings.cpp
    src/recorder/FrameGrabber.hpp
    src/recorder/FrameGrabber.cpp
    src/recorder/VideoRecorderCore.hpp
    src/recorder/VideoRecorderCore.cpp
    src/recorder/VideoRecorderFFmpeg.hpp
    src/recorder/VideoRecorderFFmpeg.cpp
    src/recorder/VideoRecorderThread.hpp
    src/recorder/VideoRecorderThread.cpp
)

set(LYRICS_SOURCES
    src/lyrics/LyricsData.hpp
    src/lyrics/LyricsData.cpp
    src/lyrics/LyricsSync.hpp
    src/lyrics/LyricsSync.cpp
)

# ─────────────────────────────────────────────────────────────
# QML BRIDGE SOURCES - C++ types exposed to QML
# ─────────────────────────────────────────────────────────────
set(QML_BRIDGE_SOURCES
    src/qml_bridge/QmlSingletonBridge.hpp
    src/qml_bridge/AudioBridge.hpp
    src/qml_bridge/AudioBridge.cpp
    src/qml_bridge/PlaylistBridge.hpp
    src/qml_bridge/PlaylistBridge.cpp
    src/qml_bridge/PlaylistItemPresenter.hpp
    src/qml_bridge/PlaylistItemPresenter.cpp
    src/qml_bridge/VisualizerBridge.hpp
    src/qml_bridge/VisualizerBridge.cpp
    src/qml_bridge/RecordingBridge.hpp
    src/qml_bridge/RecordingBridge.cpp
    src/qml_bridge/PresetBridge.hpp
    src/qml_bridge/PresetBridge.cpp
    src/qml_bridge/LyricsBridge.hpp
    src/qml_bridge/LyricsBridge.cpp
    src/qml_bridge/SunoBridge.hpp
    src/qml_bridge/SunoBridge.cpp
    src/qml_bridge/ThemeBridge.hpp
    src/qml_bridge/ThemeBridge.cpp
    src/qml_bridge/OverlayBridge.hpp
    src/qml_bridge/OverlayBridge.cpp
    src/qml_bridge/SettingsBridge.hpp
    src/qml_bridge/SettingsBridge.cpp
    src/qml_bridge/BridgeRegistration.hpp
    src/qml_bridge/BridgeRegistration.cpp
)

# ─────────────────────────────────────────────────────────────
# UI SOURCES - Minimal auth/controller classes for QML
# ─────────────────────────────────────────────────────────────
set(UI_SOURCES
    src/ui/controllers/SunoController.hpp
    src/ui/controllers/SunoController.cpp
    src/ui/SystemBrowserAuth.hpp
    src/ui/SystemBrowserAuth.cpp
)

# ─────────────────────────────────────────────────────────────
# QML FILES - Declarative UI components
# ─────────────────────────────────────────────────────────────
set(QML_SOURCES
    src/qml/main.qml
    src/qml/styles/Theme.qml
    src/qml/components/AccordionPanel.qml
    src/qml/components/AccordionContainer.qml
    src/qml/components/AppButton.qml
    src/qml/components/AppSlider.qml
    src/qml/components/AppTextField.qml
    src/qml/components/AppComboBox.qml
    src/qml/components/AppSwitch.qml
    src/qml/components/SectionHeader.qml
    src/qml/components/PulseIndicator.qml
    src/qml/components/SettingSpinRow.qml
    src/qml/components/KaraokeMaster.qml
    src/qml/components/KaraokeSettings.qml
    src/qml/components/VisualizerOverlay.qml
    src/qml/panels/settings/PerformanceSettings.qml
    src/qml/panels/settings/AppearanceSettings.qml
    src/qml/panels/settings/AudioSettings.qml
    src/qml/panels/settings/VisualizerSettings.qml
    src/qml/panels/settings/RecordingSettings.qml
    src/qml/panels/settings/SunoSettings.qml
    src/qml/panels/settings/ShortcutsSettings.qml
    src/qml/panels/settings/ProfileSettings.qml
    src/qml/panels/PlaybackPanel.qml
    src/qml/panels/PlaylistPanel.qml
    src/qml/panels/PresetsPanel.qml
    src/qml/panels/LyricsPanel.qml
    src/qml/panels/SunoPanel.qml
    src/qml/panels/OverlayPanel.qml
    src/qml/panels/RecordingPanel.qml
    src/qml/panels/SettingsPanel.qml
)
