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
 * - Thread-Safety: THE CONTRACT (T0022). All Config access is main-thread
 *   affine. The thread that first calls instance() owns the object; every
 *   mutating path -- the *Mut() write accessors, setDebug(), load(),
 *   loadDefault(), save() and the overlay-element mutations -- asserts that
 *   affinity in debug builds (assertAffinity(), Config.cpp). Workers must copy
 *   what they need on the main thread; nothing in the tree reads or writes a
 *   section from another thread today.
 *   - Reads: the const accessors (audio(), ui(), ...). They never mark the
 *     config dirty. Because instance() returns a non-const reference, a plain
 *     `CONFIG.suno()` selects the const overload ONLY because the mutable
 *     overloads no longer share its name -- which is exactly why the write
 *     accessors are named *Mut(). Before T0022 the mutable overloads were
 *     name-identical overloads, every read in the tree silently selected them,
 *     and isDirty() was effectively always true at quit.
 *   - Writes: `CONFIG.sunoMut().deviceId = ...` etc. Marks dirty, asserts
 *     affinity. A write through the old spelling is now a compile error, so
 *     "this site only reads" is enforced by the compiler instead of trusted.
 * - The mutex_ still serializes the file-backed paths (load/save/loadDefault/
 *   overlay mutations) as defence in depth; the dirty flag is atomic because
 *   quit-time save() reads it while QML getters read sections.
 */

#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
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
        assertAffinity();
        debug_ = v;
        markDirty();
    }

    // True when the calling thread is the one that owns the config (the thread
    // that first called instance()). Exposed so tests can pin the affinity
    // contract without triggering the debug assert.
    [[nodiscard]] bool onConfigThread() const {
        return std::this_thread::get_id() == owner_;
    }

    // Section accessors (const) -- the read path. Never marks dirty.
    const AudioConfig& audio() const { return audio_; }
    const VisualizerConfig& visualizer() const { return visualizer_; }
    const RecordingConfig& recording() const { return recording_; }
    const UIConfig& ui() const { return ui_; }
    const KeyboardConfig& keyboard() const { return keyboard_; }
    const SunoConfig& suno() const { return suno_; }
    const KaraokeConfig& karaoke() const { return karaoke_; }
    const OverlayConfig& overlay() const { return overlay_; }

    // Section write accessors (T0022). Deliberately NOT overloads of the const
    // names: with `Config::instance()` handing out a non-const reference, an
    // overload pair made every plain read -- `CONFIG.visualizer().fps`,
    // `const auto& v = CONFIG.visualizer()` -- bind to the MUTABLE overload and
    // mark the config dirty, so quit-time isDirty() was almost always true.
    // Distinct names make the read path const by default and make every write
    // site say "write" where grep (and review) can see it.
    AudioConfig& audioMut() {
        assertAffinity();
        markDirty();
        return audio_;
    }
    VisualizerConfig& visualizerMut() {
        assertAffinity();
        markDirty();
        return visualizer_;
    }
    RecordingConfig& recordingMut() {
        assertAffinity();
        markDirty();
        return recording_;
    }
    UIConfig& uiMut() {
        assertAffinity();
        markDirty();
        return ui_;
    }
    KeyboardConfig& keyboardMut() {
        assertAffinity();
        markDirty();
        return keyboard_;
    }
    SunoConfig& sunoMut() {
        assertAffinity();
        markDirty();
        return suno_;
    }
    KaraokeConfig& karaokeMut() {
        assertAffinity();
        markDirty();
        return karaoke_;
    }
    OverlayConfig& overlayMut() {
        assertAffinity();
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
    // The thread that first calls instance() owns the object. Magic-static
    // initialisation publishes `owner_` to every later caller of instance()
    // with a happens-before edge, so reading it from any thread that reached
    // the singleton legally is race-free.
    Config() : owner_(std::this_thread::get_id()) {}

    // Debug-build enforcement of the affinity contract: every mutating path
    // calls this. A violation is UB, not a benign stale read (std::string
    // tears), so failing loudly in debug/sanitizer builds is the point.
    void assertAffinity() const;

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

    std::thread::id owner_;

    // `mutable` so the const save() can clear it on success (T0023: save()
    // never cleared the flag, so the file was rewritten on every quit).
    mutable std::atomic<bool> dirty_{false};

    mutable std::mutex mutex_;
};

#define CONFIG vc::Config::instance()

} // namespace vc
