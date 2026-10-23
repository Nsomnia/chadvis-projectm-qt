#include "PresetPersistence.hpp"
#include "util/FileUtils.hpp"
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
    // Build the whole file, then let the shared writer own the temp+rename.
    // saveState runs on every favourite toggle, so a crash mid-write used to
    // truncate the user's favourites and blacklist (T0059).
    std::string content = "[favorites]\n";
    for (const auto& name : favoriteNames) {
        content += name;
        content += '\n';
    }
    content += "\n[blacklist]\n";
    for (const auto& name : blacklistedNames) {
        content += name;
        content += '\n';
    }

    return file::writeText(path, content);
}

} // namespace vc
