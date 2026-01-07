#pragma once
// Config.hpp - TOML configuration management
// Because hardcoding values is a code smell and we're not animals

#include <toml++/toml.h>
#include <mutex>
#include <optional>
#include <vector>
#include "util/Result.hpp"
#include "util/Types.hpp"

namespace vc {

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
};

// Audio encoding settings
struct AudioEncoderConfig {
    std::string codec{"aac"};
    u32 bitrate{320};
};

// Recording configuration
struct RecordingConfig {
    bool enabled{true};
    bool autoRecord{false};
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

// Main configuration class
class Config {
public:
    // Singleton access
    static Config& instance();

    // Load/save
    Result<void> load(const fs::path& path);
    Result<void> save(const fs::path& path) const;
    Result<void> loadDefault();

    // Get config file path
    fs::path configPath() const {
        return configPath_;
    }

    // General
    bool debug() const {
        return debug_;
    }
    void setDebug(bool v) {
        debug_ = v;
        markDirty();
    }

    // Section accessors (const)
    const AudioConfig& audio() const {
        return audio_;
    }
    const VisualizerConfig& visualizer() const {
        return visualizer_;
    }
    const RecordingConfig& recording() const {
        return recording_;
    }
    const UIConfig& ui() const {
        return ui_;
    }
    const KeyboardConfig& keyboard() const {
        return keyboard_;
    }
    const SunoConfig& suno() const {
        return suno_;
    }

    // Section accessors (mutable)
    AudioConfig& audio() {
        markDirty();
        return audio_;
    }
    VisualizerConfig& visualizer() {
        markDirty();
        return visualizer_;
    }
    RecordingConfig& recording() {
        markDirty();
        return recording_;
    }
    UIConfig& ui() {
        markDirty();
        return ui_;
    }
    KeyboardConfig& keyboard() {
        markDirty();
        return keyboard_;
    }
    SunoConfig& suno() {
        markDirty();
        return suno_;
    }

    // Overlay elements
    const std::vector<OverlayElementConfig>& overlayElements() const {
        return overlayElements_;
    }
    std::vector<OverlayElementConfig>& overlayElements() {
        markDirty();
        return overlayElements_;
    }

    void addOverlayElement(OverlayElementConfig elem);
    void removeOverlayElement(const std::string& id);
    OverlayElementConfig* findOverlayElement(const std::string& id);

    // Dirty tracking for auto-save
    bool isDirty() const {
        return dirty_;
    }
    void markClean() {
        dirty_ = false;
    }

private:
    Config() = default;
    void markDirty() {
        dirty_ = true;
    }

    // Parse helpers
    void parseAudio(const toml::table& tbl);
    void parseVisualizer(const toml::table& tbl);
    void parseRecording(const toml::table& tbl);
    void parseOverlay(const toml::table& tbl);
    void parseUI(const toml::table& tbl);
    void parseKeyboard(const toml::table& tbl);
    void parseSuno(const toml::table& tbl);

    // Serialize helpers
    toml::table serialize() const;

    fs::path configPath_;
    bool dirty_{false};
    bool debug_{false};

    AudioConfig audio_;
    VisualizerConfig visualizer_;
    RecordingConfig recording_;
    UIConfig ui_;
    KeyboardConfig keyboard_;
    SunoConfig suno_;
    std::vector<OverlayElementConfig> overlayElements_;

    mutable std::mutex mutex_;
};

// Convenience macro
#define CONFIG vc::Config::instance()

} // namespace vc
