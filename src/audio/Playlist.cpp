#include "Playlist.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <numeric>
#include <sstream>

namespace vc {

namespace {

/// std::mt19937's sequence-seeded constructor takes its argument by lvalue
/// reference, so seeding it from a temporary std::random_device fails to
/// compile on libc++ ("expects an lvalue for $1"). Materializing the seed in
/// its own function keeps the ctor's member-init list clean.
std::mt19937::result_type freshSeed() {
    std::random_device device;
    return device();
}

} // namespace

Playlist::Playlist()
    : rng_(freshSeed()), ownerThread_(std::this_thread::get_id())
{
}

void Playlist::assertOwnerThread(const char* callSite) const {
    if (onOwnerThread()) {
        return;
    }
    // No lock exists to fail closed on, so say so loudly in every build. The
    // assert is debug-only (it compiles out under NDEBUG), which is exactly why
    // the log line comes first and is unconditional. std::thread::id is
    // streamable but not formattable, hence the ostringstream.
    std::ostringstream detail;
    detail << "Playlist: " << callSite << "() called from thread "
           << std::this_thread::get_id() << " but this Playlist is owned by thread "
           << ownerThread_ << "; Playlist is single-threaded by contract";
    LOG_ERROR("{}", detail.str());
    assert(false && "Playlist: cross-thread access violates the single-thread contract");
}

bool Playlist::onOwnerThread() const noexcept {
    return std::this_thread::get_id() == ownerThread_;
}

void Playlist::addFile(const fs::path& path) {
    assertOwnerThread(__func__);

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
    
    // Check for matching LRC file (external lyrics with timing)
    fs::path lrcPath = path;
    lrcPath.replace_extension(".lrc");
    
    if (fs::exists(lrcPath)) {
        item.lyricsPath = lrcPath.string();
        LOG_INFO("Found lyrics file: {}", lrcPath.string());
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
    
    // Appending cannot change the selection, so currentChanged stays silent.
    itemAdded.emitSignal(index);
    changed.emitSignal();
    
    LOG_DEBUG("Added to playlist: {}", path.filename().string());
}

void Playlist::addUrl(const std::string& url, const std::string& title) {
    assertOwnerThread(__func__);

    PlaylistItem item;
    item.url = url;
    item.isRemote = true;
    item.metadata.title = title.empty() ? url : title;
    
    usize index = items_.size();
    items_.push_back(std::move(item));
    
    if (shuffle_) {
        shuffleOrder_.push_back(index);
    }
    
    itemAdded.emitSignal(index);
    changed.emitSignal();
}

void Playlist::addFiles(const std::vector<fs::path>& paths) {
    assertOwnerThread(__func__);

    for (const auto& path : paths) {
        addFile(path);
    }
}

void Playlist::removeAt(usize index) {
    assertOwnerThread(__func__);

    if (index >= items_.size()) return;

    // The only case that changes the selection is deleting the selected item.
    // Captured up front because the index arithmetic below cannot distinguish
    // "renumbered" from "gone" afterwards.
    const bool removedCurrent = currentIndex_ && *currentIndex_ == index;

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
    // Mutated fully above, so a re-entrant reader sees the settled state. Renaming
    // the selection is *not* announced: the same track is still current, and a
    // listener woken for it would reload the track it is already playing.
    if (removedCurrent) {
        currentChanged.emitSignal(currentIndex_);
    }
    changed.emitSignal();
}

void Playlist::clear() {
    assertOwnerThread(__func__);

    // A selection that was already empty did not change, so nothing is emitted;
    // announcing a transition that did not happen is its own kind of lie.
    const bool hadCurrent = currentIndex_.has_value();

    items_.clear();
    currentIndex_ = std::nullopt;
    shuffleOrder_.clear();
    shufflePosition_ = 0;

    if (hadCurrent) {
        currentChanged.emitSignal(currentIndex_);
    }
    changed.emitSignal();
}

void Playlist::move(usize from, usize to) {
    assertOwnerThread(__func__);

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
    
    // The index adjustments above exist precisely to keep the same item
    // selected, so move() never changes the selection and never emits.
    changed.emitSignal();
}

std::optional<usize> Playlist::currentIndex() const {
    assertOwnerThread(__func__);
    return currentIndex_;
}

std::optional<PlaylistItem> Playlist::currentItem() const {
    assertOwnerThread(__func__);

    if (!currentIndex_ || *currentIndex_ >= items_.size()) {
        return std::nullopt;
    }
    return items_[*currentIndex_];
}

std::optional<PlaylistItem> Playlist::itemAt(usize index) const {
    assertOwnerThread(__func__);

    if (index >= items_.size()) return std::nullopt;
    return items_[index];
}

Playlist::Snapshot Playlist::snapshot() const {
    assertOwnerThread(__func__);
    return Snapshot{.items = items_, .currentIndex = currentIndex_};
}

bool Playlist::next() {
    assertOwnerThread(__func__);

    if (items_.empty()) return false;
    
    if (repeatMode_ == RepeatMode::One && currentIndex_) {
        // Not a transition: the same track, re-emitted because Repeat::One means
        // "play this one again". Consumers treat it as a replay request.
        currentChanged.emitSignal(currentIndex_);
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
    
    currentChanged.emitSignal(currentIndex_);
    return true;
}

bool Playlist::previous() {
    assertOwnerThread(__func__);

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
    
    currentChanged.emitSignal(currentIndex_);
    return true;
}

bool Playlist::jumpTo(usize index) {
    assertOwnerThread(__func__);

    if (index >= items_.size()) return false;
    
    currentIndex_ = index;
    
    if (shuffle_) {
        auto it = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), index);
        if (it != shuffleOrder_.end()) {
            shufflePosition_ = std::distance(shuffleOrder_.begin(), it);
        }
    }
    
    currentChanged.emitSignal(currentIndex_);
    return true;
}

bool Playlist::shuffle() const {
    assertOwnerThread(__func__);
    return shuffle_;
}

void Playlist::setShuffle(bool enabled) {
    assertOwnerThread(__func__);

    if (shuffle_ == enabled) return;
    
    shuffle_ = enabled;
    
    if (shuffle_) {
        regenerateShuffleOrder();
        if (currentIndex_) {
            shufflePosition_ = realIndexToShuffle(*currentIndex_);
        }
    }
    
    // The current track is untouched: enabling shuffle repositions the
    // traversal order around it, not the selection itself.
    changed.emitSignal();
}

RepeatMode Playlist::repeatMode() const {
    assertOwnerThread(__func__);
    return repeatMode_;
}

void Playlist::setRepeatMode(RepeatMode mode) {
    assertOwnerThread(__func__);

    repeatMode_ = mode;
    changed.emitSignal();
}

void Playlist::cycleRepeatMode() {
    assertOwnerThread(__func__);

    switch (repeatMode_) {
        case RepeatMode::Off: repeatMode_ = RepeatMode::All; break;
        case RepeatMode::All: repeatMode_ = RepeatMode::One; break;
        case RepeatMode::One: repeatMode_ = RepeatMode::Off; break;
    }
    changed.emitSignal();
}

usize Playlist::size() const {
    assertOwnerThread(__func__);
    return items_.size();
}

bool Playlist::empty() const {
    assertOwnerThread(__func__);
    return items_.empty();
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
    assertOwnerThread(__func__);

    std::ofstream file(path);
    if (!file) {
        return Result<void>::err("Failed to open file for writing");
    }
    
    file << "#EXTM3U\n";
    
    for (const auto& item : items_) {
        file << "#EXTINF:" << item.metadata.duration.count() / 1000 
             << "," << item.metadata.displayArtist() << " - " << item.metadata.displayTitle() << "\n";
        if (item.isRemote) {
            file << item.url << "\n";
        } else {
            file << item.path.string() << "\n";
        }
    }
    
    return Result<void>::ok();
}

Result<void> Playlist::loadM3U(const fs::path& path) {
    assertOwnerThread(__func__);

    std::ifstream file(path);
    if (!file) {
        return Result<void>::err("Failed to open file");
    }

    bool wasShuffle = shuffle_;
    shuffle_ = false;

    std::string line;
    std::vector<PlaylistItem> newItems;
    
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        PlaylistItem item;
        if (line.starts_with("http") || line.starts_with("https")) {
            item.url = line;
            item.isRemote = true;
            item.metadata.title = line;
        } else {
            fs::path filePath(line);
            if (!filePath.is_absolute()) {
                filePath = path.parent_path() / filePath;
            }

            fs::path baseDir = path.parent_path();
            fs::path resolved;
            try {
                resolved = fs::weakly_canonical(filePath);
                fs::path rel = fs::relative(resolved, baseDir);
                if (rel.string().starts_with("..")) {
                    LOG_WARN("Playlist: Path traversal blocked, skipping: {}", filePath.string());
                    continue;
                }
            } catch (const fs::filesystem_error& e) {
                LOG_WARN("Playlist: Failed to resolve path, skipping: {} ({})", filePath.string(), e.what());
                continue;
            }

            if (!fs::exists(resolved)) {
                LOG_WARN("Playlist: File not found, skipping: {}", resolved.string());
                continue;
            }

            LOG_DEBUG("Playlist: Loading file: {}", resolved.string());
            item.path = resolved;

            auto metaResult = MetadataReader::read(resolved);
            if (metaResult) {
                item.metadata = std::move(*metaResult);
            } else {
                item.metadata.title = resolved.stem().string();
            }

            fs::path lrcPath = resolved;
            lrcPath.replace_extension(".lrc");
            if (fs::exists(lrcPath)) item.lyricsPath = lrcPath.string();
        }
        newItems.push_back(std::move(item));
    }


    if (!newItems.empty()) {
        items_.insert(items_.end(), std::make_move_iterator(newItems.begin()), 
                                    std::make_move_iterator(newItems.end()));
        
        if (wasShuffle) {
            shuffle_ = true;
            regenerateShuffleOrder();
        }
        
        // Appending cannot change the selection, so currentChanged stays silent.
        changed.emitSignal();
        LOG_INFO("Playlist: Loaded {} items from M3U", newItems.size());
    }

    return Result<void>::ok();
}

} // namespace vc
