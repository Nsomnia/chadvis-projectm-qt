#include "PresetScanner.hpp"
#include <algorithm>
#include <regex>
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

Result<void> PresetScanner::scan(
        const fs::path& directory,
        bool recursive,
        std::vector<PresetInfo>& presets,
        const std::set<std::string>& favoriteNames,
        const std::set<std::string>& blacklistedNames) {
    if (!fs::exists(directory)) {
        return Result<void>::err("Preset directory does not exist: " +
                                 directory.string());
    }

    LOG_INFO("PresetScanner: Scanning directory '{}' (recursive={})",
             directory.string(),
             recursive);

    auto files = file::listFiles(directory, file::presetExtensions, recursive);
    LOG_INFO("PresetScanner: Found {} potential preset files", files.size());

    for (const auto& path : files) {
        PresetInfo info;
        info.path = path;
        info.name = path.stem().string();

        auto rel = fs::relative(path.parent_path(), directory);
        info.category = rel.string();
        if (info.category == ".")
            info.category = "Uncategorized";

        parsePresetInfo(info);

        if (favoriteNames.contains(info.name))
            info.favorite = true;
        if (blacklistedNames.contains(info.name))
            info.blacklisted = true;

        presets.push_back(std::move(info));
    }

    std::sort(presets.begin(), presets.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    return Result<void>::ok();
}

void PresetScanner::parsePresetInfo(PresetInfo& info) {
    std::regex authorPattern(R"(^(.+?)\s*-\s*(.+)$)");
    std::smatch match;
    if (std::regex_match(info.name, match, authorPattern)) {
        info.author = match[1].str();
    }
}

} // namespace vc
