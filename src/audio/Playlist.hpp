#pragma once
// Playlist.hpp - Track queue management
// Because shuffle algorithms are surprisingly controversial
//
// ── Read API (no borrowed views, by design) ────────────────────────────────────
// The public read surface hands out *values* only:
//   - size() / empty() / currentIndex()  — scalar queries
//   - itemAt(i) / currentItem()          — one copied PlaylistItem (or empty)
//   - snapshot()                          — an owned { items, currentIndex } view
// There is deliberately no `const std::vector<PlaylistItem>&` accessor. The one
// that used to exist returned a reference into live storage, so every caller
// held a pointer that a later addFile()/removeAt() could invalidate under it,
// and there was no way to write a correct caller: the list model's persistent
// QModelIndex bracket, a re-entrant signal slot, and a "read these two items
// consistently" query all need a stable handle, and a bare reference gives none
// of them one. The same reasoning drives PresetManager's Snapshot/PresetView
// shared-generation design (src/visualizer/PresetManager.hpp:34-64) — see there
// for why a mutex is the wrong instrument.
//
// A copied PlaylistItem is cheap: after MediaMetadata dropped its dead QPixmap
// field, an item is a handful of std::string / path / integers, so the
// single-item accessors are O(1) and a whole-list snapshot is O(n) only where a
// caller actually asked for the whole list. Bridges that paint a list view use
// itemAt() per cell, never snapshot(), so a repaint does not become O(n²).
//
// ── Threading: single-threaded by contract, and enforced ──────────────────────
// A mutex is deliberately absent, and that is a decision, not an oversight:
//   - A lock would only cover items_ for the microseconds a reader holds it. The
//     things that actually dangle — a view's persistent QModelIndex and its
//     begin/endInsertRows bracket, and a signal slot reading state mid-emission
//     — are unprotectable by any lock inside this class.
//   - Worse, a lock replaces a loud failure with false confidence: an off-thread
//     writer would look safe while the model's row bookkeeping quietly rotted.
// So the contract is *enforced* instead of *hidden*: the ctor captures this
// instance's owning thread and every public method asserts it. An off-thread
// caller gets a log line in release builds and an abort in debug builds, instead
// of a heisenbug three call frames later. See onOwnerThread() for the predicate
// itself, which is public so the contract can be asserted without tripping the
// abort.
//
// A mutex would only become the right instrument if a second thread ever
// genuinely needs to touch one Playlist. Nothing does: the audio callback
// touches only AudioEngine's scratch buffer and queue, the analyzer thread never
// reaches the playlist, QMediaPlayer signals are delivered on the GUI thread,
// and src/recorder/ does not reference Playlist at all. Until that changes, the
// single-thread contract is the whole truth and is now machine-checked.
#include "util/Types.hpp"
#include "util/Signal.hpp"
#include "analysis/MediaMetadata.hpp"
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace vc {

struct PlaylistItem {
    fs::path path;
    std::string url;
    bool isRemote{false};
    MediaMetadata metadata;
    std::string lyricsPath;  // Path to external .lrc file
    // Helper to get title for fuzzy matching
    std::string title() const { return metadata.title.empty() ? path.stem().string() : metadata.title; }
};

enum class RepeatMode {
    Off,
    One,
    All
};

class Playlist {
public:
    /// Owned, self-consistent view of the whole queue. Remains valid and
    /// readable for as long as the caller holds it, no matter what the live
    /// Playlist does afterwards — that is the point of it.
    struct Snapshot {
        std::vector<PlaylistItem> items;
        std::optional<usize> currentIndex;

        [[nodiscard]] bool empty() const { return items.empty(); }
        [[nodiscard]] usize size() const { return items.size(); }
    };

    Playlist();

    // Modification
    void addFile(const fs::path& path);
    void addUrl(const std::string& url, const std::string& title = "");
    void addFiles(const std::vector<fs::path>& paths);
    void removeAt(usize index);
    void clear();
    void move(usize from, usize to);

    // Navigation
    [[nodiscard]] std::optional<usize> currentIndex() const;
    [[nodiscard]] std::optional<PlaylistItem> currentItem() const;
    [[nodiscard]] std::optional<PlaylistItem> itemAt(usize index) const;
    [[nodiscard]] Snapshot snapshot() const;

    bool next();
    bool previous();
    bool jumpTo(usize index);

    // Playback modes
    [[nodiscard]] bool shuffle() const;
    void setShuffle(bool enabled);

    [[nodiscard]] RepeatMode repeatMode() const;
    void setRepeatMode(RepeatMode mode);
    void cycleRepeatMode();

    // Queries
    [[nodiscard]] usize size() const;
    [[nodiscard]] bool empty() const;

    // Persistence
    [[nodiscard]] Result<void> saveM3U(const fs::path& path) const;
    Result<void> loadM3U(const fs::path& path);

    /// True when the calling thread is the one that constructed this Playlist.
    /// Public so the contract can be asserted (by tests, diagnostics, or a
    /// future worker hand-off) without tripping the abort in
    /// assertOwnerThread().
    [[nodiscard]] bool onOwnerThread() const noexcept;

    // ── Signal contract ──────────────────────────────────────────────────────
    // `changed` fires on any structural or mode change and is the "you must
    // re-read everything" signal. It says nothing about *which* item is current.
    //
    // `currentChanged` is the current-selection signal, and it is emitted
    // exactly when the item the selection resolves to changes:
    //   * next() / previous() / jumpTo()  — always (subject to the Repeat::One
    //     note below).
    //   * removeAt()                      — only when the *removed* item was the
    //     current one, i.e. the selection becomes "none". Removing an item
    //     below the current one renumbers the selection but the selected track,
    //     and the track prepared to follow it, are both unchanged, so a
    //     consumer must not be woken to reload it. Consumers that care about
    //     the index itself (PlaylistBridge) are driven directly by their own
    //     model notifications.
    //   * clear()                         — only when there *was* a selection.
    //     Emitting for an already-empty selection would be a transition that
    //     did not happen.
    //   * move()                          — never; move() tracks the moved item
    //     so the selected track is preserved by construction.
    //   * addFile()/addUrl()              — never; appending cannot change the
    //     selection.
    //
    // The payload is `std::optional<usize>` on purpose: the previous
    // `Signal<usize>` had no value that could mean "there is no current item",
    // which is precisely why removeAt() and clear() used to drop the
    // notification on the floor. A usize cannot express the null state, so the
    // engine kept playing a deleted track and never recomputed the next one.
    //
    // Repeat::One re-emits the *same* index, which is not a transition but a
    // replay request; it is documented here so the exception is not mistaken
    // for a contract violation.
    //
    // Every emit site mutates fully before emitting, so a re-entrant slot
    // reading itemAt()/currentItem()/snapshot() observes the settled
    // post-transition state. Pinned by tests/unit/audio/test_Playlist.cpp.
    Signal<> changed;
    Signal<std::optional<usize>> currentChanged;
    Signal<usize> itemAdded;
    Signal<usize> itemRemoved;

private:
    void assertOwnerThread(const char* callSite) const;

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

    /// The thread that constructed this Playlist; every public method asserts
    /// against it. Captured in the ctor and const, so a move to another thread
    /// carries the *original* owner and trips the assert rather than silently
    /// re-homing the object. It is deliberately not a `constexpr` token:
    /// `std::thread::id` is not a literal type in either libc++ or libstdc++
    /// (no constexpr default constructor), so a constexpr "unowned" identity
    /// cannot be written portably. What the design does guarantee is that the
    /// member has exactly one value for the lifetime of the object — there is no
    /// default or "not yet owned" state to reason about.
    const std::thread::id ownerThread_;
};

} // namespace vc
