#include "Playlist.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>

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

// ── #EXTINF ────────────────────────────────────────────────────────────
// The fields saveM3U writes and loadM3U used to discard. `#EXTINF:<seconds>,
// <artist> - <title>` is the shape saveM3U has always emitted, which is the
// standard extended-M3U form, so an M3U written by any other player is read
// back too rather than merely our own files.
struct ExtInf {
    i64 durationMs{0};
    std::string artist;
    std::string title;
};

ExtInf parseExtInf(std::string_view payload) {
    ExtInf info;

    const std::size_t comma = payload.find(',');
    if (comma == std::string_view::npos) {
        return info;
    }

    // Duration is seconds in the standard form, and written that way by
    // saveM3U. Parsed digit-by-digit rather than with std::stoi for the same
    // reason Color stopped using it: no allocation, no throw, and a
    // hand-edited file with "-5" or "1e3" yields 0 instead of an exception.
    //
    // The accumulator is seconds and is multiplied by 1000 once at the end.
    // Accumulating milliseconds per digit (`ms = ms * 1000 + d`) is wrong and
    // was wrong on the first draft of this: "#EXTINF:212" came back as
    // 2001002000 ms, which is 23 days. The bound stops a hand-edited file full
    // of digits from overflowing the multiply -- 10^16 seconds is already
    // longer than any recording, and past that a value of 0 is more honest than
    // a wrapped one.
    i64 seconds = 0;
    for (std::size_t i = 0; i < comma; ++i) {
        const char c = payload[i];
        if (c < '0' || c > '9') break;
        if (seconds > 10000000000000000LL) break;
        seconds = seconds * 10 + (c - '0');
    }
    info.durationMs = seconds * 1000;

    std::string_view rest = payload.substr(comma + 1);
    // Split on the FIRST " - ", which is the form saveM3U writes:
    // `displayArtist() + " - " + displayTitle()`.
    //
    // Splitting on the last one is superficially tidier and measurably wrong:
    // for "#EXTINF:5,A - B - C" it yields artist "A - B", because a track titled
    // "B - C" then swallows "A - " into its artist. An artist containing " - "
    // does not happen in music metadata; a title containing it happens
    // constantly ("Song - Part 1"). First occurrence it is.
    const std::size_t sep = rest.find(" - ");
    if (sep == std::string_view::npos) {
        info.title = std::string(rest);
    } else {
        info.artist = std::string(rest.substr(0, sep));
        info.title = std::string(rest.substr(sep + 3));
    }

    // saveM3U writes displayArtist()/displayTitle(), which substitute the
    // literal "Unknown Artist"/"Unknown Title" for an empty tag. Those are
    // display sentinels, not data, so they are mapped back to empty here --
    // otherwise every restored entry claims an artist the file does not have.
    auto trim = [](std::string& value) {
        const std::size_t first = value.find_first_not_of(" \t");
        if (first == std::string::npos) {
            value.clear();
            return;
        }
        const std::size_t last = value.find_last_not_of(" \t");
        value = value.substr(first, last - first + 1);
    };
    trim(info.artist);
    trim(info.title);
    if (info.artist == "Unknown Artist") info.artist.clear();
    if (info.title == "Unknown Title") info.title.clear();

    return info;
}

/// Diagnostics only: does this line look like a URL rather than a path?
///
/// This NEVER sets PlaylistItem::url and NEVER sets PlaylistItem::isRemote. It
/// exists for one reason: without it, a refused remote entry is reported as
/// "File not found: https:/evil.example/x.mp3" (note the collapsed "//" — the
/// path branch mangles it), which reads like a missing file rather than a
/// refusal by design. Do not let this predicate grow into a classifier; the
/// security property is that loadM3U has no remote branch at all, so there is
/// nothing for a classification to feed.
bool looksLikeRemoteReference(const std::string& line) {
    return line.starts_with("http://") || line.starts_with("https://") ||
           line.starts_with("ftp://") || line.starts_with("file://") ||
           line.starts_with("rtsp://") || line.starts_with("ws://");
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
        // Appended, like addUrl, and deliberately not spliced into a random
        // position: shufflePosition_ indexes the entry currently playing, so
        // moving entries underneath it would desynchronise the traversal from
        // the selection and let a pass visit one track twice while skipping
        // another. A new track simply plays later in the current pass.
        shuffleOrder_.push_back(index);
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
        if (currentIndex_) {
            ++shufflePosition_;
            if (shufflePosition_ >= shuffleOrder_.size()) {
                if (repeatMode_ == RepeatMode::All) {
                    shufflePosition_ = 0;
                    regenerateShuffleOrder();
                } else {
                    return false;
                }
            }
        }
        // No selection yet means the traversal has not started, so it begins at
        // the first entry of the permutation rather than stepping past it.
        // Pre-incrementing here used to skip that entry, so the first pass over
        // a three-item queue visited two of them under Repeat::Off.
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

bool Playlist::startPlayback() {
    assertOwnerThread(__func__);

    if (items_.empty() || currentIndex_)
        return false;

    if (shuffle_) {
        shufflePosition_ = 0;
        currentIndex_ = shuffleOrder_[0];
        currentChanged.emitSignal(currentIndex_);
        return true;
    }
    return jumpTo(0);
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
        // The standard extended-M3U form, and the same shape loadM3U reads back:
        // `#EXTINF:<seconds>,<artist> - <title>`. It was written here all along
        // and discarded on load, so the session file was neither round-tripped
        // nor a valid interchange file.
        file << "#EXTINF:" << item.metadata.duration.count() / 1000 
             << "," << item.metadata.displayArtist() << " - " << item.metadata.displayTitle() << "\n";
        if (item.isRemote) {
            // Deliberately still written even though loadM3U refuses to read it
            // back. Dropping it here would make the loss silent and
            // unrecoverable; keeping it means a session file that lists remote
            // entries is an honest record of a queue that had them, and the
            // refusal is logged at load time with a reason. Remote tracks still
            // reach the queue through DownloadQueue, which validates the host.
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
    bool sawRemoteLine = false;
    std::optional<ExtInf> pendingExtInf;

    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) {
            pendingExtInf.reset();
            continue;
        }

        // #EXTINF carries the metadata for the NEXT entry, which is the whole
        // standard's reason for it existing. It used to be skipped like every
        // other '#' line, so saveM3U wrote duration and artist into a file that
        // then threw both away on the way back in.
        if (line.starts_with("#EXTINF:")) {
            pendingExtInf = parseExtInf(std::string_view(line).substr(8));
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        // ── Every remaining line is a FILESYSTEM PATH. There is no remote
        // branch, and that is the fix rather than a simplification of one.
        //
        // The previous code classified a line by prefix and, for anything
        // starting "http", built a PlaylistItem with isRemote = true. The
        // traversal guard below applied only to the other branch, so a URL from
        // any .m3u -- a download, a shared drive, a forum post -- reached
        // AudioEngine::loadCurrentTrack and then QMediaPlayer::setSource
        // verbatim. `http://169.254.169.254/latest/meta-data/` therefore became
        // a track, and the client fetched the cloud-metadata endpoint on behalf
        // of whoever wrote the file. A local-file-open SSRF primitive, reachable
        // by writing a text file.
        //
        // Legitimate remote tracks never came through here: DownloadQueue
        // fetches them and adds local paths. A remote .m3u is refused rather
        // than honoured; if one is ever wanted it must go through the same
        // captured-host allowlist as every other outbound request
        // (AuthHeaders.cpp / DownloadQueue.cpp apply ManualRedirectPolicy and
        // an exact-origin check), not through a playlist loader.
        //
        // A URL line is not special-cased to be skipped -- it simply is not a
        // path, so the existence check below refuses it, exactly as it refuses
        // a filename that is not there.
        PlaylistItem item;
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
            if (looksLikeRemoteReference(line)) {
                LOG_WARN("Playlist: refused remote entry (loadM3U reads local paths "
                         "only, so a .m3u cannot turn a URL into a request): {}",
                         line);
                sawRemoteLine = true;
            } else {
                LOG_WARN("Playlist: File not found, skipping: {}", resolved.string());
            }
            continue;
        }

        LOG_DEBUG("Playlist: Loading file: {}", resolved.string());
        item.path = resolved;

        auto metaResult = MetadataReader::read(resolved);
        if (metaResult) {
            item.metadata = std::move(*metaResult);
            // #EXTINF fills the gaps the file's own tags leave, and never
            // overrides them: the file is the fresher record of what it is.
            //
            // Deliberately keyed on BLANK FIELDS rather than on read failure,
            // and that is a measurement rather than a preference. TagLib 2.3.2
            // returns a *successful* read for a .mp3 whose contents are nine
            // bytes of non-audio, with the title backfilled from the filename
            // stem (MediaMetadata.cpp:89-92), an empty artist and a zero
            // duration. A fallback keyed on failure would have been dead code,
            // and the two fields a user actually notices going missing across a
            // session restore are exactly the two a tag leaves blank.
            if (pendingExtInf) {
                if (item.metadata.artist.empty()) {
                    item.metadata.artist = pendingExtInf->artist;
                }
                if (item.metadata.duration == Duration{0}) {
                    item.metadata.duration = Duration(pendingExtInf->durationMs);
                }
            }
        } else if (pendingExtInf && !pendingExtInf->title.empty()) {
            item.metadata.title = pendingExtInf->title;
            item.metadata.artist = pendingExtInf->artist;
            item.metadata.duration = Duration(pendingExtInf->durationMs);
        } else {
            item.metadata.title = resolved.stem().string();
        }

        fs::path lrcPath = resolved;
        lrcPath.replace_extension(".lrc");
        if (fs::exists(lrcPath)) item.lyricsPath = lrcPath.string();

        pendingExtInf.reset();
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
        // This is the same rule addFile()/addUrl() follow, and it is also why a
        // restored session has no current track: selecting index 0 here would
        // mean AudioEngine::loadCurrentTrack fires on a playlist that the user
        // never asked to play, on the next event loop turn. If a restore should
        // select without playing, that is a decision for the caller that owns
        // the policy, not a side effect of reading a file.
        changed.emitSignal();
        LOG_INFO("Playlist: Loaded {} items from M3U", newItems.size());
    }

    if (sawRemoteLine) {
        LOG_INFO("Playlist: M3U contained remote entries, all refused. Remote tracks "
                 "arrive through DownloadQueue, which validates the host.");
    }

    return Result<void>::ok();
}

} // namespace vc
