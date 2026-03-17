/**
 * @file BridgeRegistration.cpp
 * @brief Implementation of bridge registration
 */

#include "BridgeRegistration.hpp"
#include "AudioBridge.hpp"
#include "PlaylistBridge.hpp"
#include "VisualizerBridge.hpp"
#include <QQmlEngine>

namespace qml_bridge {

namespace {
    AudioBridge* s_audioBridge = nullptr;
    PlaylistBridge* s_playlistBridge = nullptr;
    VisualizerBridge* s_visualizerBridge = nullptr;
}

void registerBridges(QQmlEngine* engine,
                     vc::AudioEngine* audioEngine,
                     vc::VisualizerWindow* visualizer)
{
    // Create bridge instances with QML engine ownership
    s_audioBridge = AudioBridge::create(engine, nullptr);
    s_playlistBridge = PlaylistBridge::create(engine, nullptr);
    s_visualizerBridge = VisualizerBridge::create(engine, nullptr);

    // Connect to backend
    if (audioEngine) {
        s_audioBridge->setAudioEngine(audioEngine);
        s_playlistBridge->setAudioEngine(audioEngine);
    }

    if (visualizer) {
        s_visualizerBridge->setVisualizerWindow(visualizer);
    }

    // Register as QML singletons (type names match class names)
    qmlRegisterSingletonInstance("ChadVis", 1, 0, "AudioBridge", s_audioBridge);
    qmlRegisterSingletonInstance("ChadVis", 1, 0, "PlaylistBridge", s_playlistBridge);
    qmlRegisterSingletonInstance("ChadVis", 1, 0, "VisualizerBridge", s_visualizerBridge);
}

AudioBridge* getAudioBridge()
{
    return s_audioBridge;
}

PlaylistBridge* getPlaylistBridge()
{
    return s_playlistBridge;
}

VisualizerBridge* getVisualizerBridge()
{
    return s_visualizerBridge;
}

} // namespace qml_bridge
