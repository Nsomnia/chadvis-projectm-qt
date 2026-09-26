#include "PresetManager.hpp"

#include <QMetaObject>
#include <QObject>

#include <algorithm>
#include <utility>

#include "PresetPersistence.hpp"
#include "PresetScanner.hpp"
#include "core/Logger.hpp"

namespace vc {

PresetManager::PresetManager() : presets_(std::make_shared<PresetList>()) {}

PresetManager::~PresetManager() {
    stopScanWorker();
}

// ─── Off-thread scanning ─────────────────────────────────────────────────────

void PresetManager::setPublishContext(QObject* context) {
    std::lock_guard lock(mutex_);
    publishContext_ = context;
}

bool PresetManager::hasPublishContext() const {
    std::lock_guard lock(mutex_);
    return publishContext_ != nullptr;
}

bool PresetManager::scanAsync(const fs::path& directory, bool recursive) {
    if (!hasPublishContext()) {
        // Nobody asked for a GUI-thread hand-off (headless caller, no event
        // loop), so there is no thread to hop back to. Scan inline rather than
        // silently doing nothing.
        return static_cast<bool>(scan(directory, recursive));
    }

    scanDirectory_ = directory;

    ScanRequest request;
    request.directory = directory;
    request.recursive = recursive;
    // Copied here, on the calling (publishing) thread, so the worker only ever
    // reads its own snapshot while the GUI thread keeps mutating the live sets.
    request.favoriteNames = favoriteNames_;
    request.blacklistedNames = blacklistedNames_;

    bool started = false;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            LOG_WARN("PresetManager: ignoring a preset scan request during shutdown");
        } else if (scanRequested_ || scanRunning_ || scanPublishing_) {
            // Coalesce. Latest request wins and at most ONE follow-up stays
            // queued, so a QML rescan button (or a burst of them) cannot pile up
            // redundant walks or multiply the listChanged notification storm.
            //
            // scanPublishing_ belongs in this test, not just in scanInFlight():
            // between the worker posting its result and the publishing thread
            // applying it, scanRunning_ is already false, so without this term a
            // request arriving in that gap would start a second concurrent walk
            // and break the one-walk-at-a-time guarantee. That window is exactly
            // where a burst of rescan calls lands, because no event loop has run
            // to drain the queued hand-off yet.
            pendingScan_ = std::move(request);
            scanRequested_ = true;
        } else {
            if (!scanWorker_.joinable()) {
                scanWorker_ = JThread([this](StopToken stopToken) {
                    scanWorkerMain(stopToken);
                });
            }
            pendingScan_ = std::move(request);
            scanRequested_ = true;
            started = true;
        }
    }
    scanSignal_.notify_all();
    return started;
}

bool PresetManager::rescanAsync() {
    if (scanDirectory_.empty())
        return false;
    // Mirrors rescan(): the last scan was recursive, so a bare rescan is too.
    return scanAsync(scanDirectory_);
}

bool PresetManager::scanInFlight() const {
    std::lock_guard lock(mutex_);
    return scanRequested_ || scanRunning_ || scanPublishing_;
}

void PresetManager::waitForScan() {
    std::unique_lock lock(mutex_);
    scanSignal_.wait(lock, [this] {
        return stopping_ || (!scanRequested_ && !scanRunning_);
    });
}

void PresetManager::scanWorkerMain(StopToken stopToken) {
    for (;;) {
        ScanRequest request;
        {
            std::unique_lock lock(mutex_);
            scanSignal_.wait(lock, [this, &stopToken] {
                return stopping_ || stopToken.stop_requested() || scanRequested_;
            });
            if (stopping_ || stopToken.stop_requested())
                return;
            request = std::move(pendingScan_);
            pendingScan_ = ScanRequest{};
            scanRequested_ = false;
            scanRunning_ = true;
        }

        ScanOutcome outcome;
        if (auto result = PresetScanner::scan(request.directory,
                                              request.recursive,
                                              outcome.presets,
                                              request.favoriteNames,
                                              request.blacklistedNames);
            !result) {
            LOG_WARN("PresetManager: preset scan of '{}' failed: {}",
                     request.directory.string(),
                     result.error().message);
            outcome.presets.clear();
            outcome.error = result.error().message;
        }

        deliverScanResult(std::move(outcome));
    }
}

void PresetManager::deliverScanResult(ScanOutcome&& outcome) {
    QObject* context = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            // The destructor is joining us. It also stops the result from being
            // posted at a publish context that may be going away.
            //
            // Note this leaves scanRunning_ set: the result will never be
            // applied, so nothing clears it. That is deliberate and safe -- the
            // manager is being destroyed, scanInFlight() is only queried by live
            // callers, and stopScanWorker's own predicate uses stopping_ instead.
            return;
        }
        context = publishContext_;
    }

    if (!context) {
        // Unreachable: scanAsync() refuses to use the worker without a context.
        LOG_ERROR("PresetManager: dropping a preset scan result with no publishing context");
    } else if (!QMetaObject::invokeMethod(
                       context,
                       [this, outcome = std::move(outcome)]() mutable {
                           applyScanOutcome(std::move(outcome));
                       },
                       Qt::QueuedConnection)) {
        LOG_ERROR("PresetManager: could not queue the preset scan result onto its publishing context");
        // Nothing will ever call applyScanOutcome, so scanPublishing_ must not be
        // set -- otherwise every later request would coalesce into a no-op and
        // the manager would look permanently busy.
        finishPostedScan(/*publishing=*/false);
    } else {
        // Cleared only now, after the hand-off exists, so waitForScan() cannot
        // return before the publishing event has been posted. scanPublishing_
        // stays set until the publishing thread applies the result, so a request
        // arriving in the gap coalesces instead of starting a second walk.
        finishPostedScan(/*publishing=*/true);
    }
}

void PresetManager::finishPostedScan(bool publishing) {
    {
        std::lock_guard lock(mutex_);
        scanRunning_ = false;
        scanPublishing_ = publishing;
    }
    scanSignal_.notify_all();
}

void PresetManager::applyScanOutcome(ScanOutcome&& outcome) {
    // The publish window is now over, so the manager is no longer busy. Doing it
    // here, on the publishing thread, is what makes the coalescing window
    // airtight: scanInFlight() has been true continuously from the request
    // through the walk to this point, so no rescan could have started a second
    // concurrent walk. Releasing before the emits also means a slot that
    // resyncs during listChanged correctly queues a fresh walk.
    finishPostedScan(/*publishing=*/false);

    if (!outcome.error.empty()) {
        // Keep the previously published generation: a failed walk must not
        // empty a working library.
        scanFailed.emitSignal(outcome.error);
        return;
    }
    publishGeneration(std::move(outcome.presets));
}

void PresetManager::publishGeneration(PresetList&& scanned) {
    auto generation = std::make_shared<PresetList>(std::move(scanned));
    {
        std::lock_guard lock(mutex_);
        // Swap, never clear-and-refill: a Snapshot pinned by any reader keeps
        // the old storage alive and unmodified.
        presets_ = std::move(generation);
    }
    applyPendingSelection();
    listChanged.emitSignal();
}

void PresetManager::applyPendingSelection() {
    if (!pendingPresetName_.empty()) {
        if (selectByName(pendingPresetName_))
            pendingPresetName_.clear();
    }
}

void PresetManager::stopScanWorker() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    scanWorker_.request_stop();
    scanSignal_.notify_all();
    if (scanWorker_.joinable()) {
        // A filesystem walk cannot be cancelled mid-stat(); the join waits for
        // the scan that is already running. Same bounded shutdown wait
        // CredentialStoreWorker performs for a keychain call.
        scanWorker_.join();
    }
}

// ─── Synchronous scanning ────────────────────────────────────────────────────

Result<void> PresetManager::scan(const fs::path& directory, bool recursive) {
    scanDirectory_ = directory;

    // Scan into a private vector and publish it, so a pinned Snapshot is never
    // resized underneath its holder.
    PresetList scanned;
    if (auto result = PresetScanner::scan(
                directory, recursive, scanned, favoriteNames_, blacklistedNames_);
        !result) {
        return result;
    }

    publishGeneration(std::move(scanned));
    return Result<void>::ok();
}

void PresetManager::rescan() {
    if (!scanDirectory_.empty())
        (void)scan(scanDirectory_);
}

void PresetManager::clear() {
    currentIndex_ = 0;
    publishGeneration(PresetList{});
}

// ─── Access ───────────────────────────────────────────────────────────────────

PresetManager::Snapshot PresetManager::currentGeneration() const {
    std::lock_guard lock(mutex_);
    return presets_;
}

usize PresetManager::count() const {
    const Snapshot generation = currentGeneration();
    return generation->size();
}

usize PresetManager::activeCount() const {
    const Snapshot generation = currentGeneration();
    return static_cast<usize>(std::count_if(
            generation->begin(), generation->end(), [](const auto& p) {
                return !p.blacklisted;
            }));
}

bool PresetManager::empty() const {
    return count() == 0;
}

PresetManager::Snapshot PresetManager::allPresets() const {
    return currentGeneration();
}

PresetManager::PresetView PresetManager::activePresets() const {
    PresetView view;
    view.generation = currentGeneration();
    for (const auto& p : *view.generation)
        if (!p.blacklisted)
            view.items.push_back(&p);
    return view;
}

PresetManager::PresetView PresetManager::favoritePresets() const {
    PresetView view;
    view.generation = currentGeneration();
    for (const auto& p : *view.generation)
        if (p.favorite && !p.blacklisted)
            view.items.push_back(&p);
    return view;
}

PresetManager::PresetView PresetManager::blacklistedPresets() const {
    PresetView view;
    view.generation = currentGeneration();
    for (const auto& p : *view.generation)
        if (p.blacklisted)
            view.items.push_back(&p);
    return view;
}

std::vector<std::string> PresetManager::categories() const {
    const Snapshot generation = currentGeneration();
    std::set<std::string> cats;
    for (const auto& p : *generation)
        cats.insert(p.category);
    return {cats.begin(), cats.end()};
}

// ─── Selection ───────────────────────────────────────────────────────────────

const PresetInfo* PresetManager::current() const {
    const PresetList& list = *currentGeneration();
    if (currentIndex_ >= list.size())
        return nullptr;
    return &list[currentIndex_];
}

bool PresetManager::selectByIndex(usize index) {
    PresetList& list = liveList();
    if (index >= list.size() || list[index].blacklisted)
        return false;

    if (history_.empty() || history_[historyPosition_] != index) {
        if (!history_.empty() && historyPosition_ < history_.size() - 1) {
            history_.erase(history_.begin() + historyPosition_ + 1, history_.end());
        }
        history_.push_back(index);
        historyPosition_ = history_.size() - 1;
        if (history_.size() > 100) {
            history_.erase(history_.begin());
            historyPosition_--;
        }
    }

    currentIndex_ = index;
    list[currentIndex_].playCount++;
    const PresetInfo* changed = &list[currentIndex_];
    presetChanged.emitSignal(changed);
    return true;
}

bool PresetManager::selectByName(const std::string& name) {
    const PresetList& list = liveList();
    if (list.empty()) {
        pendingPresetName_ = name;
        return false;
    }

    for (usize i = 0; i < list.size(); ++i) {
        if (list[i].name == name && !list[i].blacklisted)
            return selectByIndex(i);
    }

    for (usize i = 0; i < list.size(); ++i) {
        if (!list[i].blacklisted && list[i].name.find(name) != std::string::npos)
            return selectByIndex(i);
    }

    std::string lowerName = name;
    std::transform(
            lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    for (usize i = 0; i < list.size(); ++i) {
        if (list[i].blacklisted)
            continue;
        std::string lowerPreset = list[i].name;
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
    const PresetList& list = liveList();
    for (usize i = 0; i < list.size(); ++i) {
        if (list[i].path == path && !list[i].blacklisted)
            return selectByIndex(i);
    }
    return false;
}

bool PresetManager::selectRandom() {
    const PresetView active = activePresets();
    if (active.empty())
        return false;
    std::uniform_int_distribution<usize> dist(0, active.items.size() - 1);
    const PresetInfo* preset = active.items[dist(rng_)];
    const PresetList& list = *active.generation;
    for (usize i = 0; i < list.size(); ++i)
        if (&list[i] == preset)
            return selectByIndex(i);
    return false;
}

bool PresetManager::selectNext() {
    if (liveList().empty())
        return false;
    PresetList& list = liveList();
    if (!history_.empty() && historyPosition_ < history_.size() - 1) {
        historyPosition_++;
        currentIndex_ = history_[historyPosition_];
        list[currentIndex_].playCount++;
        presetChanged.emitSignal(&list[currentIndex_]);
        return true;
    }

    std::string currentName = current() ? current()->name : "";
    usize start = currentIndex_;
    usize nextIndex = currentIndex_;
    do {
        nextIndex = (nextIndex + 1) % list.size();
        if (!list[nextIndex].blacklisted) {
            if (!currentName.empty() && list[nextIndex].name == currentName)
                continue;
            return selectByIndex(nextIndex);
        }
    } while (nextIndex != start);
    return false;
}

bool PresetManager::selectPrevious() {
    if (liveList().empty())
        return false;
    PresetList& list = liveList();
    if (!history_.empty() && historyPosition_ > 0) {
        historyPosition_--;
        currentIndex_ = history_[historyPosition_];
        list[currentIndex_].playCount++;
        presetChanged.emitSignal(&list[currentIndex_]);
        return true;
    }

    std::string currentName = current() ? current()->name : "";
    usize start = currentIndex_;
    usize prevIndex = currentIndex_;
    do {
        prevIndex = (prevIndex == 0) ? list.size() - 1 : prevIndex - 1;
        if (!list[prevIndex].blacklisted) {
            if (!currentName.empty() && list[prevIndex].name == currentName)
                continue;
            return selectByIndex(prevIndex);
        }
    } while (prevIndex != start);
    return false;
}

// ─── Favorites & Blacklist ───────────────────────────────────────────────────

void PresetManager::setFavorite(usize index, bool favorite) {
    if (index >= liveList().size())
        return;
    liveList()[index].favorite = favorite;
    if (favorite)
        favoriteNames_.insert(liveList()[index].name);
    else
        favoriteNames_.erase(liveList()[index].name);
    listChanged.emitSignal();
}

void PresetManager::setBlacklisted(usize index, bool blacklisted) {
    if (index >= liveList().size())
        return;
    liveList()[index].blacklisted = blacklisted;
    if (blacklisted)
        blacklistedNames_.insert(liveList()[index].name);
    else
        blacklistedNames_.erase(liveList()[index].name);
    listChanged.emitSignal();
}

void PresetManager::toggleFavorite(usize index) {
    if (index < liveList().size())
        setFavorite(index, !liveList()[index].favorite);
}

void PresetManager::toggleBlacklisted(usize index) {
    if (index < liveList().size())
        setBlacklisted(index, !liveList()[index].blacklisted);
}

// ─── Search ───────────────────────────────────────────────────────────────────

PresetManager::PresetView PresetManager::search(const std::string& query) const {
    PresetView view;
    view.generation = currentGeneration();
    std::string lowerQuery = query;
    std::transform(
            lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    for (const auto& p : *view.generation) {
        std::string lowerName = p.name;
        std::transform(lowerName.begin(),
                       lowerName.end(),
                       lowerName.begin(),
                       ::tolower);
        if (lowerName.find(lowerQuery) != std::string::npos)
            view.items.push_back(&p);
    }
    return view;
}

PresetManager::PresetView PresetManager::byCategory(
        const std::string& category) const {
    PresetView view;
    view.generation = currentGeneration();
    for (const auto& p : *view.generation)
        if (p.category == category && !p.blacklisted)
            view.items.push_back(&p);
    return view;
}

// ─── Persistence ─────────────────────────────────────────────────────────────

Result<void> PresetManager::loadState(const fs::path& path) {
    // Merge into a copy and swap it in. Loading straight into the published
    // vector would reallocate it, which is exactly the invalidation a pinned
    // Snapshot must never see.
    auto merged = std::make_shared<PresetList>(*presets_);
    if (auto result = PresetPersistence::loadState(
                path, favoriteNames_, blacklistedNames_, *merged);
        !result) {
        return result;
    }
    {
        std::lock_guard lock(mutex_);
        presets_ = std::move(merged);
    }
    return Result<void>::ok();
}

Result<void> PresetManager::saveState(const fs::path& path) const {
    return PresetPersistence::saveState(
            path, favoriteNames_, blacklistedNames_);
}

} // namespace vc
