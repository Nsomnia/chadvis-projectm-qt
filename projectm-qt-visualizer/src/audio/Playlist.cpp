#include "Playlist.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include <algorithm>
#include <fstream>
#include <numeric>

namespace vc {

Playlist::Playlist()
    : rng_(std::random_device{}())
{
}

void Playlist::addFile(const fs::path& path) {
    if (!fs::exists(path)) {
        LOG_WARN("File not found: {}", path.string());
        return;
    }
    
    if (!MetadataReader::canRead(path)) {
        LOG_WARN("Unsupported file format: {}", path.string());
        return;
    }
    
    PlaylistItem item;
    item.path = path;
    
    auto metaResult = MetadataReader::read(path);
    if (metaResult) {
        item.metadata = std::move(*metaResult);
    } else {
        LOG_WARN("Failed to read metadata: {}", metaResult.error().message);
        item.metadata.title = path.stem().string();
    }
    
    usize index = items_.size();
    items_.push_back(std::move(item));
    
    if (shuffle_) {
        shuffleOrder_.push_back(index);
        if (shuffleOrder_.size() > 1) {
            std::uniform_int_distribution<usize> dist(0, shuffleOrder_.size() - 1);
            std::swap(shuffleOrder_.back(), shuffleOrder_[dist(rng_)]);
        }
    }
    
    itemAdded.emitSignal(index);
    changed.emitSignal();
    
    LOG_DEBUG("Added to playlist: {}", path.filename().string());
}

void Playlist::addFiles(const std::vector<fs::path>& paths) {
    for (const auto& path : paths) {
        addFile(path);
    }
}

void Playlist::removeAt(usize index) {
    if (index >= items_.size()) return;
    
    items_.erase(items_.begin() + index);
    
    if (currentIndex_) {
        if (*currentIndex_ == index) {
            currentIndex_ = std::nullopt;
        } else if (*currentIndex_ > index) {
            --(*currentIndex_);
        }
    }
    
    if (shuffle_) {
        regenerateShuffleOrder();
    }
    
    itemRemoved.emitSignal(index);
    changed.emitSignal();
}

void Playlist::clear() {
    items_.clear();
    currentIndex_ = std::nullopt;
    shuffleOrder_.clear();
    shufflePosition_ = 0;
    
    changed.emitSignal();
}

void Playlist::move(usize from, usize to) {
    if (from >= items_.size() || to >= items_.size() || from == to) return;
    
    auto item = std::move(items_[from]);
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, std::move(item));
    
    if (currentIndex_) {
        if (*currentIndex_ == from) {
            *currentIndex_ = to;
        } else if (from < *currentIndex_ && to >= *currentIndex_) {
            --(*currentIndex_);
        } else if (from > *currentIndex_ && to <= *currentIndex_) {
            ++(*currentIndex_);
        }
    }
    
    if (shuffle_) {
        regenerateShuffleOrder();
    }
    
    changed.emitSignal();
}

const PlaylistItem* Playlist::currentItem() const {
    if (!currentIndex_ || *currentIndex_ >= items_.size()) {
        return nullptr;
    }
    return &items_[*currentIndex_];
}

const PlaylistItem* Playlist::itemAt(usize index) const {
    if (index >= items_.size()) return nullptr;
    return &items_[index];
}

bool Playlist::next() {
    if (items_.empty()) return false;
    
    if (repeatMode_ == RepeatMode::One && currentIndex_) {
        currentChanged.emitSignal(*currentIndex_);
        return true;
    }
    
    if (shuffle_) {
        ++shufflePosition_;
        if (shufflePosition_ >= shuffleOrder_.size()) {
            if (repeatMode_ == RepeatMode::All) {
                shufflePosition_ = 0;
                regenerateShuffleOrder();
            } else {
                return false;
            }
        }
        currentIndex_ = shuffleOrder_[shufflePosition_];
    } else {
        if (!currentIndex_) {
            currentIndex_ = 0;
        } else {
            ++(*currentIndex_);
            if (*currentIndex_ >= items_.size()) {
                if (repeatMode_ == RepeatMode::All) {
                    currentIndex_ = 0;
                } else {
                    currentIndex_ = std::nullopt;
                    return false;
                }
            }
        }
    }
    
    currentChanged.emitSignal(*currentIndex_);
    return true;
}

bool Playlist::previous() {
    if (items_.empty()) return false;
    
    if (shuffle_) {
        if (shufflePosition_ == 0) {
            if (repeatMode_ == RepeatMode::All) {
                shufflePosition_ = shuffleOrder_.size() - 1;
            } else {
                return false;
            }
        } else {
            --shufflePosition_;
        }
        currentIndex_ = shuffleOrder_[shufflePosition_];
    } else {
        if (!currentIndex_ || *currentIndex_ == 0) {
            if (repeatMode_ == RepeatMode::All) {
                currentIndex_ = items_.size() - 1;
            } else {
                return false;
            }
        } else {
            --(*currentIndex_);
        }
    }
    
    currentChanged.emitSignal(*currentIndex_);
    return true;
}

bool Playlist::jumpTo(usize index) {
    if (index >= items_.size()) return false;
    
    currentIndex_ = index;
    
    if (shuffle_) {
        auto it = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), index);
        if (it != shuffleOrder_.end()) {
            shufflePosition_ = std::distance(shuffleOrder_.begin(), it);
        }
    }
    
    currentChanged.emitSignal(*currentIndex_);
    return true;
}

void Playlist::setShuffle(bool enabled) {
    if (shuffle_ == enabled) return;
    
    shuffle_ = enabled;
    
    if (shuffle_) {
        regenerateShuffleOrder();
        if (currentIndex_) {
            shufflePosition_ = realIndexToShuffle(*currentIndex_);
        }
    }
    
    changed.emitSignal();
}

void Playlist::setRepeatMode(RepeatMode mode) {
    repeatMode_ = mode;
    changed.emitSignal();
}

void Playlist::cycleRepeatMode() {
    switch (repeatMode_) {
        case RepeatMode::Off: repeatMode_ = RepeatMode::All; break;
        case RepeatMode::All: repeatMode_ = RepeatMode::One; break;
        case RepeatMode::One: repeatMode_ = RepeatMode::Off; break;
    }
    changed.emitSignal();
}

void Playlist::regenerateShuffleOrder() {
    shuffleOrder_.resize(items_.size());
    std::iota(shuffleOrder_.begin(), shuffleOrder_.end(), 0);
    std::shuffle(shuffleOrder_.begin(), shuffleOrder_.end(), rng_);
    shufflePosition_ = 0;
}

usize Playlist::shuffleIndexToReal(usize shuffleIdx) const {
    if (shuffleIdx >= shuffleOrder_.size()) return 0;
    return shuffleOrder_[shuffleIdx];
}

usize Playlist::realIndexToShuffle(usize realIdx) const {
    auto it = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), realIdx);
    if (it != shuffleOrder_.end()) {
        return std::distance(shuffleOrder_.begin(), it);
    }
    return 0;
}

Result<void> Playlist::saveM3U(const fs::path& path) const {
    std::ofstream file(path);
    if (!file) {
        return Result<void>::err("Failed to open file for writing");
    }
    
    file << "#EXTM3U\n";
    
    for (const auto& item : items_) {
        file << "#EXTINF:" << item.metadata.duration.count() / 1000 
             << "," << item.metadata.displayArtist() << " - " << item.metadata.displayTitle() << "\n";
        file << item.path.string() << "\n";
    }
    
    return Result<void>::ok();
}

Result<void> Playlist::loadM3U(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return Result<void>::err("Failed to open file");
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        fs::path filePath(line);
        if (!filePath.is_absolute()) {
            filePath = path.parent_path() / filePath;
        }
        
        addFile(filePath);
    }
    
    return Result<void>::ok();
}

} // namespace vc
