// X-macro table for SettingsBridge properties.
// Included twice in SettingsBridge.cpp:
//   1. With SETTING_* macros defined → generates getter/setter implementations
//   2. With all macros redefined to emit signals → generates resetToDefaults() body
//
// To add a new setting: add one line here + 3 lines in SettingsBridge.hpp
// (Q_PROPERTY, getter/setter declarations, signal declaration).
//
// NOTE: this file is included TWICE, so every macro used here must also be
// #undef'd and redefined to emit-only in SettingsBridge.cpp's resetToDefaults.
// Forgetting the second stub compiles as a function definition inside a
// function -- "function definition is not allowed here" -- one line per entry.

// ─── Audio ────────────────────────────────────────────────────
SETTING_INT(audioBufferSize, AudioBufferSize, audio().bufferSize, audioMut().bufferSize, vc::u32)
SETTING_INT(audioSampleRate, AudioSampleRate, audio().sampleRate, audioMut().sampleRate, vc::u32)

// ─── Visualizer ───────────────────────────────────────────────
SETTING_INT(visualizerFps, VisualizerFps, visualizer().fps, visualizerMut().fps, vc::u32)
SETTING_INT(visualizerMeshX, VisualizerMeshX, visualizer().meshX, visualizerMut().meshX, vc::u32)
SETTING_INT(visualizerMeshY, VisualizerMeshY, visualizer().meshY, visualizerMut().meshY, vc::u32)
SETTING_FLOAT(visualizerBeatSensitivity, VisualizerBeatSensitivity, visualizer().beatSensitivity, visualizerMut().beatSensitivity, vc::f32)
SETTING_INT(visualizerPresetDuration, VisualizerPresetDuration, visualizer().presetDuration, visualizerMut().presetDuration, vc::u32)
SETTING_INT(visualizerSmoothPresetDuration, VisualizerSmoothPresetDuration, visualizer().smoothPresetDuration, visualizerMut().smoothPresetDuration, vc::u32)
SETTING_BOOL(visualizerShufflePresets, VisualizerShufflePresets, visualizer().shufflePresets, visualizerMut().shufflePresets)
SETTING_BOOL(visualizerAspectCorrection, VisualizerAspectCorrection, visualizer().aspectCorrection, visualizerMut().aspectCorrection)

// ─── Recorder ─────────────────────────────────────────────────
SETTING_INT(recorderCrf, RecorderCrf, recording().video.crf, recordingMut().video.crf, vc::u32)
SETTING_STRING(recorderPreset, RecorderPreset, recording().video.preset, recordingMut().video.preset)
SETTING_INT(recorderWidth, RecorderWidth, recording().video.width, recordingMut().video.width, vc::u32)
SETTING_INT(recorderHeight, RecorderHeight, recording().video.height, recordingMut().video.height, vc::u32)
SETTING_INT(recorderFps, RecorderFps, recording().video.fps, recordingMut().video.fps, vc::u32)
SETTING_STRING(recorderAudioCodec, RecorderAudioCodec, recording().audio.codec, recordingMut().audio.codec)
SETTING_INT(recorderAudioBitrate, RecorderAudioBitrate, recording().audio.bitrate, recordingMut().audio.bitrate, vc::u32)

// ─── Keyboard Shortcuts (read-only) ───────────────────────────
SETTING_RO_STRING(keyboardPlayPause, keyboard().playPause)
SETTING_RO_STRING(keyboardNextTrack, keyboard().nextTrack)
SETTING_RO_STRING(keyboardPrevTrack, keyboard().prevTrack)
SETTING_RO_STRING(keyboardToggleRecord, keyboard().toggleRecord)
SETTING_RO_STRING(keyboardToggleFullscreen, keyboard().toggleFullscreen)
SETTING_RO_STRING(keyboardNextPreset, keyboard().nextPreset)
SETTING_RO_STRING(keyboardPrevPreset, keyboard().prevPreset)

// ─── Karaoke ──────────────────────────────────────────────────
SETTING_STRING(karaokeFont, KaraokeFont, karaoke().fontFamily, karaokeMut().fontFamily)
SETTING_FLOAT(karaokeYPosition, KaraokeYPosition, karaoke().yPosition, karaokeMut().yPosition, vc::f32)
SETTING_BOOL(karaokeEnabled, KaraokeEnabled, karaoke().enabled, karaokeMut().enabled)

// ─── Suno ─────────────────────────────────────────────────────
// NOTE: sunoToken is NOT in this table — it is a SECRET stored via
// CredentialStore (OS keychain), never written to config.toml. It is
// implemented manually next to sunoDownloadPath.
// NOTE: sunoDownloadPath is also not here — it uses fs::path, not std::string

// ─── Appearance (T0047) ───────────────────────────────────────
// The two colours the appearance panel edits. Both already parsed and
// persisted; before this they had no writer, so Theme.applyAccent() only
// touched the in-memory singleton and the colours reset on every launch.
SETTING_COLOR(accentColor, AccentColor, ui().accentColor, uiMut().accentColor)
SETTING_COLOR(backgroundColor, BackgroundColor, ui().backgroundColor, uiMut().backgroundColor)
// The active theme id (THEME-ENGINE-PLAN.md §9.3.1). Deliberately a raw
// string with no validation here: ConfigParsers validates on load, and Theme
// .qml's resolveThemeId() runs the fallback chain on the way to applying, so an
// unrecognised id is reported as what is actually on screen rather than being
// silently replaced at the point of entry. See T0190.
SETTING_STRING(themeName, ThemeName, ui().theme, uiMut().theme)

// ─── UI State ─────────────────────────────────────────────────
SETTING_STRING(expandedPanel, ExpandedPanel, ui().expandedPanel, uiMut().expandedPanel)
SETTING_INT(sidebarWidth, SidebarWidth, ui().sidebarWidth, uiMut().sidebarWidth, vc::i32)
SETTING_BOOL(drawerOpen, DrawerOpen, ui().drawerOpen, uiMut().drawerOpen)
