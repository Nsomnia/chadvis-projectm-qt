/**
 * @file ConfigData.hpp
 * @brief Configuration data structures.
 *
 * This file defines the POD (Plain Old Data) structs used to hold configuration
 * values. It is separated from the logic classes to keep headers lean and
 * avoid circular dependencies.
 */

#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc {

namespace fs = std::filesystem;

// Text overlay element configuration
struct OverlayElementConfig {
    std::string id;
    std::string text;
    Vec2 position{0.5f, 0.5f};
    u32 fontSize{32};
    Color color{Color::white()};
    f32 opacity{1.0f};
    std::string animation{"none"};
    f32 animationSpeed{1.0f};
    std::string anchor{"left"}; // left, center, right
    bool visible{true};
};

// Video encoding settings
struct VideoEncoderConfig {
    std::string codec{"libx264"};
    u32 crf{18};
    std::string preset{"medium"};
    std::string pixelFormat{"yuv420p"};
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};

    std::string codecName() const {
        return codec;
    }
    std::string presetName() const {
        return preset;
    }
    u32 gopSize{0};
    u32 bFrames{0};
};

// Audio encoding settings
struct AudioEncoderConfig {
    std::string codec{"aac"};
    u32 bitrate{320};

    std::string codecName() const {
        return codec;
    }
    u32 sampleRate{48000};
    u32 channels{2};
};

// Recording configuration
struct RecordingConfig {
    bool enabled{true};
    bool autoRecord{false};
    bool recordEntireSong{false};
    bool restartTrackOnRecord{false};
    bool stopAtTrackEnd{false};
    fs::path outputDirectory;
    std::string defaultFilename{"chadvis-projectm-qt_{date}_{time}"};
    std::string container{"mp4"};
    VideoEncoderConfig video;
    AudioEncoderConfig audio;
};

// Visualizer configuration
struct VisualizerConfig {
    fs::path presetPath;
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    f32 beatSensitivity{1.0f};
    u32 presetDuration{30};
    u32 smoothPresetDuration{5};
    bool shufflePresets{true};
    std::string forcePreset{}; // Force specific preset for debugging
    bool useDefaultPreset{false}; // Use default projectM visualizer (no preset)
    bool lowResourceMode{false};
    std::vector<fs::path> texturePaths;
};

// Audio configuration
struct AudioConfig {
    std::string device{"default"};
    u32 bufferSize{2048};
    u32 sampleRate{44100};
};

// UI configuration
struct UIConfig {
    std::string theme{"dark"};
    bool showPlaylist{true};
    bool showPresets{true};
    bool showDebugPanel{false};
    Color backgroundColor{Color::black()};
    Color accentColor{Color::fromHex("#00FF88")};
};

// Keyboard shortcuts
struct KeyboardConfig {
    std::string playPause{"Space"};
    std::string nextTrack{"N"};
    std::string prevTrack{"P"};
    std::string toggleRecord{"R"};
    std::string toggleFullscreen{"F"};
    std::string nextPreset{"Right"};
    std::string prevPreset{"Left"};
};

struct SunoConfig {
    std::string token;
    std::string cookie;
    fs::path downloadPath;
    bool autoDownload{false};
    bool saveLyrics{true};
    bool embedMetadata{true};
};

} // namespace vc
