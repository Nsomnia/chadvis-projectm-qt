#pragma once
// PresetManager.hpp - ProjectM preset management
// Because manually browsing .milk files is for peasants

#include "util/Types.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include <vector>
#include <set>
#include <random>

namespace vc {

struct PresetInfo {
    fs::path path;
    std::string name;
    std::string author;
    std::string category;  // Parent folder name
    bool favorite{false};
    bool blacklisted{false};
    u32 playCount{0};
};

class PresetManager {
public:
    PresetManager();
    
    // Scanning
    Result<void> scan(const fs::path& directory, bool recursive = true);
    void rescan();
    void clear();
    
    // Access
    usize count() const { return presets_.size(); }
    usize activeCount() const;  // Excludes blacklisted
    bool empty() const { return presets_.empty(); }
    
    const std::vector<PresetInfo>& allPresets() const { return presets_; }
    std::vector<const PresetInfo*> activePresets() const;
    std::vector<const PresetInfo*> favoritePresets() const;
    std::vector<std::string> categories() const;
    
    // Selection
    const PresetInfo* current() const;
    usize currentIndex() const { return currentIndex_; }
    
    bool selectByIndex(usize index);
    bool selectByName(const std::string& name);
    bool selectByPath(const fs::path& path);
    bool selectRandom();
    bool selectNext();
    bool selectPrevious();
    
    // Pending preset (for command-line args before scanning)
    void setPendingPreset(const std::string& name) { pendingPresetName_ = name; }
    const std::string& pendingPreset() const { return pendingPresetName_; }
    void clearPendingPreset() { pendingPresetName_.clear(); }
    
    // Favorites & Blacklist
    void setFavorite(usize index, bool favorite);
    void setBlacklisted(usize index, bool blacklisted);
    void toggleFavorite(usize index);
    void toggleBlacklisted(usize index);
    
    // Search
    std::vector<const PresetInfo*> search(const std::string& query) const;
    std::vector<const PresetInfo*> byCategory(const std::string& category) const;
    
    // Persistence
    Result<void> loadState(const fs::path& path);
    Result<void> saveState(const fs::path& path) const;
    
    // Signals
    Signal<const PresetInfo*> presetChanged;
    Signal<> listChanged;
    
private:
    void parsePresetInfo(PresetInfo& info);
    
    std::vector<PresetInfo> presets_;
    usize currentIndex_{0};
    fs::path scanDirectory_;
    
    std::set<std::string> favoriteNames_;
    std::set<std::string> blacklistedNames_;
    std::string pendingPresetName_;  // Preset requested before scanning
    
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace vc