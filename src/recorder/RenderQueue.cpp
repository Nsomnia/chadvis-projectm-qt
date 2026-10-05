#include "recorder/RenderQueue.hpp"

#include "core/Logger.hpp"

#include <QDateTime>

#include <algorithm>
#include <cerrno>

namespace vc {

namespace {

/// ISO-8601 UTC to the second, matching the receipt's documented precision.
[[nodiscard]] std::string utcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Failure taxonomy
// ─────────────────────────────────────────────────────────────────────────────

std::string_view renderFailureKindName(const RenderFailureKind kind) noexcept {
    switch (kind) {
        case RenderFailureKind::None: return "none";
        case RenderFailureKind::Cancelled: return "cancelled";
        case RenderFailureKind::Transient: return "transient";
        case RenderFailureKind::Permanent: return "permanent";
    }
    return "unknown";
}

const std::vector<RenderErrnoRule>& renderErrnoRules() {
    // Order is irrelevant to the lookup (first match wins and every errno here is
    // distinct), but grouped permanent-first so the two decisions read apart.
    static const std::vector<RenderErrnoRule> rules{
        {ENOSPC, RenderFailureKind::Permanent,
         "the filesystem is out of space; another attempt writes the same zero"},
#ifdef EDQUOT
        {EDQUOT, RenderFailureKind::Permanent, "the user's quota is exhausted"},
#endif
#ifdef EROFS
        {EROFS, RenderFailureKind::Permanent, "the output filesystem is read-only"},
#endif
        {EACCES, RenderFailureKind::Permanent, "the output path is not writable"},
        {EPERM, RenderFailureKind::Permanent, "the operation is not permitted"},
        {EINVAL, RenderFailureKind::Permanent,
         "the encoder or muxer rejected the settings outright"},
#ifdef ENOTSUP
        {ENOTSUP, RenderFailureKind::Permanent,
         "this build cannot do what the job asked for"},
#endif
        {EBADF, RenderFailureKind::Permanent, "a descriptor was closed under us"},
        {ENODEV, RenderFailureKind::Permanent,
         "the requested hardware encoder or render node does not exist"},
#ifdef ENXIO
        {ENXIO, RenderFailureKind::Permanent, "the render device is not there"},
#endif
        {EAGAIN, RenderFailureKind::Transient, "resource temporarily unavailable"},
        {EINTR, RenderFailureKind::Transient, "a signal cut the call short"},
        {ENOMEM, RenderFailureKind::Transient,
         "another job may have just released its buffers"},
        {EBUSY, RenderFailureKind::Transient, "the device is momentarily in use"},
    };
    return rules;
}

RenderFailureKind classifyRenderFailure(const RenderFailure& failure) {
    if (failure.osErrno != 0) {
        for (const auto& rule : renderErrnoRules()) {
            if (rule.osErrno != failure.osErrno) continue;
            return rule.kind;
        }
        // An errno nobody classified is a real signal, and Transient is the
        // answer that keeps the failure visible while costing one attempt.
    }
    return failure.kind;
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch reporting
// ─────────────────────────────────────────────────────────────────────────────

int RenderResult::progressPercent() const noexcept {
    if (framesExpected == 0) return -1;
    const u64 scaled = (framesDone * 100u) / framesExpected;
    return static_cast<int>(std::min<u64>(scaled, 100u));
}

u32 BatchSummary::settled() const noexcept {
    return completed + completedPartial + failedRetryable + failedPermanent +
           cancelled;
}

u32 BatchSummary::accountedFor() const noexcept { return settled() + outstanding; }

u32 BatchSummary::fullyRendered() const noexcept { return completed; }

u32 BatchSummary::notFullyRendered() const noexcept {
    return completedPartial + failedRetryable + failedPermanent + cancelled;
}

bool BatchSummary::isSettled() const noexcept { return total > 0 && settled() == total; }

BatchSummary summarise(const std::span<const RenderResult> results) {
    BatchSummary summary;
    summary.total = static_cast<u32>(results.size());
    for (const auto& result : results) {
        switch (result.state) {
            case RenderState::Queued:
            case RenderState::Rendering:
                ++summary.outstanding;
                break;
            case RenderState::Completed:
                ++summary.completed;
                break;
            case RenderState::CompletedPartial:
                ++summary.completedPartial;
                break;
            case RenderState::FailedRetryable:
                ++summary.failedRetryable;
                break;
            case RenderState::FailedPermanent:
                ++summary.failedPermanent;
                break;
            case RenderState::Cancelled:
                ++summary.cancelled;
                break;
        }
    }
    return summary;
}

int batchProgressPercent(const std::span<const RenderResult> results) {
    u64 done = 0;
    u64 expected = 0;
    for (const auto& result : results) {
        done += result.framesDone;
        expected += result.framesExpected;
    }
    if (expected == 0) return -1;
    return static_cast<int>(std::min<u64>((done * 100u) / expected, 100u));
}

// ─────────────────────────────────────────────────────────────────────────────
// Queue
// ─────────────────────────────────────────────────────────────────────────────

RenderQueue::RenderQueue(JobRunner runner, QObject* parent)
    : QObject(parent), runner_(std::move(runner)) {}

RenderQueue::~RenderQueue() = default;

void RenderQueue::setMaxConcurrent(const int maxConcurrent) {
    maxConcurrent_ = std::max(1, maxConcurrent);
    pump();
}

void RenderQueue::setMaxAttempts(const int maxAttempts) {
    maxAttempts_ = std::max(1, maxAttempts);
}

void RenderQueue::setCancelHook(CancelHook hook) { cancelHook_ = std::move(hook); }

std::expected<RenderState, JobError> RenderQueue::enqueue(RenderJob job) {
    if (findResult(job.id) != results_.size()) {
        LOG_WARN("RenderQueue: duplicate live job for id {}", job.id);
        return std::unexpected(JobError{JobErrorKind::DuplicateId, "job.id",
                                        "a job with this id is already in the batch"});
    }
    if (auto invalid = job.validate(); !invalid) {
        LOG_WARN("RenderQueue: refused job {}: {}", job.id, invalid.error().describe());
        return std::unexpected(invalid.error());
    }

    auto item = std::make_unique<Item>();
    item->job = std::move(job);
    // Stamp the receipt's denominator from the job so the runner and the batch
    // aggregate cannot disagree about how many frames this render owes.
    item->job.receipt.framesExpected = item->job.expectedFrames;
    item->resultIndex = results_.size();

    RenderResult slot;
    slot.jobId = item->job.id;
    slot.state = RenderState::Queued;
    slot.framesExpected = item->job.expectedFrames;
    results_.push_back(std::move(slot));

    const std::string id = item->job.id;
    pending_.push_back(std::move(item));

    // -1, not 0, when the frame count is unknown. `progressPercent()` is
    // documented to answer -1 for an unknown denominator precisely so that
    // "0% of nothing" and "0% of a lot" stay distinguishable; a hardcoded 0 here
    // threw that away, and a batch whose first report is a fabricated 0% looks
    // like a stall to whatever is drawing the bar.
    emit jobStateChanged(QString::fromStdString(id),
                         static_cast<int>(RenderState::Queued), results_.back().progressPercent());
    idleEmitted_ = false;
    emitBatchProgress();

    pump();
    // Reported back rather than assumed: `pump()` may already have handed this
    // job out, and a caller that believed it was still queued would schedule the
    // wrong thing.
    return stateOf(id);
}

bool RenderQueue::cancel(const std::string& jobId) {
    Item* item = findItem(jobId);
    // Unknown or already terminal: graceful no-op, exactly as `DownloadQueue::cancel`
    // behaves for both cases.
    if (item == nullptr || isTerminal(item->state)) return false;

    if (item->state == RenderState::Queued) {
        // Never started, so nothing to interrupt: `settle` owns the removal, and
        // an earlier version of this function erased from `pending_` *before*
        // calling it, which destroyed the very `Item` the call then read.
        settle(jobId, *item, RenderState::Cancelled, 0, 0, 0, RenderFailureKind::Cancelled,
               "cancelled before a slot was free");
        return true;
    }

    item->cancelRequested = true;
    if (cancelHook_) cancelHook_(jobId);
    // No immediate state change: the executor still owns this job, and the
    // outcome it reports decides the receipt. Only the *request* is ours.
    return true;
}

bool RenderQueue::isCancelRequested(const std::string& jobId) const noexcept {
    const Item* item = findItem(jobId);
    return item != nullptr && item->cancelRequested;
}

bool RenderQueue::reportProgress(const std::string& jobId, const u64 framesDone) {
    Item* item = findItem(jobId);
    if (item == nullptr || isTerminal(item->state)) return false;

    const u64 previous = results_[item->resultIndex].framesDone;
    if (framesDone < previous) {
        LOG_WARN("RenderQueue: {} reported progress going backwards ({} < {}), ignoring",
                 jobId, framesDone, previous);
        return false;
    }
    publishProgress(*item, framesDone);
    return true;
}

bool RenderQueue::finish(const std::string& jobId, RenderReceipt receipt) {
    Item* item = findItem(jobId);
    if (item == nullptr || isTerminal(item->state)) return false;
    if (!isTerminal(receipt.outcome)) {
        LOG_WARN("RenderQueue: {} reported a non-terminal outcome {}", jobId,
                 std::string(renderStateName(receipt.outcome)));
        return false;
    }

    // The honesty decision lives here, not in the worker: a worker that says
    // "completed" while writing 400 of 1800 frames must not certify a short file.
    RenderState state = receipt.outcome;
    if (state == RenderState::Completed &&
        receipt.framesWritten < item->job.expectedFrames) {
        state = RenderState::CompletedPartial;
    }
    // A successful report stands even under a cancel request: the executor's own
    // report is the ground truth about what reached the file, and overriding it
    // would file a complete output under "cancelled".
    const RenderFailureKind failureKind =
        state == RenderState::Completed || state == RenderState::CompletedPartial
            ? RenderFailureKind::None
            : RenderFailureKind::Permanent;

    settle(jobId, *item, state, receipt.framesWritten, receipt.bytesWritten,
           receipt.elapsedMs, failureKind, std::move(receipt.note));
    return true;
}

bool RenderQueue::fail(const std::string& jobId, RenderFailure failure) {
    Item* item = findItem(jobId);
    if (item == nullptr || isTerminal(item->state)) return false;

    const auto matches = [&jobId](const auto& ptr) { return ptr->job.id == jobId; };
    const RenderFailureKind kind = classifyRenderFailure(failure);
    if (kind == RenderFailureKind::None) {
        LOG_WARN("RenderQueue: {} reported no failure through the failure path", jobId);
        return false;
    }
    if (kind == RenderFailureKind::Cancelled || item->cancelRequested) {
        settle(jobId, *item, RenderState::Cancelled, results_[item->resultIndex].framesDone,
               0, 0, RenderFailureKind::Cancelled,
               failure.reason.empty() ? "cancelled" : failure.reason);
        return true;
    }

    ++item->attempts;
    if (kind == RenderFailureKind::Transient && item->attempts < maxAttempts_) {
        LOG_WARN("RenderQueue: {} failed transiently (attempt {}/{}): {}", jobId,
                 item->attempts, maxAttempts_, failure.reason);
        // Ownership hops to the front of the queue; the erase below drops the
        // active_ reference, so exactly one container owns it afterwards.
        const auto it = std::find_if(active_.begin(), active_.end(), matches);
        if (it == active_.end()) return false;  // settled reentrantly; nothing to retry
        auto owned = std::move(*it);
        active_.erase(it);
        setState(*owned, RenderState::Queued);
        pending_.push_front(std::move(owned));
        publishProgress(*pending_.front(), results_[pending_.front()->resultIndex].framesDone);
        pump();
        return true;
    }

    const RenderState state = kind == RenderFailureKind::Transient
                                  ? RenderState::FailedRetryable
                                  : RenderState::FailedPermanent;
    settle(jobId, *item, state, results_[item->resultIndex].framesDone, 0, 0, kind,
           failure.reason);
    return true;
}

std::size_t RenderQueue::batchPosition(const std::string& jobId) const noexcept {
    const std::size_t index = findResult(jobId);
    return index == results_.size() ? 0 : index + 1;
}

int RenderQueue::waitingAhead(const std::string& jobId) const noexcept {
    int ahead = 0;
    for (const auto& item : pending_) {
        if (item->job.id == jobId) return ahead;
        ++ahead;
    }
    return 0;
}

const RenderJob* RenderQueue::job(const std::string& jobId) const noexcept {
    const Item* item = findItem(jobId);
    return item == nullptr ? nullptr : &item->job;
}

RenderState RenderQueue::stateOf(const std::string& jobId) const noexcept {
    // Live items first, and not as an optimisation. `clearBatch()` empties
    // results_, and a live job's state lives *only* in its Item -- so reading the
    // array alone reported Queued for a job that was demonstrably mid-render,
    // which is exactly the "declared but not implemented" shape this class
    // exists to avoid. A live job always wins over its (possibly stale) slot.
    if (const Item* live = findItem(jobId); live != nullptr) {
        return live->state;
    }
    const std::size_t index = findResult(jobId);
    return index == results_.size() ? RenderState::Queued : results_[index].state;
}

std::vector<std::string> RenderQueue::idsNotFullyRendered() const {
    std::vector<std::string> ids;
    for (const auto& result : results_) {
        if (result.state == RenderState::Completed) continue;
        if (!isTerminal(result.state)) continue;
        ids.push_back(result.jobId);
    }
    return ids;
}

void RenderQueue::clearBatch() {
    // Compacting, NOT `results_.clear()`.
    //
    // `Item::resultIndex` is stamped once at enqueue and nine sites index
    // `results_[item->resultIndex]` with no bounds check. So the previous
    // implementation -- which emptied the vector -- left every LIVE job holding
    // an index into freed storage, and the next `reportProgress` on one of them was
    // an out-of-bounds read followed by an out-of-bounds write. The header
    // documented "a job already in flight keeps writing into its slot" without
    // noticing that after a clear there was no slot.
    //
    // Erasing only the settled entries and remapping what is left keeps both
    // halves of the contract: the batch genuinely starts over for everything that
    // had finished, and a live job keeps a slot to write into. A new batch
    // therefore starts numbering from the jobs that are still running, which is
    // the only place a batch can legitimately continue from.
    std::vector<RenderResult> kept;
    kept.reserve(results_.size());
    // remap[i] is where slot i ends up, or results_.size() when it was dropped.
    std::vector<std::size_t> remap(results_.size(), results_.size());
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (findItem(results_[i].jobId) != nullptr) {
            remap[i] = kept.size();
            kept.push_back(results_[i]);
        }
    }

    // Unreachable by construction -- a live item's own slot is kept, because
    // findItem() finds it -- but this is an unchecked index into a vector and the
    // cost of being wrong is a wild write, so it is clamped rather than trusted.
    const auto remapOne = [&remap](Item& item) {
        if (item.resultIndex >= remap.size()) return;
        const std::size_t target = remap[item.resultIndex];
        item.resultIndex = target < remap.size() ? target : 0;
    };
    for (const auto& held : pending_) {
        remapOne(*held);
    }
    for (const auto& held : active_) {
        remapOne(*held);
    }
    results_ = std::move(kept);

    lastBatchPercent_ = -2;
    emitBatchProgress();
}

// ── internals ────────────────────────────────────────────────────────────────

RenderQueue::Item* RenderQueue::findItem(const std::string& jobId) noexcept {
    const auto scan = [&](auto& container) -> Item* {
        for (auto& item : container) {
            if (item->job.id == jobId) return item.get();
        }
        return nullptr;
    };
    if (Item* item = scan(active_)) return item;
    return scan(pending_);
}

const RenderQueue::Item* RenderQueue::findItem(const std::string& jobId) const noexcept {
    return const_cast<RenderQueue*>(this)->findItem(jobId);
}

std::size_t RenderQueue::findResult(const std::string& jobId) const noexcept {
    for (std::size_t i = 0; i < results_.size(); ++i) {
        if (results_[i].jobId == jobId) return i;
    }
    return results_.size();
}

void RenderQueue::pump() {
    if (pumping_) return;  // a runner already holds this loop on its stack
    pumping_ = true;
    while (!pending_.empty() && static_cast<int>(active_.size()) < maxConcurrent_) {
        auto item = std::move(pending_.front());
        pending_.pop_front();
        Item* raw = item.get();
        // Registered *before* the handout. DownloadQueue pushes after, and gets
        // away with it only because its `startItem` failure path cannot call back
        // into the queue; here the runner is arbitrary user code and may complete
        // the job reentrantly, which must not find itself absent from `active_`.
        active_.push_back(std::move(item));
        runJob(*raw);
    }
    pumping_ = false;
    checkIdle();
}

void RenderQueue::runJob(Item& item) {
    item.cancelRequested = false;
    setState(item, RenderState::Rendering);
    if (item.job.receipt.startedUtc.empty()) item.job.receipt.startedUtc = utcNow();
    publishProgress(item, results_[item.resultIndex].framesDone);

    if (!runner_) {
        settle(item.job.id, item, RenderState::FailedPermanent, 0, 0, 0,
               RenderFailureKind::Permanent, "no job runner installed");
        return;
    }
    // By value, and the runner owns its copy: this stack frame may hold a
    // dangling reference the instant the runner returns, because the runner is
    // free to complete the job (and therefore erase it) before it does.
    runner_(item.job);
}

void RenderQueue::setState(Item& item, const RenderState state) {
    item.state = state;
    results_[item.resultIndex].state = state;
    emit jobStateChanged(QString::fromStdString(item.job.id), static_cast<int>(state),
                         results_[item.resultIndex].progressPercent());
}

void RenderQueue::publishProgress(Item& item, const u64 framesDone) {
    RenderResult& slot = results_[item.resultIndex];
    slot.framesDone = std::max(slot.framesDone, framesDone);
    if (item.state == RenderState::Rendering) {
        emit jobStateChanged(QString::fromStdString(item.job.id),
                             static_cast<int>(RenderState::Rendering),
                             slot.progressPercent());
    }
    emitBatchProgress();
}

void RenderQueue::settle(const std::string jobId, Item& item, const RenderState state,
                         const u64 framesWritten, const u64 bytesWritten,
                         const u64 elapsedMs, const RenderFailureKind failureKind,
                         std::string note) {
    RenderResult& slot = results_[item.resultIndex];
    slot.state = state;
    slot.framesWritten = framesWritten;
    slot.bytesWritten = bytesWritten;
    slot.elapsedMs = elapsedMs;
    slot.failureKind = failureKind;
    slot.note = std::move(note);
    // A settled job counts as done work: the aggregate must not stall at 90% on a
    // batch whose last job produced every frame it promised.
    slot.framesDone = std::max(slot.framesDone, framesWritten);

    item.state = state;
    item.job.receipt.outcome = state;
    item.job.receipt.framesWritten = framesWritten;
    item.job.receipt.bytesWritten = bytesWritten;
    item.job.receipt.elapsedMs = elapsedMs;
    item.job.receipt.note = std::move(note);
    if (item.job.receipt.finishedUtc.empty()) item.job.receipt.finishedUtc = utcNow();

    // The receipt is the point of the exercise, and a failure to write one must
    // not fail a render that already succeeded -- but it is loud, because a
    // silently missing receipt is a re-render nobody can plan.
    if (auto written = writeReceipt(item.job, item.job.receiptPath()); !written) {
        LOG_ERROR("RenderQueue: receipt for {} not written: {}", jobId,
                  written.error().describe());
    }

    emit jobStateChanged(QString::fromStdString(jobId), static_cast<int>(state),
                         slot.progressPercent());

    // Last, and by the by-value id: this destroys `item`.
    eraseEverywhere(jobId);
    emitBatchProgress();
    pump();
}

void RenderQueue::eraseEverywhere(const std::string& jobId) noexcept {
    const auto matches = [&jobId](const auto& ptr) { return ptr->job.id == jobId; };
    active_.erase(std::remove_if(active_.begin(), active_.end(), matches), active_.end());
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), matches),
                   pending_.end());
}

void RenderQueue::checkIdle() {
    const bool idleNow = isEmpty();
    if (idleNow && !idleEmitted_) {
        idleEmitted_ = true;
        emit queueIdle();
    } else if (!idleNow) {
        idleEmitted_ = false;
    }
}

void RenderQueue::emitBatchProgress() {
    const BatchSummary summary = summarise(results_);
    const int percent = vc::batchProgressPercent(results_);
    if (percent == lastBatchPercent_) return;
    lastBatchPercent_ = percent;
    emit batchProgressChanged(static_cast<int>(summary.settled()),
                              static_cast<int>(summary.total), percent);
}

} // namespace vc
