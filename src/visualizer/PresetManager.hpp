/**
 * @file PresetManager.hpp
 * @brief ProjectM preset management.
 *
 * This file defines the PresetManager class which provides the public API
 * for browsing, searching, and selecting presets. It delegates scanning
 * to PresetScanner and persistence to PresetPersistence.
 *
 * @section Dependencies
 * - PresetData
 * - PresetScanner
 * - PresetPersistence
 *
 * @section Patterns
 * - Manager: Central point of control for preset logic.
 */

#pragma once
#include <random>
#include <set>
#include <vector>
#include "PresetData.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

namespace vc {

class PresetManager {
public:
    PresetManager();

    // Scanning
    Result<void> scan(const fs::path& directory, bool recursive = true);
    void rescan();
    void clear();

    // Access
    usize count() const {
        return presets_.size();
    }
    usize activeCount() const;
    bool empty() const {
        return presets_.empty();
    }

    const std::vector<PresetInfo>& allPresets() const {
        return presets_;
    }
    std::vector<const PresetInfo*> activePresets() const;
    std::vector<const PresetInfo*> favoritePresets() const;
    std::vector<const PresetInfo*> blacklistedPresets() const;
    std::vector<std::string> categories() const;

    // Selection
    const PresetInfo* current() const;
    usize currentIndex() const {
        return currentIndex_;
    }

    bool selectByIndex(usize index);
    bool selectByName(const std::string& name);
    bool selectByPath(const fs::path& path);
    bool selectRandom();
    bool selectNext();
    bool selectPrevious();

    // Pending preset
    void setPendingPreset(const std::string& name) {
        pendingPresetName_ = name;
    }
    const std::string& pendingPreset() const {
        return pendingPresetName_;
    }
    void clearPendingPreset() {
        pendingPresetName_.clear();
    }

    // Favorites & Blacklist
    void setFavorite(usize index, bool favorite);
    void setBlacklisted(usize index, bool blacklisted);
    void toggleFavorite(usize index);
    void toggleBlacklisted(usize index);

    // Search
    std::vector<const PresetInfo*> search(const std::string& query) const;
    std::vector<const PresetInfo*> byCategory(
            const std::string& category) const;

    // Persistence
    Result<void> loadState(const fs::path& path);
    Result<void> saveState(const fs::path& path) const;

    // Signals
    Signal<const PresetInfo*> presetChanged;
    Signal<> listChanged;

private:
    std::vector<PresetInfo> presets_;
    usize currentIndex_{0};
    fs::path scanDirectory_;

    std::vector<usize> history_;
    usize historyPosition_{0};

    std::set<std::string> favoriteNames_;
    std::set<std::string> blacklistedNames_;
    std::string pendingPresetName_;

    std::mt19937 rng_{std::random_device{}()};
};

} // namespace vc
