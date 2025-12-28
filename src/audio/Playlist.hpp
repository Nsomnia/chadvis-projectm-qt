#pragma once
// Playlist.hpp - Track queue management
// Because shuffle algorithms are surprisingly controversial

#include "util/Types.hpp"
#include "util/Signal.hpp"
#include "MediaMetadata.hpp"
#include <vector>
#include <random>
#include <optional>

namespace vc {

struct PlaylistItem {
    fs::path path;
    MediaMetadata metadata;
    bool valid{true};
};

enum class RepeatMode {
    Off,
    One,
    All
};

class Playlist {
public:
    Playlist();
    
    // Modification
    void addFile(const fs::path& path);
    void addFiles(const std::vector<fs::path>& paths);
    void removeAt(usize index);
    void clear();
    void move(usize from, usize to);
    
    // Navigation
    std::optional<usize> currentIndex() const { return currentIndex_; }
    const PlaylistItem* currentItem() const;
    const PlaylistItem* itemAt(usize index) const;
    
    bool next();
    bool previous();
    bool jumpTo(usize index);
    
    // Playback modes
    bool shuffle() const { return shuffle_; }
    void setShuffle(bool enabled);
    
    RepeatMode repeatMode() const { return repeatMode_; }
    void setRepeatMode(RepeatMode mode);
    void cycleRepeatMode();
    
    // Queries
    usize size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const std::vector<PlaylistItem>& items() const { return items_; }
    
    // Persistence
    Result<void> saveM3U(const fs::path& path) const;
    Result<void> loadM3U(const fs::path& path);
    
    // Signals
    Signal<> changed;
    Signal<usize> currentChanged;
    Signal<usize> itemAdded;
    Signal<usize> itemRemoved;
    
private:
    void regenerateShuffleOrder();
    usize shuffleIndexToReal(usize shuffleIdx) const;
    usize realIndexToShuffle(usize realIdx) const;
    
    std::vector<PlaylistItem> items_;
    std::optional<usize> currentIndex_;
    
    bool shuffle_{false};
    std::vector<usize> shuffleOrder_;
    usize shufflePosition_{0};
    
    RepeatMode repeatMode_{RepeatMode::Off};
    std::mt19937 rng_;
};

} // namespace vc
