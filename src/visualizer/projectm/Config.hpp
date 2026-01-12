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
    bool shufflePresets{true};
    std::string forcePreset{};
    bool useDefaultPreset{false};
    u32 meshX{128};
    u32 meshY{96};
    std::vector<fs::path> texturePaths;
};

} // namespace vc::pm
