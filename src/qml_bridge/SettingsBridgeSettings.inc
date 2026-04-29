// X-macro table for SettingsBridge properties.
// Included twice in SettingsBridge.cpp:
//   1. With SETTING_* macros defined → generates getter/setter implementations
//   2. With all macros redefined to emit signals → generates resetToDefaults() body
//
// To add a new setting: add one line here + 3 lines in SettingsBridge.hpp
// (Q_PROPERTY, getter/setter declarations, signal declaration).

// ─── Audio ────────────────────────────────────────────────────
SETTING_INT(audioBufferSize, AudioBufferSize, audio().bufferSize, vc::u32)
SETTING_INT(audioSampleRate, AudioSampleRate, audio().sampleRate, vc::u32)

// ─── Visualizer ───────────────────────────────────────────────
SETTING_INT(visualizerFps, VisualizerFps, visualizer().fps, vc::u32)
SETTING_INT(visualizerMeshX, VisualizerMeshX, visualizer().meshX, vc::u32)
SETTING_INT(visualizerMeshY, VisualizerMeshY, visualizer().meshY, vc::u32)
SETTING_FLOAT(visualizerBeatSensitivity, VisualizerBeatSensitivity, visualizer().beatSensitivity, vc::f32)
SETTING_INT(visualizerPresetDuration, VisualizerPresetDuration, visualizer().presetDuration, vc::u32)
SETTING_INT(visualizerSmoothPresetDuration, VisualizerSmoothPresetDuration, visualizer().smoothPresetDuration, vc::u32)
SETTING_BOOL(visualizerShufflePresets, VisualizerShufflePresets, visualizer().shufflePresets)
SETTING_BOOL(visualizerAspectCorrection, VisualizerAspectCorrection, visualizer().aspectCorrection)

// ─── Recorder ─────────────────────────────────────────────────
SETTING_INT(recorderCrf, RecorderCrf, recording().video.crf, vc::u32)
SETTING_STRING(recorderPreset, RecorderPreset, recording().video.preset)
SETTING_INT(recorderWidth, RecorderWidth, recording().video.width, vc::u32)
SETTING_INT(recorderHeight, RecorderHeight, recording().video.height, vc::u32)
SETTING_INT(recorderFps, RecorderFps, recording().video.fps, vc::u32)
SETTING_STRING(recorderAudioCodec, RecorderAudioCodec, recording().audio.codec)
SETTING_INT(recorderAudioBitrate, RecorderAudioBitrate, recording().audio.bitrate, vc::u32)

// ─── Keyboard Shortcuts (read-only) ───────────────────────────
SETTING_RO_STRING(keyboardPlayPause, keyboard().playPause)
SETTING_RO_STRING(keyboardNextTrack, keyboard().nextTrack)
SETTING_RO_STRING(keyboardPrevTrack, keyboard().prevTrack)
SETTING_RO_STRING(keyboardToggleRecord, keyboard().toggleRecord)
SETTING_RO_STRING(keyboardToggleFullscreen, keyboard().toggleFullscreen)
SETTING_RO_STRING(keyboardNextPreset, keyboard().nextPreset)
SETTING_RO_STRING(keyboardPrevPreset, keyboard().prevPreset)

// ─── Karaoke ──────────────────────────────────────────────────
SETTING_STRING(karaokeFont, KaraokeFont, karaoke().fontFamily)
SETTING_FLOAT(karaokeYPosition, KaraokeYPosition, karaoke().yPosition, vc::f32)
SETTING_BOOL(karaokeEnabled, KaraokeEnabled, karaoke().enabled)

// ─── Suno ─────────────────────────────────────────────────────
SETTING_STRING(sunoToken, SunoToken, suno().token)
// NOTE: sunoDownloadPath is NOT in this table — it uses fs::path, not std::string

// ─── UI State ─────────────────────────────────────────────────
SETTING_STRING(expandedPanel, ExpandedPanel, ui().expandedPanel)
SETTING_INT(sidebarWidth, SidebarWidth, ui().sidebarWidth, vc::i32)
SETTING_BOOL(drawerOpen, DrawerOpen, ui().drawerOpen)
