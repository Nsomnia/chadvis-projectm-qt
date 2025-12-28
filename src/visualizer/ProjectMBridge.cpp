#include "ProjectMBridge.hpp"
#include "util/FileUtils.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc {

ProjectMBridge::ProjectMBridge() = default;

ProjectMBridge::~ProjectMBridge() {
    shutdown();
}

Result<void> ProjectMBridge::init(const ProjectMConfig& config) {
    if (projectM_) {
        shutdown();
    }
    
    width_ = config.width;
    height_ = config.height;
    
    // Create ProjectM instance
    projectM_ = projectm_create();
    if (!projectM_) {
        return Result<void>::err("Failed to create ProjectM instance");
    }
    
    // Configure
    projectm_set_window_size(projectM_, width_, height_);
    projectm_set_fps(projectM_, config.fps);
    projectm_set_beat_sensitivity(projectM_, config.beatSensitivity);
    projectm_set_preset_duration(projectM_, config.presetDuration);
    projectm_set_soft_cut_duration(projectM_, config.transitionDuration);
    projectm_set_mesh_size(projectM_, config.meshX, config.meshY);
    projectm_set_preset_locked(projectM_, false);
    
    // Load presets
    if (!config.presetPath.empty() && fs::exists(config.presetPath)) {
        auto result = presets_.scan(config.presetPath);
        if (!result) {
            LOG_WARN("Failed to scan presets: {}", result.error().message);
        }
        
        // Load state (favorites/blacklist)
        auto statePath = file::configDir() / "preset_state.txt";
        presets_.loadState(statePath);
    }
    
    // Connect preset manager
    presets_.presetChanged.connect([this](const PresetInfo* p) {
        onPresetManagerChanged(p);
    });
    
    // Select initial preset
    if (!presets_.empty()) {
        // Check if a specific preset is forced
        if (!config.forcePreset.empty()) {
            if (presets_.selectByName(config.forcePreset)) {
                LOG_INFO("Forced preset loaded: {}", config.forcePreset);
            } else {
                LOG_WARN("Could not find forced preset: {}, using first available", config.forcePreset);
                presets_.selectByIndex(0);
            }
        } else if (config.shufflePresets) {
            presets_.selectRandom();
            LOG_INFO("Random preset selected: {}", presets_.current()->name);
        } else {
            presets_.selectByIndex(0);
            LOG_INFO("First preset selected: {}", presets_.current()->name);
        }
    } else {
        LOG_WARN("No presets found!");
    }
    
    LOG_INFO("ProjectM initialized: {}x{} @ {} fps, {} presets", 
             width_, height_, config.fps, presets_.count());
    
    return Result<void>::ok();
}

void ProjectMBridge::shutdown() {
    if (projectM_) {
        // Save preset state
        auto statePath = file::configDir() / "preset_state.txt";
        presets_.saveState(statePath);
        
        projectm_destroy(projectM_);
        projectM_ = nullptr;
        
        LOG_INFO("ProjectM shutdown");
    }
}

void ProjectMBridge::render() {
    if (!projectM_) {
        LOG_DEBUG("ProjectMBridge::render(): projectM_ is null");
        return;
    }
    LOG_DEBUG("ProjectMBridge::render(): calling projectm_opengl_render_frame");
    projectm_opengl_render_frame(projectM_);
    LOG_DEBUG("ProjectMBridge::render(): projectm_opengl_render_frame returned");
}

void ProjectMBridge::renderToTarget(RenderTarget& target) {
    if (!projectM_) return;
    
    // Resize if needed
    if (target.width() != width_ || target.height() != height_) {
        resize(target.width(), target.height());
    }
    
    target.bind();
    projectm_opengl_render_frame(projectM_);
    target.unbind();
}

void ProjectMBridge::addPCMData(const f32* data, u32 samples, u32 channels) {
    if (!projectM_) return;
    
    if (channels == 1) {
        projectm_pcm_add_float(projectM_, data, samples, PROJECTM_MONO);
    } else {
        // v4 API: pass interleaved stereo directly
        projectm_pcm_add_float(projectM_, data, samples * channels, PROJECTM_STEREO);
    }
}

void ProjectMBridge::addPCMDataInterleaved(const f32* data, u32 frames, u32 channels) {
    if (!projectM_) {
        LOG_DEBUG("ProjectMBridge::addPCMDataInterleaved: projectM_ is null, skipping");
        return;
    }
    
    LOG_DEBUG("ProjectMBridge::addPCMDataInterleaved: {} frames, {} channels", frames, channels);
    
    if (channels == 1) {
        projectm_pcm_add_float(projectM_, data, frames, PROJECTM_MONO);
    } else if (channels == 2) {
        // v4 API: pass interleaved stereo directly
        projectm_pcm_add_float(projectM_, data, frames * 2, PROJECTM_STEREO);
    }
}

void ProjectMBridge::resize(u32 width, u32 height) {
    if (!projectM_) return;
    if (width == width_ && height == height_) return;
    
    width_ = width;
    height_ = height;
    projectm_set_window_size(projectM_, width_, height_);
    
    LOG_DEBUG("ProjectM resized to {}x{}", width, height);
}

void ProjectMBridge::setFPS(u32 fps) {
    if (projectM_) {
        projectm_set_fps(projectM_, fps);
    }
}

void ProjectMBridge::setBeatSensitivity(f32 sensitivity) {
    if (projectM_) {
        projectm_set_beat_sensitivity(projectM_, sensitivity);
    }
}

void ProjectMBridge::loadPreset(const fs::path& path, bool smooth) {
    if (!projectM_) return;
    
    projectm_load_preset_file(projectM_, path.c_str(), smooth);
    presetChanged.emitSignal(path.stem().string());
    
    LOG_DEBUG("Loaded preset: {}", path.filename().string());
}

void ProjectMBridge::nextPreset(bool smooth) {
    if (presetLocked_) return;
    
    if (presets_.selectNext()) {
        // onPresetManagerChanged will handle loading
    }
}

void ProjectMBridge::previousPreset(bool smooth) {
    if (presetLocked_) return;
    
    if (presets_.selectPrevious()) {
        // onPresetManagerChanged will handle loading
    }
}

void ProjectMBridge::randomPreset(bool smooth) {
    if (presetLocked_) return;
    
    if (presets_.selectRandom()) {
        // onPresetManagerChanged will handle loading
    }
}

void ProjectMBridge::lockPreset(bool locked) {
    presetLocked_ = locked;
    if (projectM_) {
        projectm_set_preset_locked(projectM_, locked);
    }
}

std::string ProjectMBridge::currentPresetName() const {
    if (const auto* preset = presets_.current()) {
        return preset->name;
    }
    return "No preset";
}

void ProjectMBridge::onPresetManagerChanged(const PresetInfo* preset) {
    if (!preset) {
        LOG_WARN("onPresetManagerChanged: preset is null");
        return;
    }
    if (!projectM_) {
        LOG_WARN("onPresetManagerChanged: projectM_ is null, cannot load preset: {}", preset->name);
        return;
    }
    
    LOG_INFO("Loading preset: {} from {}", preset->name, preset->path.string());
    LOG_DEBUG("onPresetManagerChanged: About to call projectm_load_preset_file");
    projectm_load_preset_file(projectM_, preset->path.c_str(), true);
    LOG_DEBUG("onPresetManagerChanged: projectm_load_preset_file returned");
    presetChanged.emitSignal(preset->name);
    LOG_DEBUG("onPresetManagerChanged: Emitted presetChanged signal");
}

} // namespace vc