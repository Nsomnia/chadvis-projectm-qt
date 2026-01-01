#include "ProjectMBridge.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

ProjectMBridge::ProjectMBridge() = default;

ProjectMBridge::~ProjectMBridge() {
    shutdown();
}

Result<void> ProjectMBridge::init(const ProjectMConfig& config) {
    if (projectM_)
        shutdown();
    width_ = config.width;
    height_ = config.height;
    shuffleEnabled_ = config.shufflePresets;

    projectM_ = projectm_create();
    if (!projectM_)
        return Result<void>::err("Failed to create ProjectM instance");

    projectm_set_window_size(projectM_, width_, height_);
    projectm_set_fps(projectM_, config.fps);
    projectm_set_beat_sensitivity(projectM_, config.beatSensitivity);
    projectm_set_preset_duration(projectM_, 0);
    projectm_set_soft_cut_duration(projectM_, config.transitionDuration);
    projectm_set_mesh_size(projectM_, config.meshX, config.meshY);
    projectm_set_preset_locked(projectM_, false);

    presets_.presetChanged.connect(
            [this](const PresetInfo* p) { onPresetManagerChanged(p); });

    if (!config.presetPath.empty() && fs::exists(config.presetPath)) {
        presets_.scan(config.presetPath);
        presets_.loadState(file::configDir() / "preset_state.txt");
    }

    if (config.useDefaultPreset)
        LOG_INFO("Using default visualizer");
    else if (!presets_.empty()) {
        if (!config.forcePreset.empty()) {
            if (!presets_.selectByName(config.forcePreset))
                presets_.selectByIndex(0);
        } else if (config.shufflePresets)
            presets_.selectRandom();
        else
            presets_.selectByIndex(0);
    }

    return Result<void>::ok();
}

void ProjectMBridge::shutdown() {
    if (projectM_) {
        presets_.saveState(file::configDir() / "preset_state.txt");
        projectm_destroy(projectM_);
        projectM_ = nullptr;
    }
}

void ProjectMBridge::render() {
    if (!projectM_)
        return;
    projectm_opengl_render_frame(projectM_);
}

void ProjectMBridge::renderToTarget(RenderTarget& target) {
    if (!projectM_)
        return;
    if (target.width() != width_ || target.height() != height_)
        resize(target.width(), target.height());
    target.bind();
    glViewport(0, 0, target.width(), target.height());
    projectm_opengl_render_frame(projectM_);
    target.unbind();
}

void ProjectMBridge::addPCMData(const f32* data, u32 samples, u32 channels) {
    if (!projectM_)
        return;
    projectm_pcm_add_float(projectM_,
                           data,
                           samples,
                           channels == 1 ? PROJECTM_MONO : PROJECTM_STEREO);
}

void ProjectMBridge::addPCMDataInterleaved(const f32* data,
                                           u32 frames,
                                           u32 channels) {
    if (!projectM_)
        return;
    projectm_pcm_add_float(projectM_,
                           data,
                           frames,
                           channels == 1 ? PROJECTM_MONO : PROJECTM_STEREO);
}

void ProjectMBridge::resize(u32 width, u32 height) {
    if (!projectM_ || (width == width_ && height == height_))
        return;
    width_ = width;
    height_ = height;
    projectm_set_window_size(projectM_, width_, height_);
}

void ProjectMBridge::resetViewport(u32 width, u32 height) {
    if (!projectM_)
        return;
    width_ = width;
    height_ = height;
    projectm_set_window_size(projectM_, width, height);
    glViewport(0, 0, width, height);
}

void ProjectMBridge::setFPS(u32 fps) {
    if (projectM_)
        projectm_set_fps(projectM_, fps);
}
void ProjectMBridge::setBeatSensitivity(f32 s) {
    if (projectM_)
        projectm_set_beat_sensitivity(projectM_, s);
}

void ProjectMBridge::loadPreset(const fs::path& path, bool smooth) {
    if (!projectM_)
        return;
    projectm_load_preset_file(projectM_, path.c_str(), false);
    presetChanged.emitSignal(path.stem().string());
}

void ProjectMBridge::nextPreset(bool smooth) {
    if (presetLocked_)
        return;
    if (shuffleEnabled_)
        presets_.selectRandom();
    else
        presets_.selectNext();
}

void ProjectMBridge::previousPreset(bool smooth) {
    if (presetLocked_)
        return;
    if (shuffleEnabled_)
        presets_.selectRandom();
    else
        presets_.selectPrevious();
}

void ProjectMBridge::randomPreset(bool smooth) {
    if (!presetLocked_)
        presets_.selectRandom();
}

void ProjectMBridge::lockPreset(bool locked) {
    presetLocked_ = locked;
    if (projectM_)
        projectm_set_preset_locked(projectM_, locked);
}

std::string ProjectMBridge::currentPresetName() const {
    return presets_.current() ? presets_.current()->name : "No preset";
}

void ProjectMBridge::onPresetManagerChanged(const PresetInfo* preset) {
    if (!preset || !projectM_)
        return;
    presetChanged.emitSignal(preset->name);
}

} // namespace vc
