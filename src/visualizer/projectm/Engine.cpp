#include "Engine.hpp"
#include <algorithm>
#include "core/Logger.hpp"

namespace vc::pm {

Engine::Engine() = default;

Engine::~Engine() {
    shutdown();
}

Result<void> Engine::init(const EngineConfig& config) {
    if (handle_)
        shutdown();

    width_ = config.width;
    height_ = config.height;

    handle_ = projectm_create();
    if (!handle_)
        return Result<void>::err("Failed to create projectM instance");

    projectm_set_window_size(handle_, width_, height_);
    projectm_set_fps(handle_, config.fps);
    projectm_set_beat_sensitivity(handle_, config.beatSensitivity);
    projectm_set_mesh_size(handle_, config.meshX, config.meshY);
    projectm_set_preset_duration(handle_, config.presetDuration);
    projectm_set_soft_cut_duration(handle_, config.transitionDuration);
    projectm_set_preset_locked(handle_, false);

    if (!config.texturePaths.empty()) {
        std::vector<std::string> pathStrings;
        std::vector<const char*> paths;
        pathStrings.reserve(config.texturePaths.size());
        paths.reserve(config.texturePaths.size());

        for (const auto& p : config.texturePaths) {
            if (fs::exists(p)) {
                pathStrings.push_back(p.string());
                paths.push_back(pathStrings.back().c_str());
            }
        }

        if (!paths.empty()) {
            projectm_set_texture_search_paths(
                    handle_, paths.data(), paths.size());
        }
    }

    return Result<void>::ok();
}

void Engine::shutdown() {
    if (handle_) {
        projectm_destroy(handle_);
        handle_ = nullptr;
    }
}

void Engine::render() {
    if (handle_)
        projectm_opengl_render_frame(handle_);
}

void Engine::renderToTarget(RenderTarget& target) {
    if (!handle_)
        return;
    target.bind();
    projectm_opengl_render_frame(handle_);
    target.unbind();
}

void Engine::addPCMData(const f32* data, u32 samples, u32 channels) {
    if (handle_)
        projectm_pcm_add_float(handle_,
                               data,
                               samples,
                               static_cast<projectm_channels>(channels));
}

void Engine::addPCMDataInterleaved(const f32* data, u32 frames, u32 channels) {
    if (handle_)
        projectm_pcm_add_float(handle_,
                               data,
                               frames,
                               static_cast<projectm_channels>(channels));
}

void Engine::resize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    if (handle_)
        projectm_set_window_size(handle_, width, height);
}

void Engine::setFPS(u32 fps) {
    if (handle_)
        projectm_set_fps(handle_, fps);
}

void Engine::setBeatSensitivity(f32 sensitivity) {
    if (handle_)
        projectm_set_beat_sensitivity(handle_, sensitivity);
}

void Engine::setPresetDuration(double seconds) {
    if (handle_)
        projectm_set_preset_duration(handle_, seconds);
}

void Engine::setSoftCutDuration(double seconds) {
    if (handle_)
        projectm_set_soft_cut_duration(handle_, seconds);
}

void Engine::setPresetLocked(bool locked) {
    if (handle_)
        projectm_set_preset_locked(handle_, locked);
}

void Engine::loadPreset(const std::string& path, bool immediate) {
    if (handle_)
        projectm_load_preset_file(handle_, path.c_str(), immediate);
}

} // namespace vc::pm
