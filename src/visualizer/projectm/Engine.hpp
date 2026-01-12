#pragma once
/**
 * @file Engine.hpp
 * @brief Core projectM instance wrapper for rendering and audio processing.
 */

#include "projectM-4/projectM.h"
#include <filesystem>
#include <string>
#include <vector>
#include "util/Result.hpp"
#include "util/Types.hpp"
#include "visualizer/RenderTarget.hpp"

namespace fs = std::filesystem;

namespace vc::pm {

/**
 * @brief Configuration for the projectM Engine.
 */
struct EngineConfig {
    u32 width{1280};
    u32 height{720};
    u32 fps{60};
    f32 beatSensitivity{1.0f};
    u32 meshX{128};
    u32 meshY{96};
    u32 presetDuration{30};
    u32 transitionDuration{3};
    std::vector<fs::path> texturePaths;
};

/**
 * @brief Wraps the projectm_handle and provides low-level control.
 */
class Engine {
public:
    Engine();
    ~Engine();

    // Non-copyable
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /**
     * @brief Initialize projectM. Must be called with an active OpenGL context.
     */
    Result<void> init(const EngineConfig& config);

    /**
     * @brief Clean up projectM resources.
     */
    void shutdown();

    /**
     * @brief Check if engine is initialized.
     */
    bool isInitialized() const {
        return handle_ != nullptr;
    }

    /**
     * @brief Render a single frame to the current OpenGL framebuffer.
     */
    void render();

    /**
     * @brief Render to a specific RenderTarget.
     */
    void renderToTarget(RenderTarget& target);

    /**
     * @brief Add PCM audio data.
     * @param data Pointer to float samples.
     * @param samples Number of samples per channel.
     * @param channels Number of channels (1 or 2).
     */
    void addPCMData(const f32* data, u32 samples, u32 channels);

    /**
     * @brief Add interleaved PCM audio data.
     */
    void addPCMDataInterleaved(const f32* data, u32 frames, u32 channels);

    /**
     * @brief Resize the internal projectM render buffers.
     */
    void resize(u32 width, u32 height);

    /**
     * @brief Set the target frames per second.
     */
    void setFPS(u32 fps);

    /**
     * @brief Set beat detection sensitivity.
     */
    void setBeatSensitivity(f32 sensitivity);

    /**
     * @brief Set the duration each preset stays active.
     * @param seconds Duration in seconds (0 to disable auto-rotation).
     */
    void setPresetDuration(double seconds);

    /**
     * @brief Set the duration of the transition between presets.
     */
    void setSoftCutDuration(double seconds);

    /**
     * @brief Lock or unlock the current preset.
     */
    void setPresetLocked(bool locked);

    /**
     * @brief Load a preset from a file.
     * @param path Full path to the .milk file.
     * @param immediate If true, switch immediately without transition.
     */
    void loadPreset(const std::string& path, bool immediate = false);

    /**
     * @brief Get the underlying projectM handle.
     */
    projectm_handle handle() {
        return handle_;
    }

private:
    projectm_handle handle_{nullptr};
    u32 width_{0};
    u32 height_{0};
};

} // namespace vc::pm
