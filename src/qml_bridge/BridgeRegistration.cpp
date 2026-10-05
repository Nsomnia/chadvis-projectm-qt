#include "BridgeRegistration.hpp"
#include "AudioBridge.hpp"
#include "PlaylistBridge.hpp"
#include "VisualizerBridge.hpp"
#include "RecordingBridge.hpp"
#include "PresetBridge.hpp"
#include "LyricsBridge.hpp"
#include "SunoBridge.hpp"
#include "ThemeBridge.hpp"
#include "OverlayBridge.hpp"
#include "SettingsBridge.hpp"

#include "audio/AudioEngine.hpp"
#include "suno/SunoClient.hpp"
#include "ui/controllers/SunoController.hpp"

namespace qml_bridge {

void registerBridges(QQmlApplicationEngine* engine,
    vc::AudioEngine* audioEngine,
    vc::VisualizerWindow* visualizer,
    vc::VideoRecorder* recorder,
    vc::PresetManager* presetManager,
    vc::LyricsSync* lyricsSync,
    vc::suno::SunoController* sunoController) 
{
    qmlRegisterSingletonType<AudioBridge>("ChadVis", 1, 0, "AudioBridge", AudioBridge::create);
    qmlRegisterSingletonType<PlaylistBridge>("ChadVis", 1, 0, "PlaylistBridge", PlaylistBridge::create);
    qmlRegisterSingletonType<VisualizerBridge>("ChadVis", 1, 0, "VisualizerBridge", VisualizerBridge::create);
    qmlRegisterSingletonType<RecordingBridge>("ChadVis", 1, 0, "RecordingBridge", RecordingBridge::create);
    qmlRegisterSingletonType<PresetBridge>("ChadVis", 1, 0, "PresetBridge", PresetBridge::create);
    qmlRegisterSingletonType<LyricsBridge>("ChadVis", 1, 0, "LyricsBridge", LyricsBridge::create);
    qmlRegisterSingletonType<SunoBridge>("ChadVis", 1, 0, "SunoBridge", SunoBridge::create);
    // NOTE: "Theme" is intentionally NOT registered here. The QML-side
    // styles/Theme.qml singleton (full token set) owns that name; registering
    // the C++ ThemeBridge under it shadows the QML singleton and leaves most
    // tokens (fontDisplay, spacingXL, shadows, ...) undefined at runtime.
    qmlRegisterSingletonType<OverlayBridge>("ChadVis", 1, 0, "OverlayBridge", OverlayBridge::create);
    qmlRegisterSingletonType<SettingsBridge>("ChadVis", 1, 0, "SettingsBridge", SettingsBridge::create);

    // SunoWorkspaceBridge is DELIBERATELY not registered, and adding the one
    // registration line is not the change. Its `vc::suno::SunoWorkspace` is honest
    // as of 2026-10-05 — generation refuses with a named reason, a render is a real
    // `RenderJob` submitted to a real `RenderQueue`, and every render signal is a
    // translation of a queue verdict — but two producer-side gaps remain, and both
    // are outside this function:
    //
    //   1. The bridge `new`s its own private `SunoWorkspace` in its constructor
    //      (SunoWorkspaceBridge.cpp:16) instead of taking an injected one, so it
    //      has no way to hand it a `RenderQueue`. `SunoWorkspace::startRender`
    //      refuses with `WorkspaceRefusalKind::NoQueueAttached` without one, so
    //      registering it today yields a Render button that names that refusal --
    //      honest, and still a dead button. The seam is `SunoWorkspace::setQueue`,
    //      which needs the queue this function has no argument for.
    //   2. There is no `RenderFrameBackend` in the tree, so the only render outcome
    //      reachable on this machine is `FailedPermanent` with the reason "no
    //      RenderFrameBackend: offline rendering has no drawable owner in this
    //      build" (RenderExecutor.cpp:489-494). That is a true statement about the
    //      platform, not a defect in the workspace, and it is why the bridge is
    //      still the wrong thing to expose: a UI whose only possible render result
    //      is a failure should not ship before the backend does.
    //
    // Register it when a backend exists AND the bridge takes an injected queue. The
    // honest-state suite for the class is `tests/unit/suno/test_SunoWorkspace.cpp`.

    AudioBridge::setAudioEngine(audioEngine);
    PlaylistBridge::setPlaylist(&audioEngine->playlist());
    VisualizerBridge::setVisualizerEngine(visualizer);
    RecordingBridge::setRecorder(recorder);
    // The recorder's bridge needs the lyrics as well as the recorder: it is the
    // only layer that holds both, and it muxes the ASS document into the file at
    // startRecording. RecordingBridge::startRecording is the sole production
    // caller of VideoRecorder::start, so without this line nothing else can.
    RecordingBridge::setLyricsSync(lyricsSync);
    PresetBridge::setPresetManager(presetManager);
    
    // Lyrics integration
    LyricsBridge::setAudioEngine(audioEngine);
    LyricsBridge::setLyricsSync(lyricsSync);
    LyricsBridge::connectSignals();

    // Without this the audio device / buffer / sample-rate settings persist and
    // apply on the next start, and SettingsBridge.audioDeviceNotice says exactly
    // that rather than pretending they took effect. It is here so the panel is
    // live, not so the feature works: the config reaches the engine at init.
    SettingsBridge::setAudioEngine(audioEngine);

    // Suno integration
    if (sunoController) {
        SettingsBridge::setSunoClient(sunoController->client());
        SunoBridge::setSunoController(sunoController);
    }
}

} // namespace qml_bridge
