#pragma once
/**
 * @file RenderQueue.hpp
 * @file Purpose: bounded-concurrency scheduling for batch video renders.
 *
 * @section The seam: job handout, not direct render
 * This queue knows nothing about OpenGL, libav, or `AudioFileDecoder`. It hands
 * a job to an injected `JobRunner` and then waits to be told what happened. That
 * is a deliberate divergence from `DownloadQueue`, whose `ReplyFactory` seam is
 * still a *direct* action: `nam_->get(request)` happens inside the queue, so
 * the queue owns the socket, the timeout and the `.part` file.
 *
 * Handout was chosen because of three facts, not for symmetry:
 *
 *  1. **The unknown belongs to someone else.** The blocking question for offline
 *     rendering is whether a GL context current on a background thread behaves on
 *     macOS (CGL / `NSOpenGLContext` thread affinity). A queue that spawned the
 *     thread would have to own that answer -- the thread's affinity, the QPA
 *     integration, the teardown order -- and it is exactly what the spike has to
 *     answer first. A handout queue has no opinion, so the spike's answer is a
 *     change to one `std::function` and not to this file.
 *  2. **The repo has already been bitten by a queue that blocks.** The
 *     credential-restore test holds a worker thread in a blocking wait, and a
 *     failing assertion before the release turns into a 300 s timeout and a
 *     SIGABRT that buries the real message. A queue whose runner returns
 *     promptly cannot be turned into that failure mode by any assertion.
 *  3. **It is what makes the whole thing testable at all.** With handout, the
 *     suite runs a plain lambda: no thread, no GL, no FFmpeg, and every assertion
 *     is an exact value. That is not a testing convenience bolted on afterwards;
 *     it is the reason the concurrency ceiling can be *proved* rather than
 *     asserted by inspection.
 *
 * The cost of handout is that the executor owns interruption, so `cancel()` needs
 * a way out. Both mechanisms are provided and neither is assumed: a `CancelHook`
 * the executor installs if it can be interrupted, and a polled
 * `isCancelRequested()` for one that cannot.
 *
 * @section Why bounded concurrency is mandatory, and why the default is 2
 * This was written as "2, not DownloadQueue's 3, because one job costs a GL
 * context and an 8.3 MB per-frame readback, so two put a megabyte-scale
 * allocator on the critical path twice over." That was an argument, not a
 * measurement, and OffscreenRenderSpike has now measured it: on macOS the
 * ceiling is **1**, because there is no off-main-thread drawable at all and a
 * second job cannot have a context on a worker thread. The allocator was never
 * the binding constraint.
 *
 * The limit is a real ceiling, not a hint -- `pump()` is the only thing that
 * fills slots and it checks before every handout.
 *
 * @section Why there is no backoff ladder
 * `DownloadQueue`'s ladder (1s / 4s / 16s with jitter) exists because its
 * failures are *network*: a server rate-limits, so waiting is the correct
 * response, and jitter stops parallel retries from re-colliding. None of that
 * transfers:
 *
 *  - The dominant transient failure here is a lost GL context, and the fix is a
 *    **fresh context**, not a wait. Sleeping does not repair a driver.
 *  - A retry is not cheap. A three-minute render that fails at 2:59 costs three
 *    minutes; with `kMaxAttempts == 2` the worst case is bounded at twice the
 *    wall clock of the failing job, which is predictable enough to state.
 *  - Immediate re-entry cannot stampede, because concurrency is already bounded:
 *    at most `kMaxAttempts * maxConcurrent` attempts exist at any instant, and
 *    `pump()` is the only path to a handout.
 *  - `ENOSPC` is not retryable at all. `classifyRenderFailure` sends it to
 *    `Permanent`, so "the disk is full" fails on the first attempt instead of
 *    occupying a slot for a minute to fail identically.
 *
 * So there are **two** containers, not three: `pending_` (FIFO) and `active_`.
 * A retry re-enters at the FIFO *front*, because the job that just burned three
 * minutes is the one the user is waiting on.
 */

#include "recorder/RenderJob.hpp"

#include <QObject>
#include <QString>

#include <deque>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace vc {

// ─────────────────────────────────────────────────────────────────────────────
// Failure taxonomy
// ─────────────────────────────────────────────────────────────────────────────

/// What the scheduler should do about a reported failure.
enum class RenderFailureKind : u8 {
    /// Not a failure.
    None = 0,
    /// The user stopped it. Terminal and **not** retryable -- see
    /// `RenderState::Cancelled`.
    Cancelled = 1,
    /// Worth exactly one more attempt, immediately.
    Transient = 2,
    /// Another attempt cannot help. The slot is released and the job is done.
    Permanent = 3,
};

[[nodiscard]] std::string_view renderFailureKindName(RenderFailureKind kind) noexcept;

/// One errno rule in `kErrnoRules`. A named table entry rather than a bare pair
/// so the table itself is documentation, and so a test can walk it.
struct RenderErrnoRule {
    int osErrno;
    RenderFailureKind kind;
    /// Why this errno is what it is. Read by a human adding the next entry.
    const char* why;
};

/// The pure classification table. Platform-dependent errno constants are guarded
/// because `EDQUOT` and `EROFS` do not exist on the Windows CRT.
extern const std::vector<RenderErrnoRule>& renderErrnoRules();

/// What a render worker reports when it stops early.
struct RenderFailure {
    /// The worker's own read of the situation. Used only when `osErrno == 0`.
    RenderFailureKind kind{RenderFailureKind::Transient};
    /// errno or an AVERROR, or 0 when the failure did not come from the OS.
    int osErrno{0};
    /// Never empty. Free text for the receipt's `note`.
    std::string reason;
};

/// Pure: how the scheduler should treat a reported failure.
///
/// `osErrno` **overrides** the worker's own `kind`, because a worker that reports
/// `ENOSPC` has told us the truth about the disk regardless of what it guessed.
/// The override is the whole reason this is a function and not an enum mapping:
/// the errno table is where the domain knowledge lives.
[[nodiscard]] RenderFailureKind classifyRenderFailure(const RenderFailure& failure);

// ─────────────────────────────────────────────────────────────────────────────
// Batch reporting
// ─────────────────────────────────────────────────────────────────────────────

/// One slot of the pre-allocated batch array.
///
/// Indexed by **enqueue position**, never by completion order, which is the
/// shape `suno-power-exporter`'s `downloadBatch` uses and the reason its results
/// line up regardless of which download finished first. It is also why
/// `framesDone`/`framesExpected` live here and not on the private item: the whole
/// batch aggregate is a fold over this array, so there is exactly one source of
/// truth for "how far along is the batch".
struct RenderResult {
    std::string jobId;
    RenderState state{RenderState::Queued};
    u64 framesDone{0};
    u64 framesExpected{0};
    u64 framesWritten{0};
    u64 bytesWritten{0};
    u64 elapsedMs{0};
    RenderFailureKind failureKind{RenderFailureKind::None};
    /// The receipt's note: the diagnosis a human can read.
    std::string note;

    /// 0..100 while the denominator is known, **-1 when it is not**. The -1 is
    /// the same "unknown" convention `DownloadQueue` uses for a reply that has
    /// not reported a total, and it is deliberately not 0: "0% of nothing" and
    /// "0% of a lot" are different claims.
    [[nodiscard]] int progressPercent() const noexcept;
};

/// Honest aggregate. Every field is a count, and `accountedFor()` is the check
/// that the counts add up -- a summary that does not account for every slot is
/// the "short list presented as a total" failure this whole batch model exists to
/// prevent.
struct BatchSummary {
    u32 total{0};
    u32 completed{0};
    u32 completedPartial{0};
    u32 failedRetryable{0};
    u32 failedPermanent{0};
    u32 cancelled{0};
    /// Still queued or rendering.
    u32 outstanding{0};

    /// Terminal slots.
    [[nodiscard]] u32 settled() const noexcept;
    /// `settled() + outstanding()`. Must equal `total`; asserted by the suite on
    /// a batch where some jobs fail, because the arithmetic is the point.
    [[nodiscard]] u32 accountedFor() const noexcept;
    /// Jobs that reached the file whole. The number a user means by "done".
    [[nodiscard]] u32 fullyRendered() const noexcept;
    /// Everything that did **not** reach the file whole: partial, failed and
    /// cancelled alike. This is "how many", and `RenderQueue::idsNotFullyRendered`
    /// is "which".
    [[nodiscard]] u32 notFullyRendered() const noexcept;
    /// Every slot is terminal and there was at least one.
    [[nodiscard]] bool isSettled() const noexcept;
};

/// Pure fold over the batch array. Free function so the honest-aggregate claim is
/// testable without a queue, a runner, or a filesystem.
[[nodiscard]] BatchSummary summarise(std::span<const RenderResult> results);

/// Frame-weighted batch progress, 0..100, or -1 when no slot declares a
/// denominator.
///
/// Weighted by frames rather than by job count, and the difference is not
/// cosmetic: with one 1800-frame job and three 10-frame jobs, "1 of 4 jobs done"
/// reads as 25% while 1800 of 1830 frames -- 98% of the work -- are already on
/// disk. A percentage that lies about effort is worse than no percentage.
[[nodiscard]] int batchProgressPercent(std::span<const RenderResult> results);

// ─────────────────────────────────────────────────────────────────────────────
// The queue
// ─────────────────────────────────────────────────────────────────────────────

/// Handed one job the moment a slot frees. **Must return promptly** -- the real
/// executor posts the render to its own thread and reports back through
/// `RenderQueue::reportProgress` / `finish` / `fail`.
///
/// The job arrives **by value** and the executor is expected to keep it: the
/// queue may destroy its own copy the instant this returns, including
/// reentrantly, if the runner completes the job before returning.
using JobRunner = std::function<void(RenderJob job)>;

/// Optional out-of-band interruption for a job already handed out, e.g.
/// `avcodec` interrupt callbacks or `QThread::requestInterruption`. Installed by
/// the executor; without it `cancel()` is a polled flag instead.
using CancelHook = std::function<void(const std::string& jobId)>;

/// Bounded-concurrency FIFO scheduler for render jobs, with a durable receipt per
/// job and an aggregate that accounts for every slot.
class RenderQueue : public QObject {
    Q_OBJECT

public:
        /// ONE, and that is a measurement rather than a preference.
///
/// It was 2, chosen from an *argument*: one job costs a GL context, an FFmpeg
/// encoder with its own thread, a demuxer, and a per-frame full-frame readback,
/// and two of those were thought to be a fair trade on a four-core box. The
/// argument was never tested. `src/recorder/OffscreenRenderSpike` now has, and
/// it refutes the premise on macOS: there is **no off-main-thread drawable at
/// all**, so a second job cannot have its own context on a worker thread and the
/// ceiling is 1 regardless of how much RAM the readback costs.
///
/// The measured detail that matters, because it is the expensive kind of wrong:
/// this is not thread *affinity*. A context created on a worker thread is fine
/// (raw CGLSetCurrentContext succeeds, GL 4.1); what fails is that framebuffer 0
/// is GL_FRAMEBUFFER_UNDEFINED, because every mechanism that used to give a
/// worker a drawable is gone -- CGLCreatePBuffer (deprecated 10.3, removed
/// 10.7), QWindow::create() from a worker (NSInternalInconsistencyException),
/// Qt's own makeCurrent(QWindow*) guard, raw NSOpenGLContext setView: (SIGILL
/// inside AppKit). QOffscreenSurface makes a working *context* with no colour
/// buffer.
///
/// So the shape is: one long-lived context and one long-lived pm::Engine on the
/// GUI thread, with N jobs interleaving frames through it, each keeping its own
/// encoder. Parallelism moves from contexts to encoders, and this ceiling bounds
/// the encoders, not the GL.
///
/// A platform where a worker CAN have a drawable is what the ceiling exists for:
/// set it from offscreenVerdictMaxConcurrency(), which returns -1 for Supported
/// precisely so that no ceiling is invented until the frame-cost pass measures
/// one. Raise this when that platform exists, and do not raise it because a
/// machine has spare cores.
static constexpr int kDefaultMaxConcurrent = 1;
    /// One retry, immediately. Not a ladder; see the file header on why.
    static constexpr int kMaxAttempts = 2;

    /// Requires a runner. There is no "no runner yet" mode: a queue that accepted
    /// jobs it could never start would report a stall with no cause, which is the
    /// `SunoWorkspace` registration trap in a different costume.
    explicit RenderQueue(JobRunner runner, QObject* parent = nullptr);
    ~RenderQueue() override;

    RenderQueue(const RenderQueue&) = delete;
    RenderQueue& operator=(const RenderQueue&) = delete;

    void setMaxConcurrent(int maxConcurrent);
    [[nodiscard]] int maxConcurrent() const noexcept { return maxConcurrent_; }

    void setMaxAttempts(int maxAttempts);
    [[nodiscard]] int maxAttempts() const noexcept { return maxAttempts_; }

    void setCancelHook(CancelHook hook);

    /// Admit a job. The slot is allocated **before** the job is pumped, so the
    /// batch array stays in enqueue order no matter when the job finishes.
    ///
    /// Returns the state the job is in when this returns -- `Queued` if the queue
    /// is full, `Rendering` if a slot was free, because `enqueue` pumps and the
    /// runner is already called by the time control comes back. Returns the
    /// `JobError` verbatim for a job that fails `RenderJob::validate()` or whose
    /// id is already in this batch.
    std::expected<RenderState, JobError> enqueue(RenderJob job);

    /// Ask for a job to stop. Queued jobs are removed immediately; in-flight jobs
    /// are marked and the executor is interrupted if it installed a hook.
    ///
    /// Returns false for an unknown or already-terminal id, exactly as
    /// `DownloadQueue::cancel` does. A cancel request is a *request*: an executor
    /// that reports a successful finish before it notices keeps `Completed`,
    /// because the worker's report is the ground truth about what reached the
    /// file and overriding it would misreport a complete file.
    bool cancel(const std::string& jobId);

    /// Whether `cancel()` was asked for this id. For an executor with no
    /// `CancelHook`, this is polled between frames.
    [[nodiscard]] bool isCancelRequested(const std::string& jobId) const noexcept;

    /// Report decoded frames. Cheap and idempotent; a non-monotonic value is
    /// ignored rather than clamped, because a report that went backwards means
    /// the executor is confused and silently accepting it would corrupt the batch
    /// aggregate.
    bool reportProgress(const std::string& jobId, u64 framesDone);

    /// Report a finished render. `receipt.framesWritten` and `bytesWritten` are
    /// taken as reported; the **state is decided here**, from the frames the job
    /// expected, so a worker that claims `Completed` while writing 400 of 1800
    /// frames lands in `CompletedPartial` instead of certifying a lie. A receipt
    /// with a non-terminal outcome is rejected.
    bool finish(const std::string& jobId, RenderReceipt receipt);

    /// Report a failure. Classified by `classifyRenderFailure`; a transient
    /// failure with attempts left is re-queued at the FIFO front, anything else
    /// settles.
    bool fail(const std::string& jobId, RenderFailure failure);

    [[nodiscard]] bool isEmpty() const noexcept {
        return pending_.empty() && active_.empty();
    }
    [[nodiscard]] int activeCount() const noexcept {
        return static_cast<int>(active_.size());
    }
    [[nodiscard]] int queuedCount() const noexcept {
        return static_cast<int>(pending_.size());
    }

    [[nodiscard]] std::size_t batchSize() const noexcept { return results_.size(); }
    [[nodiscard]] std::span<const RenderResult> results() const noexcept {
        return results_;
    }
    [[nodiscard]] std::span<const RenderResult> results() noexcept { return results_; }
    [[nodiscard]] BatchSummary summary() const { return summarise(results_); }
    [[nodiscard]] int batchProgressPercent() const {
        return vc::batchProgressPercent(results_);
    }

    /// Stable 1-based index in enqueue order, or 0 when the id is unknown.
    /// Stable is the operative word: a retry re-enters at the FIFO front, so the
    /// *queue* position of a live job changes while this does not.
    [[nodiscard]] std::size_t batchPosition(const std::string& jobId) const noexcept;

    /// Live jobs ahead of this one in `pending_`. 0 for a job that is not queued,
    /// including one that is running.
    [[nodiscard]] int waitingAhead(const std::string& jobId) const noexcept;

    [[nodiscard]] const RenderJob* job(const std::string& jobId) const noexcept;
    [[nodiscard]] RenderState stateOf(const std::string& jobId) const noexcept;

    /// Ids of every slot that did not reach the file whole, in enqueue order.
    /// "Which", to `BatchSummary::notFullyRendered`'s "how many" -- a batch
    /// report that can only say "3 of 10 done" is the failure mode this exists to
    /// prevent, because it cannot distinguish a settled batch from one still
    /// running.
    [[nodiscard]] std::vector<std::string> idsNotFullyRendered() const;

    /// Drop the accumulated batch and start a new one. Live jobs are untouched,
    /// and a job already in flight keeps writing into its slot.
    ///
    /// COMPACTING, not truncating, and the distinction is a memory-safety one.
    /// `Item::resultIndex` is stamped once at enqueue and several sites index
    /// `results_[item->resultIndex]` unchecked, so emptying the vector outright
    /// left every live job holding an index into freed storage -- an
    /// out-of-bounds read followed by an out-of-bounds write on the next progress
    /// report. Only settled entries are dropped, and the surviving slots are
    /// renumbered in place. A batch therefore continues from the jobs still
    /// running rather than from zero, which is the only place it can honestly
    /// continue from.
    void clearBatch();

signals:
    /// Per-job state and progress feed. `state` is a `RenderState` ordinal,
    /// `progressPercent` 0..100 or -1 when unknown.
    void jobStateChanged(const QString& jobId, int state, int progressPercent);
    /// Batch aggregate. `settled` counts terminal slots, `total` is the batch
    /// size, `percent` is frame-weighted or -1.
    void batchProgressChanged(int settled, int total, int percent);
    /// Emitted once whenever the queue drains, edge-triggered on
    /// `DownloadQueue::checkIdle`'s flag so an already-idle queue stays silent.
    void queueIdle();

private:
    struct Item {
        RenderJob job;
        RenderState state{RenderState::Queued};
        /// Attempts already consumed (failures), zero-based index of the *next*
        /// attempt minus one. `kMaxAttempts` attempts total.
        int attempts{0};
        bool cancelRequested{false};
        /// Index into `results_`. Stored rather than looked up so a retry never
        /// has to re-search for its own slot.
        std::size_t resultIndex{0};
    };

    [[nodiscard]] Item* findItem(const std::string& jobId) noexcept;
    [[nodiscard]] const Item* findItem(const std::string& jobId) const noexcept;
    /// Find in `results_`, which is the duplicate-id authority: a live job always
    /// has a slot, so checking the array covers both cases at once.
    [[nodiscard]] std::size_t findResult(const std::string& jobId) const noexcept;

    void pump();
    void runJob(Item& item);
    void setState(Item& item, RenderState state);
    void publishProgress(Item& item, u64 framesDone);
    /// Fill the batch slot, write the receipt, release the slot, pump.
    ///
    /// Takes the id **by value** and erases from *both* `active_` and `pending_`
    /// as its last act. Both details are load-bearing: erasing destroys the
    /// `Item`, so an id borrowed from `item.job.id` would dangle for the rest of
    /// the function -- which is a use-after-free this class had once, caught by
    /// the probe that drives `cancel()` on a queued job. Doing the erase here
    /// also means no caller has to know which container its job was in.
    void settle(std::string jobId, Item& item, RenderState state, u64 framesWritten,
                u64 bytesWritten, u64 elapsedMs, RenderFailureKind failureKind,
                std::string note);
    void eraseEverywhere(const std::string& jobId) noexcept;
    void checkIdle();
    void emitBatchProgress();

    JobRunner runner_;
    CancelHook cancelHook_;
    int maxConcurrent_{kDefaultMaxConcurrent};
    int maxAttempts_{kMaxAttempts};
    bool idleEmitted_{true};
    /// Reentrancy guard. A runner may report a job finished from inside the
    /// handout, and settling calls `pump()`; without this the next job would be
    /// handed out while the previous runner is still on the stack. DownloadQueue
    /// gets the same guarantee by accident -- its `startItem` failure path cannot
    /// call back -- and this one needs it on purpose.
    bool pumping_{false};
    int lastBatchPercent_{-2};

    std::deque<std::unique_ptr<Item>> pending_;
    std::vector<std::unique_ptr<Item>> active_;
    /// Pre-allocated by enqueue order. The batch's single source of truth.
    std::vector<RenderResult> results_;
};

} // namespace vc
