#pragma once
// PresetData.hpp - Preset data structures

#include <filesystem>
#include <string>
#include "util/Types.hpp"

namespace vc {

namespace fs = std::filesystem;

struct PresetInfo {
    fs::path path;
    std::string name;
    std::string author;
    std::string category; // Parent folder name
    bool favorite{false};
    bool blacklisted{false};
    u32 playCount{0};
};

} // namespace vc
