/**
 * @file Config.hpp
 * @brief Configuration management singleton.
 *
 * This file defines the Config class which provides a thread-safe singleton
 * for accessing and modifying application settings. It delegates parsing
 * to ConfigParsers and file I/O to ConfigLoader.
 *
 * @section Dependencies
 * - ConfigData
 * - ConfigLoader
 * - ConfigParsers
 *
 * @section Patterns
 * - Singleton: Global access point for configuration.
 * - Thread-Safe: Mutex-protected access to settings.
 */

#pragma once
#include <memory>
#include <mutex>
#include "ConfigData.hpp"
#include "util/Result.hpp"

namespace vc {

class Config {
public:
    static Config& instance();

    Result<void> load(const fs::path& path);
    Result<void> save(const fs::path& path) const;
    Result<void> loadDefault();

    fs::path configPath() const {
        return configPath_;
    }
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
    const std::vector<OverlayElementConfig>& overlayElements() const {
        return overlayElements_;
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
    std::vector<OverlayElementConfig>& overlayElements() {
        markDirty();
        return overlayElements_;
    }

    void addOverlayElement(OverlayElementConfig elem);
    void removeOverlayElement(const std::string& id);
    OverlayElementConfig* findOverlayElement(const std::string& id);

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

#define CONFIG vc::Config::instance()

} // namespace vc
