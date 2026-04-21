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
    qmlRegisterSingletonType<ThemeBridge>("ChadVis", 1, 0, "Theme", ThemeBridge::create);
    qmlRegisterSingletonType<OverlayBridge>("ChadVis", 1, 0, "OverlayBridge", OverlayBridge::create);

    AudioBridge::setAudioEngine(audioEngine);
    PlaylistBridge::setPlaylist(&audioEngine->playlist());
    VisualizerBridge::setVisualizerEngine(visualizer);
    RecordingBridge::setRecorder(recorder);
    PresetBridge::setPresetManager(presetManager);
    
    // Lyrics integration
    LyricsBridge::setAudioEngine(audioEngine);
    LyricsBridge::setLyricsSync(lyricsSync);
    LyricsBridge::connectSignals();

    // Suno integration
    if (sunoController) {
        SunoBridge::setSunoController(sunoController);
    }
}

} // namespace qml_bridge
