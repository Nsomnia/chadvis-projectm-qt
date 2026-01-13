/**
 * @file ConfigLoader.hpp
 * @brief Configuration file I/O.
 *
 * This file defines the ConfigLoader class which handles reading and writing
 * configuration files from/to disk. It ensures atomic writes to prevent
 * corruption.
 *
 * @section Dependencies
 * - Config
 * - std::filesystem
 */

#pragma once
#include <filesystem>
#include "util/Result.hpp"

namespace vc {

class Config;

class ConfigLoader {
public:
    static Result<void> load(Config& config, const std::filesystem::path& path);
    static Result<void> save(const Config& config,
                             const std::filesystem::path& path);
    static Result<void> loadDefault(Config& config);
};

} // namespace vc
