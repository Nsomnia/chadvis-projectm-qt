#include "PresetPersistence.hpp"
#include <fstream>

namespace vc {

Result<void> PresetPersistence::loadState(
        const fs::path& path,
        std::set<std::string>& favoriteNames,
        std::set<std::string>& blacklistedNames,
        std::vector<PresetInfo>& presets) {
    std::ifstream file(path);
    if (!file)
        return Result<void>::ok();

    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        if (line == "[favorites]")
            section = "favorites";
        else if (line == "[blacklist]")
            section = "blacklist";
        else if (section == "favorites")
            favoriteNames.insert(line);
        else if (section == "blacklist")
            blacklistedNames.insert(line);
    }

    for (auto& p : presets) {
        p.favorite = favoriteNames.contains(p.name);
        p.blacklisted = blacklistedNames.contains(p.name);
    }

    return Result<void>::ok();
}

Result<void> PresetPersistence::saveState(
        const fs::path& path,
        const std::set<std::string>& favoriteNames,
        const std::set<std::string>& blacklistedNames) {
    std::ofstream file(path);
    if (!file)
        return Result<void>::err("Failed to open file for writing");

    file << "[favorites]\n";
    for (const auto& name : favoriteNames)
        file << name << "\n";
    file << "\n[blacklist]\n";
    for (const auto& name : blacklistedNames)
        file << name << "\n";

    return Result<void>::ok();
}

} // namespace vc
