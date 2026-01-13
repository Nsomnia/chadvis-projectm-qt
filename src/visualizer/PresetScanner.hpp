/**
 * @file PresetScanner.hpp
 * @brief Preset directory scanning and parsing.
 *
 * This file defines the PresetScanner class which handles the filesystem
 * operations to find preset files and parse their metadata (author, name).
 *
 * @section Dependencies
 * - PresetData
 * - std::filesystem
 */

#pragma once
#include <set>
#include <vector>
#include "PresetData.hpp"
#include "util/Result.hpp"

namespace vc {

class PresetScanner {
public:
    static Result<void> scan(const fs::path& directory,
                             bool recursive,
                             std::vector<PresetInfo>& presets,
                             const std::set<std::string>& favoriteNames,
                             const std::set<std::string>& blacklistedNames);

    static void parsePresetInfo(PresetInfo& info);
};

} // namespace vc
