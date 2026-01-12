#include "Bridge.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc::pm {

static void presetSwitchRequested(bool is_hard_cut, void* user_data) {
    auto* bridge = static_cast<Bridge*>(user_data);
    if (bridge) {
        LOG_DEBUG("Bridge: presetSwitchRequested (hard_cut={})", is_hard_cut);
        bridge->nextPreset(true);
    }
}

Bridge::Bridge() {
    playlist_.switched.connect([this](bool hard, u32 index) {
        onPlaylistSwitched(hard, index);
    });
}

Bridge::~Bridge() {
    shutdown();
}

Result<void> Bridge::init(const ProjectMConfig& config) {
    LOG_INFO("Bridge: Initializing projectM components");
    
    if (isInitialized())
        shutdown();

    EngineConfig eCfg;
    eCfg.width = config.width;
    eCfg.height = config.height;
    eCfg.fps = config.fps;
    eCfg.beatSensitivity = config.beatSensitivity;
    eCfg.meshX = config.meshX;
    eCfg.meshY = config.meshY;
    eCfg.presetDuration = config.presetDuration;
    eCfg.transitionDuration = config.transitionDuration;
    eCfg.texturePaths = config.texturePaths;

    auto res = engine_.init(eCfg);
    if (!res) return res;

    if (!playlist_.init(engine_.handle())) {
        return Result<void>::err("Failed to initialize native playlist");
    }
    playlist_.setShuffle(config.shufflePresets);

    projectm_set_preset_switch_requested_event_callback(
            engine_.handle(), &presetSwitchRequested, this);

    presetManager_.presetChanged.connect([this](const PresetInfo* p) {
        onPresetManagerChanged(p);
    });

    scanPresets(config.presetPath);
    presetManager_.loadState(file::configDir() / "preset_state.txt");

    if (config.useDefaultPreset) {
        engine_.setPresetDuration(0);
    } else if (!presetManager_.empty()) {
        if (!config.forcePreset.empty()) {
            presetManager_.selectByName(config.forcePreset);
        } else if (config.shufflePresets) {
            randomPreset(false);
        } else {
            presetManager_.selectByIndex(0);
        }
    }

    return Result<void>::ok();
}

void Bridge::shutdown() {
    playlist_.shutdown();
    engine_.shutdown();
}

Result<void> Bridge::scanPresets(const fs::path& path) {
    bool managerEmpty = presetManager_.empty();
    bool playlistEmpty = !playlist_.handle() || playlist_.size() == 0;

    if (path == lastPresetPath_ && !managerEmpty && !playlistEmpty) {
        return Result<void>::ok();
    }

    LOG_INFO("Bridge: Scanning presets in '{}'", path.string());
    
    if (managerEmpty || path != lastPresetPath_) {
        presetManager_.scan(path);
    }

    if (playlist_.handle() && (playlistEmpty || path != lastPresetPath_)) {
        playlist_.clear();
        u32 added = playlist_.addPath(path.string(), true);
        if (added > 0) {
            playlist_.sort();
        }
        LOG_INFO("Bridge: Native playlist populated with {} items", added);
    }

    lastPresetPath_ = path;
    return Result<void>::ok();
}

void Bridge::syncState() {
    if (!isInitialized()) return;

    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        if (!pendingLoadPath_.empty()) {
            engine_.loadPreset(pendingLoadPath_, !pendingSmooth_);
            pendingLoadPath_.clear();
        }
    }

    int pos = pendingPosition_.exchange(-1);
    if (pos != -1) {
        playlist_.setPosition(static_cast<u32>(pos), !pendingSmooth_);
    }

    if (pendingNext_.exchange(false)) {
        playlist_.next(!pendingSmooth_);
    }
    if (pendingPrev_.exchange(false)) {
        playlist_.previous(!pendingSmooth_);
    }
    if (pendingRandom_.exchange(false)) {
        if (playlist_.size() > 0) {
            std::uniform_int_distribution<u32> dist(0, playlist_.size() - 1);
            playlist_.setPosition(dist(rng_), !pendingSmooth_);
        }
    }

    if (pendingLockChange_.exchange(false)) {
        engine_.setPresetLocked(pendingLock_);
    }
}

void Bridge::nextPreset(bool smooth) {
    pendingSmooth_ = smooth;
    pendingNext_ = true;
}

void Bridge::previousPreset(bool smooth) {
    pendingSmooth_ = smooth;
    pendingPrev_ = true;
}

void Bridge::randomPreset(bool smooth) {
    pendingSmooth_ = smooth;
    pendingRandom_ = true;
}

void Bridge::lockPreset(bool locked) {
    presetLocked_ = locked;
    pendingLock_ = locked;
    pendingLockChange_ = true;
}

std::string Bridge::currentPresetName() const {
    const auto* p = presetManager_.current();
    return p ? p->name : "None";
}

void Bridge::onPresetManagerChanged(const PresetInfo* preset) {
    if (!preset || syncingFromNative_)
        return;

    if (playlist_.handle() && playlist_.size() > 0) {
        for (u32 i = 0; i < playlist_.size(); ++i) {
            if (fs::path(playlist_.itemAt(i)) == preset->path) {
                pendingPosition_ = static_cast<int>(i);
                pendingSmooth_ = false;
                return;
            }
        }
    }

    std::lock_guard<std::mutex> lock(loadMutex_);
    pendingLoadPath_ = preset->path.string();
    pendingSmooth_ = false;
}

void Bridge::onPlaylistSwitched(bool is_hard_cut, u32 index) {
    std::string path = playlist_.itemAt(index);
    if (path.empty()) return;

    fs::path p(path);
    std::string name = p.stem().string();

    syncingFromNative_ = true;
    presetManager_.selectByName(name);
    syncingFromNative_ = false;

    presetChanged.emitSignal(name);
}

} // namespace vc::pm
