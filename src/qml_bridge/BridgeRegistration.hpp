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
}

namespace qml_bridge {
class AudioBridge;
class PlaylistBridge;
class VisualizerBridge;

/**
 * @brief Registers all QML bridge singletons with the QML engine
 *
 * Must be called after AudioEngine and other core components are initialized.
 * Each bridge is registered as a QML singleton accessible by its class name.
 */
void registerBridges(QQmlEngine* engine,
                     vc::AudioEngine* audioEngine,
                     vc::VisualizerWindow* visualizer);

/**
 * @brief Gets the AudioBridge singleton instance
 */
AudioBridge* getAudioBridge();

/**
 * @brief Gets the PlaylistBridge singleton instance
 */
PlaylistBridge* getPlaylistBridge();

/**
 * @brief Gets the VisualizerBridge singleton instance
 */
VisualizerBridge* getVisualizerBridge();

} // namespace qml_bridge
