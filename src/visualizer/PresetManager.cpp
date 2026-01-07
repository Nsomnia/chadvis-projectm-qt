#include "PresetManager.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

namespace vc {

PresetManager::PresetManager() = default;

Result<void> PresetManager::scan(const fs::path& directory, bool recursive) {
    if (!fs::exists(directory)) {
        return Result<void>::err("Preset directory does not exist: " +
                                 directory.string());
    }

    scanDirectory_ = directory;
    presets_.clear();

    auto files = file::listFiles(directory, file::presetExtensions, recursive);

    for (const auto& path : files) {
        PresetInfo info;
        info.path = path;
        info.name = path.stem().string();

        // Category is parent folder relative to scan directory
        auto rel = fs::relative(path.parent_path(), directory);
        info.category = rel.string();
        if (info.category == ".")
            info.category = "Uncategorized";

        // Parse preset file for author info
        parsePresetInfo(info);

        // Apply saved state
        if (favoriteNames_.contains(info.name)) {
            info.favorite = true;
        }
        if (blacklistedNames_.contains(info.name)) {
            info.blacklisted = true;
        }

        presets_.push_back(std::move(info));
    }

    // Sort by name
    std::sort(presets_.begin(),
              presets_.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    LOG_INFO("Scanned {} presets from {}", presets_.size(), directory.string());

    // Apply pending preset if one was requested before scanning
    if (!pendingPresetName_.empty()) {
        LOG_INFO("Applying pending preset request: '{}'", pendingPresetName_);
        if (selectByName(pendingPresetName_)) {
            LOG_INFO("Successfully selected pending preset: {}",
                     pendingPresetName_);
        } else {
            LOG_WARN("Pending preset '{}' not found after scanning",
                     pendingPresetName_);
        }
        pendingPresetName_.clear();
    }

    listChanged.emitSignal();

    return Result<void>::ok();
}

void PresetManager::rescan() {
    if (!scanDirectory_.empty()) {
        scan(scanDirectory_);
    }
}

void PresetManager::clear() {
    presets_.clear();
    currentIndex_ = 0;
    listChanged.emitSignal();
}

usize PresetManager::activeCount() const {
    return std::count_if(presets_.begin(), presets_.end(), [](const auto& p) {
        return !p.blacklisted;
    });
}

std::vector<const PresetInfo*> PresetManager::activePresets() const {
    std::vector<const PresetInfo*> result;
    for (const auto& p : presets_) {
        if (!p.blacklisted) {
            result.push_back(&p);
        }
    }
    return result;
}

std::vector<const PresetInfo*> PresetManager::favoritePresets() const {
    std::vector<const PresetInfo*> result;
    for (const auto& p : presets_) {
        if (p.favorite && !p.blacklisted) {
            result.push_back(&p);
        }
    }
    return result;
}

std::vector<std::string> PresetManager::categories() const {
    std::set<std::string> cats;
    for (const auto& p : presets_) {
        cats.insert(p.category);
    }
    return {cats.begin(), cats.end()};
}

const PresetInfo* PresetManager::current() const {
    if (currentIndex_ >= presets_.size())
        return nullptr;
    return &presets_[currentIndex_];
}

bool PresetManager::selectByIndex(usize index) {
    if (index >= presets_.size())
        return false;
    if (presets_[index].blacklisted)
        return false;

    // Add to history if it's not the same as current
    if (history_.empty() || history_[historyPosition_] != index) {
        // If we were in the middle of history, clear forward history
        if (!history_.empty() && historyPosition_ < history_.size() - 1) {
            history_.erase(history_.begin() + historyPosition_ + 1,
                           history_.end());
        }

        history_.push_back(index);
        historyPosition_ = history_.size() - 1;

        // Limit history size
        if (history_.size() > 100) {
            history_.erase(history_.begin());
            historyPosition_--;
        }
    }

    currentIndex_ = index;
    presets_[currentIndex_].playCount++;
    presetChanged.emitSignal(&presets_[currentIndex_]);

    LOG_DEBUG("Selected preset: {}", presets_[currentIndex_].name);
    return true;
}

bool PresetManager::selectByName(const std::string& name) {
    LOG_INFO("PresetManager::selectByName: searching for '{}'", name);
    LOG_INFO("  Total presets: {}", presets_.size());

    // If no presets loaded yet, store as pending
    if (presets_.empty()) {
        LOG_INFO("  No presets loaded yet, storing as pending preset");
        pendingPresetName_ = name;
        return false;
    }

    // First try exact match
    for (usize i = 0; i < presets_.size(); ++i) {
        LOG_DEBUG("  Checking preset {}: '{}'", i, presets_[i].name);
        if (presets_[i].name == name && !presets_[i].blacklisted) {
            LOG_INFO("  FOUND at index {}, calling selectByIndex", i);
            return selectByIndex(i);
        }
    }

    // If no exact match, try partial match (contains)
    LOG_INFO("  No exact match, trying partial match");
    for (usize i = 0; i < presets_.size(); ++i) {
        if (presets_[i].blacklisted)
            continue;

        // Check if preset name contains the search string
        if (presets_[i].name.find(name) != std::string::npos) {
            LOG_INFO("  FOUND partial match at index {}: '{}'",
                     i,
                     presets_[i].name);
            return selectByIndex(i);
        }
    }

    // If still no match, try case-insensitive partial match
    LOG_INFO("  No partial match, trying case-insensitive");
    std::string lowerName = name;
    std::transform(
            lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    for (usize i = 0; i < presets_.size(); ++i) {
        if (presets_[i].blacklisted)
            continue;

        std::string lowerPreset = presets_[i].name;
        std::transform(lowerPreset.begin(),
                       lowerPreset.end(),
                       lowerPreset.begin(),
                       ::tolower);

        if (lowerPreset.find(lowerName) != std::string::npos) {
            LOG_INFO("  FOUND case-insensitive match at index {}: '{}'",
                     i,
                     presets_[i].name);
            return selectByIndex(i);
        }
    }

    LOG_WARN("  PRESET NOT FOUND: '{}'", name);
    return false;
}

bool PresetManager::selectByPath(const fs::path& path) {
    LOG_DEBUG("PresetManager::selectByPath: {}", path.string());
    for (usize i = 0; i < presets_.size(); ++i) {
        if (presets_[i].path == path && !presets_[i].blacklisted) {
            LOG_DEBUG(
                    "PresetManager: Found preset at index {}, calling "
                    "selectByIndex",
                    i);
            return selectByIndex(i);
        }
    }
    LOG_WARN("PresetManager: Could not find preset by path: {}", path.string());
    return false;
}

bool PresetManager::selectRandom() {
    auto active = activePresets();
    if (active.empty())
        return false;

    std::uniform_int_distribution<usize> dist(0, active.size() - 1);
    const auto* preset = active[dist(rng_)];

    // Find index in main list
    for (usize i = 0; i < presets_.size(); ++i) {
        if (&presets_[i] == preset) {
            return selectByIndex(i);
        }
    }
    return false;
}

bool PresetManager::selectNext() {
    LOG_DEBUG("PresetManager::selectNext() called, current index: {}",
              currentIndex_);
    if (presets_.empty()) {
        LOG_WARN("PresetManager: No presets available");
        return false;
    }

    // If we have forward history, use it
    if (!history_.empty() && historyPosition_ < history_.size() - 1) {
        historyPosition_++;
        currentIndex_ = history_[historyPosition_];
        presets_[currentIndex_].playCount++;
        presetChanged.emitSignal(&presets_[currentIndex_]);
        return true;
    }

    // Otherwise pick next or random
    if (history_.empty()) {
        return selectByIndex(0);
    }

    // Get current preset name to skip duplicates
    std::string currentName = presets_[currentIndex_].name;

    usize start = currentIndex_;
    do {
        currentIndex_ = (currentIndex_ + 1) % presets_.size();
        if (!presets_[currentIndex_].blacklisted) {
            // Skip presets with the same name as current
            if (presets_[currentIndex_].name == currentName) {
                continue;
            }
            return selectByIndex(currentIndex_);
        }
    } while (currentIndex_ != start);

    LOG_WARN("PresetManager: All presets are blacklisted or have same name");
    return false;
}

bool PresetManager::selectPrevious() {
    if (presets_.empty())
        return false;

    // If we have history, go back
    if (!history_.empty() && historyPosition_ > 0) {
        historyPosition_--;
        currentIndex_ = history_[historyPosition_];
        presets_[currentIndex_].playCount++;
        presetChanged.emitSignal(&presets_[currentIndex_]);
        return true;
    }

    // Otherwise standard logic
    // Get current preset name to skip duplicates
    std::string currentName = presets_[currentIndex_].name;

    usize start = currentIndex_;
    do {
        currentIndex_ =
                (currentIndex_ == 0) ? presets_.size() - 1 : currentIndex_ - 1;
        if (!presets_[currentIndex_].blacklisted) {
            // Skip presets with the same name as current
            if (presets_[currentIndex_].name == currentName) {
                continue;
            }
            return selectByIndex(currentIndex_);
        }
    } while (currentIndex_ != start);

    return false;
}

void PresetManager::setFavorite(usize index, bool favorite) {
    if (index >= presets_.size())
        return;

    presets_[index].favorite = favorite;
    if (favorite) {
        favoriteNames_.insert(presets_[index].name);
    } else {
        favoriteNames_.erase(presets_[index].name);
    }
    listChanged.emitSignal();
}

void PresetManager::setBlacklisted(usize index, bool blacklisted) {
    if (index >= presets_.size())
        return;

    presets_[index].blacklisted = blacklisted;
    if (blacklisted) {
        blacklistedNames_.insert(presets_[index].name);
    } else {
        blacklistedNames_.erase(presets_[index].name);
    }
    listChanged.emitSignal();
}

void PresetManager::toggleFavorite(usize index) {
    if (index >= presets_.size())
        return;
    setFavorite(index, !presets_[index].favorite);
}

void PresetManager::toggleBlacklisted(usize index) {
    if (index >= presets_.size())
        return;
    setBlacklisted(index, !presets_[index].blacklisted);
}

std::vector<const PresetInfo*> PresetManager::search(
        const std::string& query) const {
    std::vector<const PresetInfo*> result;

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(),
                   lowerQuery.end(),
                   lowerQuery.begin(),
                   ::tolower);

    for (const auto& p : presets_) {
        std::string lowerName = p.name;
        std::transform(lowerName.begin(),
                       lowerName.end(),
                       lowerName.begin(),
                       ::tolower);

        if (lowerName.find(lowerQuery) != std::string::npos) {
            result.push_back(&p);
        }
    }

    return result;
}

std::vector<const PresetInfo*> PresetManager::byCategory(
        const std::string& category) const {
    std::vector<const PresetInfo*> result;
    for (const auto& p : presets_) {
        if (p.category == category && !p.blacklisted) {
            result.push_back(&p);
        }
    }
    return result;
}

void PresetManager::parsePresetInfo(PresetInfo& info) {
    // Try to extract author from filename pattern "Author - Name"
    std::regex authorPattern(R"(^(.+?)\s*-\s*(.+)$)");
    std::smatch match;

    if (std::regex_match(info.name, match, authorPattern)) {
        info.author = match[1].str();
        // Keep full name for display
    }

    // Could also parse the actual preset file here for metadata
    // but most presets don't have standardized metadata
}

Result<void> PresetManager::loadState(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return Result<void>::ok(); // File doesn't exist, that's fine
    }

    std::string line;
    std::string section;

    while (std::getline(file, line)) {
        if (line.empty())
            continue;

        if (line == "[favorites]") {
            section = "favorites";
        } else if (line == "[blacklist]") {
            section = "blacklist";
        } else if (section == "favorites") {
            favoriteNames_.insert(line);
        } else if (section == "blacklist") {
            blacklistedNames_.insert(line);
        }
    }

    // Apply to loaded presets
    for (auto& p : presets_) {
        p.favorite = favoriteNames_.contains(p.name);
        p.blacklisted = blacklistedNames_.contains(p.name);
    }

    return Result<void>::ok();
}

Result<void> PresetManager::saveState(const fs::path& path) const {
    std::ofstream file(path);
    if (!file) {
        return Result<void>::err("Failed to open file for writing");
    }

    file << "[favorites]\n";
    for (const auto& name : favoriteNames_) {
        file << name << "\n";
    }

    file << "\n[blacklist]\n";
    for (const auto& name : blacklistedNames_) {
        file << name << "\n";
    }

    return Result<void>::ok();
}

} // namespace vc
