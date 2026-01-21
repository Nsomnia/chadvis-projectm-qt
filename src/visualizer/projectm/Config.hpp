#pragma once
/**
 * @file Config.hpp
 * @brief projectM visualizer configuration structures.
 */

#include <filesystem>
#include <string>
#include <vector>
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
};

} // namespace vc::pm
