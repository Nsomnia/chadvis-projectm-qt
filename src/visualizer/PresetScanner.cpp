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
    // Hoisted out of the scan loop on purpose. Constructing a std::regex parses
    // and compiles the ECMAScript grammar and allocates; doing that once per
    // file made it the dominant CPU cost of a scan after the stat() storm. A
    // function-local `static const` is initialized exactly once (thread-safe
    // since C++11, [basic.start.static]/2) and is only ever handed to const
    // member functions afterwards, which are safe to call concurrently
    // ([re.alg.match]). Behavior is unchanged: same pattern, same flags, same
    // match_results.
    static const std::regex authorPattern(R"(^(.+?)\s*-\s*(.+)$)");

    // Provably equivalent short-circuit. Equivalence argument: the pattern
    // requires the literal character '-' at some position, and no part of the
    // pattern can consume or synthesize one — `.` matches any character *except*
    // '\n' and the character classes are exactly \s and the literal. Therefore
    // `regex_match` succeeds only if '-' occurs in the subject, and a name
    // without '-' cannot match. Skipping the engine in that case leaves
    // info.author exactly where a failed match left it: untouched.
    if (info.name.find('-') == std::string::npos)
        return;

    std::smatch match;
    if (std::regex_match(info.name, match, authorPattern)) {
        info.author = match[1].str();
    }
}

} // namespace vc
