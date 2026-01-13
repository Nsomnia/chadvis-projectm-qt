#include "PresetManager.hpp"
#include <algorithm>
#include "PresetPersistence.hpp"
#include "PresetScanner.hpp"
#include "core/Logger.hpp"

namespace vc {

PresetManager::PresetManager() = default;

Result<void> PresetManager::scan(const fs::path& directory, bool recursive) {
    scanDirectory_ = directory;
    presets_.clear();

    auto res = PresetScanner::scan(
            directory, recursive, presets_, favoriteNames_, blacklistedNames_);
    if (!res)
        return res;

    if (!pendingPresetName_.empty()) {
        if (selectByName(pendingPresetName_))
            pendingPresetName_.clear();
    }

    listChanged.emitSignal();
    return Result<void>::ok();
}

void PresetManager::rescan() {
    if (!scanDirectory_.empty())
        scan(scanDirectory_);
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
    for (const auto& p : presets_)
        if (!p.blacklisted)
            result.push_back(&p);
    return result;
}

std::vector<const PresetInfo*> PresetManager::favoritePresets() const {
    std::vector<const PresetInfo*> result;
    for (const auto& p : presets_)
        if (p.favorite && !p.blacklisted)
            result.push_back(&p);
    return result;
}

std::vector<const PresetInfo*> PresetManager::blacklistedPresets() const {
    std::vector<const PresetInfo*> result;
    for (const auto& p : presets_)
        if (p.blacklisted)
            result.push_back(&p);
    return result;
}

std::vector<std::string> PresetManager::categories() const {
    std::set<std::string> cats;
    for (const auto& p : presets_)
        cats.insert(p.category);
    return {cats.begin(), cats.end()};
}

const PresetInfo* PresetManager::current() const {
    if (currentIndex_ >= presets_.size())
        return nullptr;
    return &presets_[currentIndex_];
}

bool PresetManager::selectByIndex(usize index) {
    if (index >= presets_.size() || presets_[index].blacklisted)
        return false;

    if (history_.empty() || history_[historyPosition_] != index) {
        if (!history_.empty() && historyPosition_ < history_.size() - 1) {
            history_.erase(history_.begin() + historyPosition_ + 1,
                           history_.end());
        }
        history_.push_back(index);
        historyPosition_ = history_.size() - 1;
        if (history_.size() > 100) {
            history_.erase(history_.begin());
            historyPosition_--;
        }
    }

    currentIndex_ = index;
    presets_[currentIndex_].playCount++;
    presetChanged.emitSignal(&presets_[currentIndex_]);
    return true;
}

bool PresetManager::selectByName(const std::string& name) {
    if (presets_.empty()) {
        pendingPresetName_ = name;
        return false;
    }

    for (usize i = 0; i < presets_.size(); ++i) {
        if (presets_[i].name == name && !presets_[i].blacklisted)
            return selectByIndex(i);
    }

    for (usize i = 0; i < presets_.size(); ++i) {
        if (!presets_[i].blacklisted &&
            presets_[i].name.find(name) != std::string::npos)
            return selectByIndex(i);
    }

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
        if (lowerPreset.find(lowerName) != std::string::npos)
            return selectByIndex(i);
    }

    return false;
}

bool PresetManager::selectByPath(const fs::path& path) {
    for (usize i = 0; i < presets_.size(); ++i) {
        if (presets_[i].path == path && !presets_[i].blacklisted)
            return selectByIndex(i);
    }
    return false;
}

bool PresetManager::selectRandom() {
    auto active = activePresets();
    if (active.empty())
        return false;
    std::uniform_int_distribution<usize> dist(0, active.size() - 1);
    const auto* preset = active[dist(rng_)];
    for (usize i = 0; i < presets_.size(); ++i)
        if (&presets_[i] == preset)
            return selectByIndex(i);
    return false;
}

bool PresetManager::selectNext() {
    if (presets_.empty())
        return false;
    if (!history_.empty() && historyPosition_ < history_.size() - 1) {
        historyPosition_++;
        currentIndex_ = history_[historyPosition_];
        presets_[currentIndex_].playCount++;
        presetChanged.emitSignal(&presets_[currentIndex_]);
        return true;
    }

    std::string currentName = current() ? current()->name : "";
    usize start = currentIndex_;
    usize nextIndex = currentIndex_;
    do {
        nextIndex = (nextIndex + 1) % presets_.size();
        if (!presets_[nextIndex].blacklisted) {
            if (!currentName.empty() && presets_[nextIndex].name == currentName)
                continue;
            return selectByIndex(nextIndex);
        }
    } while (nextIndex != start);
    return false;
}

bool PresetManager::selectPrevious() {
    if (presets_.empty())
        return false;
    if (!history_.empty() && historyPosition_ > 0) {
        historyPosition_--;
        currentIndex_ = history_[historyPosition_];
        presets_[currentIndex_].playCount++;
        presetChanged.emitSignal(&presets_[currentIndex_]);
        return true;
    }

    std::string currentName = current() ? current()->name : "";
    usize start = currentIndex_;
    usize prevIndex = currentIndex_;
    do {
        prevIndex = (prevIndex == 0) ? presets_.size() - 1 : prevIndex - 1;
        if (!presets_[prevIndex].blacklisted) {
            if (!currentName.empty() && presets_[prevIndex].name == currentName)
                continue;
            return selectByIndex(prevIndex);
        }
    } while (prevIndex != start);
    return false;
}

void PresetManager::setFavorite(usize index, bool favorite) {
    if (index >= presets_.size())
        return;
    presets_[index].favorite = favorite;
    if (favorite)
        favoriteNames_.insert(presets_[index].name);
    else
        favoriteNames_.erase(presets_[index].name);
    listChanged.emitSignal();
}

void PresetManager::setBlacklisted(usize index, bool blacklisted) {
    if (index >= presets_.size())
        return;
    presets_[index].blacklisted = blacklisted;
    if (blacklisted)
        blacklistedNames_.insert(presets_[index].name);
    else
        blacklistedNames_.erase(presets_[index].name);
    listChanged.emitSignal();
}

void PresetManager::toggleFavorite(usize index) {
    if (index < presets_.size())
        setFavorite(index, !presets_[index].favorite);
}
void PresetManager::toggleBlacklisted(usize index) {
    if (index < presets_.size())
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
        if (lowerName.find(lowerQuery) != std::string::npos)
            result.push_back(&p);
    }
    return result;
}

std::vector<const PresetInfo*> PresetManager::byCategory(
        const std::string& category) const {
    std::vector<const PresetInfo*> result;
    for (const auto& p : presets_)
        if (p.category == category && !p.blacklisted)
            result.push_back(&p);
    return result;
}

Result<void> PresetManager::loadState(const fs::path& path) {
    return PresetPersistence::loadState(
            path, favoriteNames_, blacklistedNames_, presets_);
}

Result<void> PresetManager::saveState(const fs::path& path) const {
    return PresetPersistence::saveState(
            path, favoriteNames_, blacklistedNames_);
}

} // namespace vc
