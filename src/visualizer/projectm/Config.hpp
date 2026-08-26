#pragma once
/**
 * @file Config.hpp
 * @file Purpose: projectM visualizer configuration structure + conversion from
 *                the canonical vc::VisualizerConfig.
 *                Does NOT parse TOML or own global config state (see core/Config).
 *
 * @version 1.1.0 - 2026-08-25
 */

#include <filesystem>
#include <string>
#include <vector>
#include "core/ConfigData.hpp"
#include "util/Types.hpp"

namespace fs = std::filesystem;

namespace vc::pm {

/**
 * @brief Configuration for the projectM visualizer.
 */
struct ProjectMConfig {
    u32 width{1920};
    u32 height{1080};
    u32 fps{60};
    f32 beatSensitivity{1.0f};
    fs::path presetPath;
    u32 presetDuration{30};
    u32 transitionDuration{3};
    f32 hardCutSensitivity{1.0f};
    bool aspectCorrection{true};
    bool shufflePresets{true};
    std::string forcePreset{};
    bool useDefaultPreset{false};
    u32 meshX{32};
    u32 meshY{24};
    std::vector<fs::path> texturePaths;

    /**
     * @brief Build a ProjectMConfig from the canonical VisualizerConfig.
     *
     * Single source of truth for the field mapping (notably
     * smoothPresetDuration -> transitionDuration, i.e. projectM soft-cut).
     * widthOverride/heightOverride of 0 fall back to the configured values,
     * letting callers pass actual render-target dimensions.
     */
    [[nodiscard]] static ProjectMConfig
    fromVisualizer(const VisualizerConfig& viz, u32 widthOverride = 0, u32 heightOverride = 0) {
        ProjectMConfig cfg;
        cfg.width = widthOverride > 0 ? widthOverride : viz.width;
        cfg.height = heightOverride > 0 ? heightOverride : viz.height;
        cfg.fps = viz.fps;
        cfg.beatSensitivity = viz.beatSensitivity;
        cfg.presetPath = viz.presetPath;
        cfg.presetDuration = viz.presetDuration;
        cfg.transitionDuration = viz.smoothPresetDuration;
        cfg.hardCutSensitivity = viz.hardCutSensitivity;
        cfg.aspectCorrection = viz.aspectCorrection;
        cfg.shufflePresets = viz.shufflePresets;
        cfg.forcePreset = viz.forcePreset;
        cfg.useDefaultPreset = viz.useDefaultPreset;
        cfg.meshX = viz.meshX;
        cfg.meshY = viz.meshY;
        cfg.texturePaths = viz.texturePaths;
        return cfg;
    }
};

} // namespace vc::pm
