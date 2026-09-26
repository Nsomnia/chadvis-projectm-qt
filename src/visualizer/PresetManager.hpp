/**
 * @file PresetManager.hpp
 * @brief ProjectM preset management.
 *
 * This file defines the PresetManager class which provides the public API
 * for browsing, searching, and selecting presets. It delegates scanning
 * to PresetScanner and persistence to PresetPersistence.
 *
 * @section Patterns
 * - Manager: Central point of control for preset logic.
 * - Producer/consumer: one background scan worker publishes whole
 *   "generations" of the preset list back onto the publishing (GUI) thread.
 *
 * @section Threading
 * Scanning a real preset library is thousands of stat() calls plus a parse per
 * file, which is far too slow to run on the GUI thread. Two entry points exist
 * because two kinds of caller exist:
 *
 * - `scan()` / `rescan()` stay synchronous, for callers that must observe the
 *   result before they continue (the projectM bridge's own manager, which
 *   populates a native playlist right after scanning).
 * - `scanAsync()` / `rescanAsync()` hand the walk to a dedicated worker and
 *   publish the finished list on the thread that called `setPublishContext()`.
 *
 * Worker shape: a `vc::JThread` loop parked on a condition variable, i.e. the
 * `VideoRecorderThread` pattern (jthread + mutex + one-slot queue) rather than
 * a private `QThread` with a moved `QObject` (`CredentialStoreWorker`). The
 * reason is that PresetManager is a plain value member of `pm::Bridge`; making
 * it a QObject just to own a worker event loop would change its value
 * semantics, and a single blocking filesystem walk needs no event loop of its
 * own. The GUI-thread hand-off reuses `CredentialStoreWorker`'s mechanism
 * verbatim: `QMetaObject::invokeMethod(context, lambda, Qt::QueuedConnection)`.
 *
 * @section Invalidation strategy — shared generations
 * A rescan used to `presets_.clear()` and refill the same vector while QML
 * could still be holding `const PresetInfo*` into it, which is a use-after-free
 * waiting to happen. Instead, every scan builds a brand-new vector and *swaps
 * it in*; a published generation is never cleared, resized, or refilled in
 * place. Readers receive a `Snapshot` (shared ownership of one generation) or a
 * `PresetView` (a query result that pins its generation), so a pointer handed
 * out before a rescan keeps pointing at valid, correctly readable storage
 * afterwards. Holding a `Snapshot` also means no read path has to hold a lock
 * while it iterates.
 *
 * The alternative — a `std::deque`, or append-only with removals by swap — was
 * rejected: it does not survive a clear-and-refill either, it would still need
 * the same ownership to be safe, and `selectByIndex` semantics are defined
 * against a contiguous, indexable vector.
 *
 * Thread-affinity contract:
 * - Only the publishing thread mutates the list, the selection/history, or the
 *   favorite/blacklist name sets. `scanAsync()` snapshots those name sets on
 *   the calling thread, so the worker only ever reads its own copies.
 * - The worker never touches published state; it fills a private vector.
 * - `mutex_` guards the generation *hand-off* (so a reader can never observe a
 *   half-swapped vector) and the request/flag handshake with the worker. It is
 *   never held while scanning, sorting, iterating, or emitting.
 * - `current()` and `presetChanged` hand out *borrowed* pointers with no
 *   ownership attached; their return and signal types are fixed by existing
 *   callers, so they cannot carry a handle. They stay valid until the
 *   publishing thread publishes a new generation, and every caller consumes
 *   them immediately. Callers that need to hold a preset across a rescan use
 *   `allPresets()` / `activePresets()` and friends, which return a Snapshot.
 */

#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "PresetData.hpp"
#include "util/JThread.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

class QObject;

namespace vc {

class PresetManager {
public:
    using PresetList = std::vector<PresetInfo>;

    /// Shared ownership of one published generation of the preset list.
    ///
    /// The pinned storage is never moved, resized, or freed while a Snapshot is
    /// alive, so every `const PresetInfo*` taken from it stays valid across any
    /// number of rescans. Individual entries can still be edited in place by
    /// explicit calls (setFavorite/setBlacklisted/selection play count); the
    /// guarantee is about lifetime and identity, not immutability.
    using Snapshot = std::shared_ptr<const PresetList>;

    /// One filtered, ordered query result plus the generation it borrows from.
    /// Keeping the two together is what makes `items` safe to hold: dropping
    /// the `PresetView` releases the generation.
    struct PresetView {
        Snapshot generation;
        std::vector<const PresetInfo*> items;

        [[nodiscard]] usize size() const { return items.size(); }
        [[nodiscard]] bool empty() const { return items.empty(); }
    };

    PresetManager();
    ~PresetManager();
    PresetManager(const PresetManager&) = delete;
    PresetManager& operator=(const PresetManager&) = delete;

    // Scanning — synchronous, for callers that need the result immediately
    // (pm::Bridge populates a native playlist straight after scanning). A
    // failed scan keeps the previously published generation: a preset directory
    // that disappeared must not silently empty a working library.
    Result<void> scan(const fs::path& directory, bool recursive = true);
    void rescan();
    void clear();

    // Scanning — off-thread, for GUI callers
    /// Sets the thread that receives scan results through a queued
    /// invocation. Must outlive this manager (or be cleared first). With no
    /// context, scanAsync() falls back to the synchronous scan() so a headless
    /// caller still gets a populated manager.
    void setPublishContext(QObject* context);

    /// Queues a scan on the worker thread and returns immediately.
    /// @return false when the request was coalesced into an in-flight scan
    ///         (at most one follow-up is ever pending, so a QML rescan button
    ///         cannot pile up work or multiply the listChanged notification) or
    ///         when it was rejected during shutdown.
    bool scanAsync(const fs::path& directory, bool recursive = true);

    /// scanAsync() for the last directory handed to scan()/scanAsync().
    /// @return false when no directory is known yet.
    bool rescanAsync();

    /// True while a scan is queued or executing on the worker. The result may
    /// not be published yet.
    [[nodiscard]] bool scanInFlight() const;

    /// Blocks until the worker has finished every scan it was given. The
    /// publishing hand-off is a queued event, so this returning does not mean
    /// the result is visible yet — the publishing thread's event loop still has
    /// to run. Used by shutdown and tests; never call it from the worker.
    void waitForScan();

    // Access
    [[nodiscard]] usize count() const;
    [[nodiscard]] usize activeCount() const;
    [[nodiscard]] bool empty() const;

    [[nodiscard]] Snapshot allPresets() const;
    [[nodiscard]] PresetView activePresets() const;
    [[nodiscard]] PresetView favoritePresets() const;
    [[nodiscard]] PresetView blacklistedPresets() const;
    [[nodiscard]] std::vector<std::string> categories() const;

    // Selection
    /// Borrowed pointer into the published generation, valid until the next
    /// scan is published. Null when nothing is selected. Use allPresets() for
    /// a handle that survives a rescan.
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
    [[nodiscard]] PresetView search(const std::string& query) const;
    [[nodiscard]] PresetView byCategory(const std::string& category) const;

    // Persistence
    Result<void> loadState(const fs::path& path);
    Result<void> saveState(const fs::path& path) const;

    // Signals
    Signal<const PresetInfo*> presetChanged;
    Signal<> listChanged;
    /// An asynchronous scan failed. Published on the publishing thread, where
    /// the caller can report it; the previously published list is untouched.
    Signal<std::string> scanFailed;

private:
    struct ScanRequest {
        fs::path directory;
        bool recursive{true};
        std::set<std::string> favoriteNames;
        std::set<std::string> blacklistedNames;
    };

    struct ScanOutcome {
        PresetList presets;
        std::string error; ///< Empty on success.
    };

    void scanWorkerMain(StopToken stopToken);
    void deliverScanResult(ScanOutcome&& outcome);
    void applyScanOutcome(ScanOutcome&& outcome);
    void publishGeneration(PresetList&& scanned);
    void applyPendingSelection();
    void stopScanWorker();

    [[nodiscard]] Snapshot currentGeneration() const;
    [[nodiscard]] bool hasPublishContext() const;
    /// The published generation, for the publishing thread's own mutations.
    PresetList& liveList() {
        return *presets_;
    }

    // Published state — only the publishing thread mutates it. `presets_` is
    // replaced wholesale, never resized in place.
    std::shared_ptr<PresetList> presets_;
    usize currentIndex_{0};
    fs::path scanDirectory_;

    std::vector<usize> history_;
    usize historyPosition_{0};

    std::set<std::string> favoriteNames_;
    std::set<std::string> blacklistedNames_;
    std::string pendingPresetName_;

    std::mt19937 rng_{std::random_device{}()};

    // Worker handshake. `presets_` is read under this mutex only for the
    // hand-off; the worker never dereferences it.
    mutable std::mutex mutex_;
    std::condition_variable scanSignal_;
    JThread scanWorker_;
    QObject* publishContext_{nullptr};
    bool stopping_{false};
    bool scanRequested_{false};
    bool scanRunning_{false};
    ScanRequest pendingScan_;
};

} // namespace vc
