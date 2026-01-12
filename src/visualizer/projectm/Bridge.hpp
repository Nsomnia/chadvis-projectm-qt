#pragma once

#include "Config.hpp"
#include "Engine.hpp"
#include "Playlist.hpp"
#include "visualizer/PresetManager.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include <memory>
#include <random>
#include <atomic>
#include <mutex>
#include <optional>

namespace vc::pm {

class Bridge {
public:
    Bridge();
    ~Bridge();

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    Result<void> init(const ProjectMConfig& config);
    void shutdown();
    bool isInitialized() const { return engine_.isInitialized(); }
    Result<void> scanPresets(const fs::path& path);

    Engine& engine() { return engine_; }
    Playlist& playlist() { return playlist_; }
    PresetManager& presets() { return presetManager_; }

    void nextPreset(bool smooth = true);
    void previousPreset(bool smooth = true);
    void randomPreset(bool smooth = true);
    void lockPreset(bool locked);
    bool isPresetLocked() const { return presetLocked_; }

    void syncState();

    std::string currentPresetName() const;

    Signal<std::string> presetChanged;
    Signal<bool> presetLoading;

private:
    void onPresetManagerChanged(const PresetInfo* preset);
    void onPlaylistSwitched(bool is_hard_cut, u32 index);

    Engine engine_;
    Playlist playlist_;
    PresetManager presetManager_;

    bool presetLocked_{false};
    bool syncingFromNative_{false};
    fs::path lastPresetPath_;
    
    std::atomic<int> pendingPosition_{-1};
    std::atomic<bool> pendingNext_{false};
    std::atomic<bool> pendingPrev_{false};
    std::atomic<bool> pendingRandom_{false};
    std::atomic<bool> pendingSmooth_{true};
    std::atomic<bool> pendingLock_{false};
    std::atomic<bool> pendingLockChange_{false};
    
    std::mutex loadMutex_;
    std::string pendingLoadPath_;
    
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace vc::pm
