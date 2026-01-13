/**
 * @file PresetPersistence.hpp
 * @brief Save/load preset state.
 *
 * This file defines the PresetPersistence class which handles saving and
 * loading user preferences for presets (favorites, blacklist) to/from disk.
 *
 * @section Dependencies
 * - PresetData
 * - std::fstream
 */

#pragma once
#include <set>
#include <vector>
#include "PresetData.hpp"
#include "util/Result.hpp"

namespace vc {

class PresetPersistence {
public:
    static Result<void> loadState(const fs::path& path,
                                  std::set<std::string>& favoriteNames,
                                  std::set<std::string>& blacklistedNames,
                                  std::vector<PresetInfo>& presets);

    static Result<void> saveState(
            const fs::path& path,
            const std::set<std::string>& favoriteNames,
            const std::set<std::string>& blacklistedNames);
};

} // namespace vc
