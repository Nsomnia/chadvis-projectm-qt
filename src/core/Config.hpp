/**
 * @file Config.hpp
 * @brief Configuration management singleton.
 *
 * This file defines the Config class which provides a singleton for
 * accessing and modifying application settings. It delegates parsing
 * to ConfigParsers and file I/O to ConfigLoader.
 *
 * @section Dependencies
 * - ConfigData
 * - ConfigLoader
 * - ConfigParsers
 *
 * @section Patterns
 * - Singleton: Global access point for configuration.
 * - Thread-Safety: mutex_ protects exactly the file-backed operations --
 *   load(), save(), loadDefault() and the overlay-element mutations. The
 *   section accessors return direct references and are NOT lock-protected
 *   (a returned reference outlives any lock the accessor could hold):
 *   read through the const overloads (never marks dirty), write through
 *   the mutable overloads (marks dirty) from the same thread that calls
 *   save(). The dirty flag itself is atomic because the unsynchronised
 *   accessor writes race the flag updates inside the locked paths.
 */

#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include "ConfigData.hpp"
#include "util/Result.hpp"

namespace vc {

class Config {
public:
    static Config& instance();

    [[nodiscard]] Result<void> load(const fs::path& path);
    [[nodiscard]] Result<void> save(const fs::path& path) const;
    [[nodiscard]] Result<void> loadDefault();

    fs::path configPath() const { return configPath_; }
    bool debug() const { return debug_; }
    void setDebug(bool v) {
        debug_ = v;
        markDirty();
    }

    // Section accessors (const)
    const AudioConfig& audio() const { return audio_; }
    const VisualizerConfig& visualizer() const { return visualizer_; }
    const RecordingConfig& recording() const { return recording_; }
    const UIConfig& ui() const { return ui_; }
    const KeyboardConfig& keyboard() const { return keyboard_; }
    const SunoConfig& suno() const { return suno_; }
    const KaraokeConfig& karaoke() const { return karaoke_; }
    const OverlayConfig& overlay() const { return overlay_; }

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
    KaraokeConfig& karaoke() {
        markDirty();
        return karaoke_;
    }
    OverlayConfig& overlay() {
        markDirty();
        return overlay_;
    }

    void addOverlayElement(OverlayElementConfig elem);
    void removeOverlayElement(const std::string& id);
    OverlayElementConfig* findOverlayElement(const std::string& id);

    bool isDirty() const { return dirty_.load(std::memory_order_relaxed); }
    // const so save() (a const member, called on the singleton) can clear the
    // flag on a successful write; the flag is `mutable` for the same reason.
    void markClean() const { dirty_.store(false, std::memory_order_relaxed); }

private:
    Config() = default;
    void markDirty() { dirty_.store(true, std::memory_order_relaxed); }

    friend class ConfigLoader;

    fs::path configPath_;
    bool debug_{false};

    AudioConfig audio_;
    VisualizerConfig visualizer_;
    RecordingConfig recording_;
    UIConfig ui_;
    KeyboardConfig keyboard_;
    SunoConfig suno_;
    KaraokeConfig karaoke_;
    OverlayConfig overlay_;

    // `mutable` so the const save() can clear it on success (T0023: save()
    // never cleared the flag, so the file was rewritten on every quit).
    mutable std::atomic<bool> dirty_{false};

    mutable std::mutex mutex_;
};

#define CONFIG vc::Config::instance()

} // namespace vc
