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
    src/suno/ClipParser.hpp
    src/suno/ClipParser.cpp
    src/suno/DownloadQueue.hpp
    src/suno/DownloadQueue.cpp
    src/suno/SunoAccountManager.hpp
    src/suno/SunoAccountManager.cpp
    src/suno/SunoClient.hpp
    src/suno/SunoClient.cpp
    src/suno/SunoAudioUploadService.hpp
    src/suno/SunoAudioUploadService.cpp
    src/suno/SunoExploreService.hpp
    src/suno/SunoExploreService.cpp
    src/suno/SunoNotificationService.hpp
    src/suno/SunoNotificationService.cpp
    src/suno/SunoOrchestrator.hpp
    src/suno/SunoOrchestrator.cpp
    src/suno/SunoDatabase.hpp
    src/suno/SunoDatabase.cpp
    src/suno/SunoLyrics.hpp
    src/suno/SunoLyrics.cpp
    src/suno/SunoLibraryManager.hpp
    src/suno/SunoLibraryManager.cpp
    src/suno/SunoDownloader.hpp
    src/suno/SunoDownloader.cpp
    src/suno/SunoLyricsManager.hpp
    src/suno/SunoLyricsManager.cpp
    src/suno/SunoAuthFailure.hpp
    src/suno/ClipResolver.hpp
    src/suno/ClipResolver.cpp
    src/suno/SunoEndpoints.hpp
    src/suno/SunoWorkspace.hpp
    src/suno/SunoWorkspace.cpp
)

# Suno auth subsystem - Clerk auth module (Lane A of docs/PIVOT_PLAN.md).
set(SUNO_AUTH_SOURCES
    src/suno/auth/AuthTypes.hpp
    src/suno/auth/JwtUtils.hpp
    src/suno/auth/JwtUtils.cpp
    src/suno/auth/CredentialStore.hpp
    src/suno/auth/CredentialStore.cpp
    src/suno/auth/ClerkAuthClient.hpp
    src/suno/auth/ClerkAuthClient.cpp
    src/suno/auth/AuthHeaders.hpp
    src/suno/auth/AuthHeaders.cpp
    src/suno/auth/oauth/OAuthTransaction.hpp
    src/suno/auth/oauth/LoopbackListener.hpp
    src/suno/auth/oauth/LoopbackListener.cpp
    src/suno/auth/oauth/OAuthLoginService.hpp
    src/suno/auth/oauth/OAuthLoginService.cpp
    src/suno/auth/AuthCoordinator.hpp
    src/suno/auth/AuthCoordinator.cpp
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

# Post-pass (burn-in). Compiled UNCONDITIONALLY, and that is the point.
#
# SubtitleBurnIn.cpp carries its own #if: with CHADVIS_HAS_AVFILTER it holds the
# real filter-graph implementation, and without it the same translation unit holds
# the stub that makes burnInUnavailableReason() explain the absence. One file, two
# bodies, one set of symbols either way.
#
# Gating this list on CHADVIS_HAS_AVFILVER was a real build-breaking bug, found by
# the lane that wired the UI: gating removed the very file that contains the stub,
# so vc::burnIn* had NO definition on a no-libavfilter build and any reference to
# them was a LINK ERROR of the whole application -- on precisely the stripped
# distros CHADVIS_POSTPROCESS exists to keep working. The defect needed two gates
# to disagree: this one, and the macro definition in TargetSetup.cmake. Removing
# either alone would have left the same trap one refactor away, so this one is gone
# entirely and the macro is now the only gate.
#
# FFmpegUtils.hpp is NOT in this list on purpose. It is included by files that
# build with or without libavfilter, and it is the one header that would have to
# conditionally include libavfilter/avfilter.h -- which is why SubtitleBurnIn.hpp
# carries the #if instead and is the only header that mentions avfilter.
set(CHADVIS_POSTPROCESS_SOURCES
    src/recorder/SubtitleBurnIn.hpp
    src/recorder/SubtitleBurnIn.cpp
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
    src/qml_bridge/SunoWorkspaceBridge.hpp
    src/qml_bridge/SunoWorkspaceBridge.cpp
)

# ─────────────────────────────────────────────────────────────
# UI SOURCES - Minimal auth/controller classes for QML
# ─────────────────────────────────────────────────────────────
set(UI_SOURCES
    src/ui/controllers/SunoController.hpp
    src/ui/controllers/SunoController.cpp
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
    src/qml/components/NavRail.qml
    src/qml/components/AccountChip.qml
    src/qml/components/TransportBar.qml
    src/qml/components/ClipCard.qml
    src/qml/components/ClipDetailSheet.qml
    src/qml/components/ComingSoonPage.qml
    src/qml/views/LibraryView.qml
    src/qml/views/DiscoverView.qml
    src/qml/views/NotificationsView.qml
    src/qml/views/ListenView.qml
    src/qml/views/SettingsView.qml
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
    src/qml/SettingsWindow.qml
    src/qml/settings/AccountPage.qml
    src/qml/settings/AccountSessionCard.qml
    src/qml/settings/AudioPage.qml
    src/qml/settings/VisualizerPage.qml
    src/qml/settings/RecordingPage.qml
    src/qml/settings/KaraokePage.qml
    src/qml/settings/AppearancePage.qml
    src/qml/settings/PerformancePage.qml
    src/qml/settings/ShortcutsPage.qml
    src/qml/settings/ProfilesPage.qml
    src/qml/views/CreateView.qml
    src/qml/views/VideoView.qml
    src/qml/settings/SettingsWindowHeader.qml
    src/qml/settings/SettingsWindowFooter.qml
    src/qml/settings/SettingsPageRail.qml
)

# ─────────────────────────────────────────────────────────────
# PCM FORMAT LAYER (appended 2026-10-03)
#
# src/audio/PcmFormat.{hpp,cpp} is the pure, device-free PCM conversion that
# AudioEngine::processAudioBuffer used to inline. It was untestable in place: the
# old `if Float / else if Int16` had no else, so an undecodable sink format fell
# through and pushed the previous buffer's samples into both consumer queues.
# Hoisting it is what made the unsupported-format contract assertable without an
# audio device.
#
# Appended to AUDIO_SOURCES with list(APPEND) rather than merged into its literal
# above, so no existing line is touched. It has to be that variable: TargetSetup
# names ${AUDIO_SOURCES} explicitly in add_library(project_lib STATIC ...) and
# does not glob, so a fresh set() would be silently dropped -- PcmFormat.cpp would
# never be compiled and every vc::pcm reference would be a link error. Merge this
# into the AUDIO_SOURCES literal when the tree is next normalised; there is no
# ordering or dependency reason for it to live apart.
# ─────────────────────────────────────────────────────────────
list(APPEND AUDIO_SOURCES
    src/audio/PcmFormat.hpp
    src/audio/PcmFormat.cpp
)

# ─────────────────────────────────────────────────────────────
# OFFLINE RENDER INPUT — the file -> PCM decoder (appended 2026-10-03)
#
# src/recorder/AudioFileDecoder.{hpp,cpp} is the last missing class on the offline
# music-video path: VisualizerRenderer is already a plain class whose isExposed is
# a parameter, AudioQueue::pushAll is already a producer seam, and swresample is
# already linked — what did not exist was anything that decoded a file.
#
# Appended to RECORDER_SOURCES with list(APPEND) rather than merged into its
# literal above, so no existing line is touched. It has to be that variable:
# TargetSetup.cmake:80-93 names each set() variable explicitly in
# add_library(project_lib STATIC ...) and does NOT glob, so a fresh
# set(AUDIODECODER_SOURCES ...) would be silently dropped — AudioFileDecoder.cpp
# would never be compiled and every vc::AudioFileDecoder reference would be a link
# error of the whole application. This is the second time that trap has been
# available in this file; the PcmFormat block above records the first. Merge both
# into their literals when the tree is next normalised; there is no ordering or
# dependency reason for either to live apart.
#
# Not gated on anything, deliberately. It needs no libavfilter, no OpenGL and no
# network, so a CMake option around it would add a configuration in which the
# offline renderer's only input path silently does not exist.
# ─────────────────────────────────────────────────────────────
list(APPEND RECORDER_SOURCES
    src/recorder/AudioFileDecoder.hpp
    src/recorder/AudioFileDecoder.cpp
)

# ─────────────────────────────────────────────────────────────
# COLOR CODEC — src/util/Color.{hpp,cpp} (appended 2026-10-03)
#
# parseHexColor() replaced the allocating, throwing std::stoi inside
# Color::fromHex, which could throw std::invalid_argument straight out of
# ConfigLoader::load (whose catch covered only toml::parse_error) on a
# hand-edited `accent_color`. Color::fromHex/toHex MOVED here from FileUtils.cpp,
# so this entry is REQUIRED, not optional: omit it and every vc::Color::toHex()
# caller — ConfigParsers, ThemeBridge, ConfigLoader — is an undefined symbol.
#
# Must be UTIL_SOURCES specifically. TargetSetup.cmake names each set() variable
# explicitly in add_library(project_lib STATIC ...) and does NOT glob, so a fresh
# set(COLOR_SOURCES ...) would be silently dropped. That is the same trap the
# PcmFormat and AudioFileDecoder blocks above record; this is the third time it
# has been available in this file. Merge all three into their literals when the
# tree is next normalised — there is no ordering or dependency reason for any of
# them to live apart.
# ─────────────────────────────────────────────────────────────
list(APPEND UTIL_SOURCES
    src/util/Color.hpp
    src/util/Color.cpp
)

# ─────────────────────────────────────────────────────────────
# RENDER JOB MODEL (appended 2026-10-03)
#
# src/recorder/Render{Job,Queue}.{hpp,cpp} are the durable-state half of the
# batch music-video creator: without them "re-render this at 4K" means
# re-rendering three minutes in real time, because nothing records that the first
# output was 1800 frames at 60 fps from these presets. The queue takes the work
# as an injected JobRunner and knows nothing about GL, FFmpeg or audio — a GL
# context current on a background thread is a separate spike, and coupling it in
# here would make this untestable without a display.
#
# RECORDER_SOURCES for the same non-globbing reason as the three blocks above.
# ─────────────────────────────────────────────────────────────
list(APPEND RECORDER_SOURCES
    src/recorder/RenderJob.hpp
    src/recorder/RenderJob.cpp
    src/recorder/RenderQueue.hpp
    src/recorder/RenderQueue.cpp
)

# ─────────────────────────────────────────────────────────────
# HTTP POLICY — the one owner of the tree's request policy (appended 2026-10-04)
#
# src/suno/HttpPolicy.{hpp,cpp} exists because the policy had no owner.
# DownloadQueue was hardened with a transfer timeout, a byte cap and a
# hand-parsed Retry-After; the other five sites in the tree had none of it, so a
# hung endpoint held a client slot forever. Centralising is the fix — a comment
# at each call site would reproduce the same bug in six months.
#
# Two measured Qt 6.11.1 facts make it non-optional rather than tidy:
#   * an UNSET transfer timeout is 0, meaning NO timeout at all, not a
#     conservative default. DefaultTransferTimeoutConstant applies only when the
#     setter is called with no argument. So the unhardened sites had none.
#   * Qt's own setDecompressedSafetyCheckThreshold docs state it "does not impose
#     an absolute limit on the total decompressed output size" and advise the app
#     to monitor bytesAvailable() itself — which is what BodyReader::pump does.
#
# SUNO_SOURCES specifically, for the non-globbing reason as the three blocks
# above. Merge into its literal when the tree is next normalised.
# ─────────────────────────────────────────────────────────────
list(APPEND SUNO_SOURCES
    src/suno/HttpPolicy.hpp
    src/suno/HttpPolicy.cpp
)

# ─────────────────────────────────────────────────────────────
# CREDENTIAL STORE PLATFORM BACKENDS (appended 2026-10-04)
#
# Listed UNCONDITIONALLY and guarded INSIDE each file with
# CHADVIS_HAS_WIN32_CREDENTIALS / CHADVIS_HAS_SECRET_SERVICE, so Windows-only
# code never compiles on Linux and vice versa, and both define their factory in
# every configuration — returning nullptr when compiled out. That is what lets
# CredentialStore.cpp dispatch with no platform #ifdef of its own.
#
# Do NOT re-gate this list on CHADVIS_HAS_KEYCHAIN. That is precisely the trap
# the CHADVIS_POSTPROCESS list recorded earlier for SubtitleBurnIn.cpp: gating a
# list removes the very translation unit holding the no-backend stub, so
# vc::storeCredential loses its definition on exactly the platforms the gate was
# meant to protect. Both gates would have to disagree for it to happen, and that
# is two chances to be wrong rather than one.
# ─────────────────────────────────────────────────────────────
list(APPEND SUNO_AUTH_SOURCES
    src/suno/auth/CredentialStoreWin32.cpp
    src/suno/auth/CredentialStoreSecretService.cpp
)

# ─────────────────────────────────────────────────────────────
# RESAMPLER ENGINE SELECTION (appended 2026-10-04)
#
# Not in any RECORDER_SOURCES set() above — TargetSetup.cmake names each variable
# explicitly and does NOT glob, so a new recorder source is dropped silently and
# only surfaces as an undefined symbol at link time, naming the CALLER rather
# than the forgotten file. That trap has now been hit four times in this project
# (PcmFormat.cpp, Color.cpp, AudioFileDecoder.cpp, and this one).
#
# The FFmpeg-free header is the point: applyEngine() needs a SwrContext*, and
# choosingEngine() — the part with the interesting branches — is pure.
# ─────────────────────────────────────────────────────────────
list(APPEND RECORDER_SOURCES
    src/recorder/ResamplerEngine.hpp
    src/recorder/ResamplerEngine.cpp
)

# ─────────────────────────────────────────────────────────────
# OFFSCREEN RENDER SPIKE (appended 2026-10-04)
#
# src/recorder/OffscreenRenderSpike.{hpp,cpp} answers the question that gates the
# whole batch renderer: can a GL context render pixels on a background thread?
#
# MEASURED ANSWER ON macOS: NO, and not because of thread affinity. The context
# itself is fine off-thread (raw CGLSetCurrentContext succeeds and reports
# GL 4.1), but there is no off-main-thread DRAWABLE left: CGLCreatePBuffer is
# gone (deprecated 10.3, removed 10.7), QWindow::create() from a worker throws
# NSInternalInconsistencyException, Qt's own makeCurrent(QWindow*) guard fires,
# and raw NSOpenGLContext setView: SIGILLs inside AppKit. QOffscreenSurface makes
# a context with framebuffer 0 == GL_FRAMEBUFFER_UNDEFINED.
#
# So the batch design is: ONE long-lived context and ONE long-lived pm::Engine on
# the GUI thread, with N jobs interleaving frames through it, each keeping its own
# encoder. Parallelism moves from contexts to encoders.
#
# This file is in the tree because the answer is a measurement, and a
# measurement nobody can reproduce is a comment. RECORDER_SOURCES for the usual
# non-globbing reason.
# ─────────────────────────────────────────────────────────────
list(APPEND RECORDER_SOURCES
    src/recorder/OffscreenRenderSpike.hpp
    src/recorder/OffscreenRenderSpike.cpp
)
