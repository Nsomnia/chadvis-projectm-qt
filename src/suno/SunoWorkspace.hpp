#pragma once
// SunoWorkspace.hpp — the LOCAL half of a creation session: authored text, region
// edits, and the submission of a render to the batch pipeline.
//
// ─────────────────────────────────────────────────────────────────────────────
// What changed, and why this file shrank from 145 lines to fewer
// ─────────────────────────────────────────────────────────────────────────────
// This class used to carry 12 signals and **6 of them had no emitter anywhere**:
// generationCompleted, generationFailed, stemsReady, renderCompleted, renderFailed
// and errorOccurred. `startRender` set `isRendering = true`, emitted
// `renderProgress(0.0f, "initializing")` once, and was then silent forever, so a
// registered UI would have shown a Render button pinned at 0% and a Generate
// button spinning permanently. Every one of those is gone rather than stubbed,
// because a signal emitted from a timer to look wired is the exact defect that put
// this class on the backlog.
//
// **What survives is what has a real producer.** Four things, and each names its
// producer in a comment below:
//
//   1. Prompt, tags and model — authored locally, persisted, nothing else claims
//      to act on them (see `startGeneration`, which REFUSES).
//   2. Regions — added, removed and cleared, each mutation notifies.
//   3. Lyrics text — set and read, persisted.
//   4. Render submission — a real `RenderJob` handed to a real `RenderQueue`,
//      and every progress / completion / failure signal translated from a queue
//      event that actually happened.
//
// ─────────────────────────────────────────────────────────────────────────────
// Why render is real now, and why the only reachable outcome today is a failure
// ─────────────────────────────────────────────────────────────────────────────
// `RenderExecutor::jobRunner()` was measured and it settles a job it cannot start
// rather than leaving it in flight: with no `RenderFrameBackend` it calls
// `queue_->fail(id, {Permanent, 0, "no RenderFrameBackend: offline rendering has
// no drawable owner in this build"})` inside the handout. There is no backend
// implementation in the tree (`RenderExecutor.hpp` @section What is not here), so
// on this machine the honest end state of a render submitted through this class
// is `FailedPermanent` with that sentence as its reason.
//
// That is a *better* outcome than the one it replaces, and the difference is worth
// stating: the old class reported a render that had begun and never ended. This
// one reports a render that was refused, with the reason, in the same turn it was
// submitted. Nothing here fabricates a frame, a clock tick, or a percentage.
//
// **The corollary is a constraint on the surface:** `renderCompleted` is emitted
// from exactly one place, the translation of a `RenderState::Completed` the queue
// decided. It is currently unreachable on this platform, and that is a fact about
// the platform rather than a hole in this class — which is the difference between
// the two that is worth the whole change. `RenderFrameBackend` existing turns it
// live with no edit here.
//
// ─────────────────────────────────────────────────────────────────────────────
// Why RenderJob, and not this class, owns the render parameters
// ─────────────────────────────────────────────────────────────────────────────
// The old `RenderMode` enum plus `renderDuration` / `renderIncludeVideo` /
// `renderIncludeKaraoke` were a second, lossy schema for what `RenderJob` already
// describes: its `karaokeMode` vocabulary is frozen at three tokens, its
// container/codec pair covers the video half, and `expectedFrames` is the honest
// progress denominator. `RenderJob.hpp` states the rule its whole design exists to
// enforce — *the first schema wins forever* — so keeping a parallel set of knobs
// here is the drift it warns about, not an extension of it. They are deleted.
//
// The consequence is a signature change: `startRender` takes the job the caller
// already built, because the caller is the only party that knows the audio file,
// the output path and the preset names, and inventing them here would be a
// fabrication rather than a default.
//
// ─────────────────────────────────────────────────────────────────────────────
// Why generation refuses rather than half-starting
// ─────────────────────────────────────────────────────────────────────────────
// Three independent reasons, so no future session re-litigates them:
//   * `SunoClient` has **no generation method at all**. There is nothing here to
//     call, so a `generationStarted` emission is the start of a wait that can
//     never end.
//   * `GENERATE` and `COWRITE_LYRICS` are route constants nothing references.
//   * Whether generation is ever wired is a **human decision**, not an engineering
//     one: the provider behind `/api/c/check` is uncaptured in the master
//     inventory and the standing backlog position is browser hand-off rather than
//     an automated solve. See the human-decisions section of `TODO.md`.
//
// So `startGeneration` records what the user authored — which is real local state
// and is persisted — and returns a named refusal. It emits nothing.
// ─────────────────────────────────────────────────────────────────────────────
// Why `errorOccurred` is deleted rather than wired
// ─────────────────────────────────────────────────────────────────────────────
// One broadcast string consumed by every subsystem is the recorded cause of a live
// bug elsewhere in this tree: `SunoController` fans `SunoClient::errorOccurred`
// into both `sunoError` and `libraryFetchFailed`, so a blocked notification route
// clears the library spinner. A refusal that cannot be delivered to the right
// consumer is the same defect in embryo. Every refusal here is a **return value**,
// which a caller either reads or ignores deliberately.

#include "recorder/RenderJob.hpp"
#include "recorder/RenderQueue.hpp"
#include "util/Types.hpp"

#include <QObject>
#include <QString>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc::suno {

// ─────────────────────────────────────────────────────────────────────────────
// Refusals
// ─────────────────────────────────────────────────────────────────────────────

/// Why a request was refused. Named rather than a bare bool so a caller can
/// branch on the class and a test can assert the class.
enum class WorkspaceRefusalKind : u8 {
    /// No `RenderQueue` attached, so there is nowhere to schedule a render.
    NoQueueAttached = 0,
    /// The `RenderJob` was refused by `RenderJob::validate()` or as a duplicate.
    JobRejected = 1,
    /// This class renders one job at a time and one is already live.
    RenderAlreadyActive = 2,
    /// Generation is not wired, and the decision to wire it is not an engineer's.
    GenerationUnavailable = 3,
};

[[nodiscard]] std::string_view workspaceRefusalKindName(WorkspaceRefusalKind kind) noexcept;

struct WorkspaceRefusal {
    WorkspaceRefusalKind kind{WorkspaceRefusalKind::NoQueueAttached};
    /// The offending thing, spelled the way its owner spells it — `"render queue"`,
    /// or `JobError::field` for a rejected job — so a message never has to
    /// invent a second name for the same subject.
    std::string subject;
    /// Never empty.
    std::string message;

    [[nodiscard]] std::string describe() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────

/// A trimmed region of a clip, for a mashup or an edit.
///
/// This is the one piece of creation state with no server round trip: it is
/// authored locally, it is persisted, and it is fully round-tripped by
/// `saveWorkspace` / `loadWorkspace`.
struct ClipRegion {
    std::string clipId;
    double startSeconds{0.0};
    double endSeconds{0.0};
    std::string label;
};

/// The local workspace state for one creation session.
///
/// Every field here is **written by something in this file** and read by
/// `saveWorkspace` or a getter. The three that were never written —
/// `generatedClips`, `stems`, `alignedLyricsJson` — and the two booleans nothing
/// ever set — `isGenerating`, `isEditing` — are gone rather than left as
/// always-default members a future reader has to disprove.
struct WorkspaceState {
    std::string currentPrompt;
    std::string currentTags;
    std::string currentModel{"chirp-v3.5"};
    bool makeInstrumental{false};

    std::vector<ClipRegion> regions;
    std::string lyricsText;

    /// The reason the most recent refusal or render failure gave, or empty after
    /// a success. Set from exactly one place per path, which is what makes it
    /// readable by a QML surface that cannot see a return value.
    std::string lastError;
};

// ─────────────────────────────────────────────────────────────────────────────
// The class
// ─────────────────────────────────────────────────────────────────────────────

/// Local song-creation state, and the submission of a render to `RenderQueue`.
///
/// It renders nothing itself and owns no clock. The only producer of video frames
/// in this tree is `RenderExecutor`, and the only producer of a *verdict* is
/// `RenderQueue`; this class is the translator between them and the local state a
/// user can see.
class SunoWorkspace : public QObject {
    Q_OBJECT

public:
    explicit SunoWorkspace(QObject* parent = nullptr);
    ~SunoWorkspace() override;

    SunoWorkspace(const SunoWorkspace&) = delete;
    SunoWorkspace& operator=(const SunoWorkspace&) = delete;

    /// Inject the queue. **Not owned** and must outlive this object.
    ///
    /// Injected rather than constructed for the same reason `RenderExecutor` takes
    /// one: the queue owns the runner, the batch array and the concurrency
    /// ceiling, all of which a batch GUI also needs. Two queues means two batches
    /// and two progress answers.
    void setQueue(RenderQueue* queue) noexcept;
    [[nodiscard]] RenderQueue* queue() const noexcept { return queue_; }

    // ── Generation: refused, always ─────────────────────────────────────────
    /// Record an authored prompt, and **refuse** to generate.
    ///
    /// Always returns `GenerationUnavailable`. Records `prompt`, `tags`,
    /// `makeInstrumental` and `model` first, because those are real local state
    /// and `saveWorkspace` persists them — but emits no signal and starts
    /// nothing. See the file header for the three reasons.
    std::expected<void, WorkspaceRefusal> startGeneration(const std::string& prompt,
                                                          const std::string& tags,
                                                          bool makeInstrumental,
                                                          const std::string& model);

    // ── Regions: real ──────────────────────────────────────────────────────
    /// Ignores a region whose `startS >= endS`, and says so in the log. An empty
    /// or inverted range adds a row to a mashup timeline that no amount of
    /// rendering can ever use.
    void addRegion(const std::string& clipId, double startS, double endS,
                   const std::string& label = {});
    /// Removes the region at `index` if it exists, then notifies. The
    /// notification is the point: the old `removeRegion` mutated a list whose
    /// every other mutation announced itself, so a QML list bound to it would
    /// have kept showing a region that was gone.
    void removeRegion(size_t index);
    /// No-op, and silent, on an already-empty list — there is no state change to
    /// announce, and a `regionsChanged` for an identical list is noise a list
    /// model would have to diff away.
    void clearRegions();
    [[nodiscard]] std::span<const ClipRegion> regions() const;

    // ── Lyrics: real ───────────────────────────────────────────────────────
    void setLyricsText(const std::string& text);
    [[nodiscard]] std::string_view lyricsText() const noexcept { return state_.lyricsText; }

    // ── Render: real, through RenderQueue ──────────────────────────────────
    /// Submit `job` to the attached queue and report what the queue decided.
    ///
    /// Returns before the render finishes — `RenderQueue::enqueue` pumps, so the
    /// job is already in flight — and the outcome arrives as `renderCompleted` or
    /// `renderFailed`. A refusal is the returned `WorkspaceRefusal`, except for a
    /// job `RenderQueue` rejects *after* admitting this object to the render, which
    /// is reported as `renderFailed` because a render was genuinely attempted.
    ///
    /// Note it takes the job **by value**: `RenderQueue`'s handout contract is
    /// that the runner keeps its copy and the queue may destroy its own the instant
    /// the handout returns, so nothing here may borrow from it afterwards.
    std::expected<void, WorkspaceRefusal> startRender(RenderJob job);

    /// Ask the queue to cancel the live render. Returns false when nothing is
    /// live. A cancel is a *request* — the queue decides the outcome, because the
    /// worker's report is the ground truth about what reached the file.
    bool cancelRender();

    [[nodiscard]] bool isRendering() const noexcept { return activeRenderId_.has_value(); }
    /// Empty unless a render is live.
    [[nodiscard]] std::optional<std::string_view> activeRenderJobId() const noexcept {
        if (!activeRenderId_.has_value()) return std::nullopt;
        return std::string_view(*activeRenderId_);
    }
    /// 0..100, or **-1 when no denominator is known** — the same convention
    /// `RenderResult::progressPercent` uses, and deliberately not 0: "0% of
    /// nothing" and "0% of a lot" are different claims. Reset to -1 by every
    /// `startRender`.
    [[nodiscard]] int renderProgressPercent() const noexcept { return lastRenderPercent_; }

    // ── State ──────────────────────────────────────────────────────────────
    [[nodiscard]] const WorkspaceState& state() const noexcept { return state_; }

    // ── Persistence ────────────────────────────────────────────────────────
    /// Save workspace state to JSON for later resume.
    ///
    /// `std::expected`, not `vc::Result`: this was the last `Result` in this file
    /// and `AGENTS.md` §7 prefers the standard monadic type. `expected` also has
    /// `operator bool`, so a caller that only checked success still compiles.
    /// The error is a message string because a file-open failure has no taxonomy
    /// worth the ceremony.
    std::expected<void, std::string> saveWorkspace(const fs::path& path);
    /// Load a previously-saved workspace. Absent and corrupt are distinct
    /// errors, so "nothing was saved" can never read as "it failed to load".
    std::expected<void, std::string> loadWorkspace(const fs::path& path);

signals:
    void regionAdded(const ClipRegion& region);
    void regionRemoved(size_t index);
    void regionsCleared();
    void lyrics_changed(const std::string& text);

    /// The job id, not a `RenderMode`: the id is what a receipt, a cancel and a
    /// progress report are all keyed on, and a second schema for "which render"
    /// would be one more thing to keep in step.
    void renderStarted(const std::string& jobId);
    /// `percent` is 0..100 or -1 for unknown, carried in the queue's own unit.
    /// The old signal used an `f32` fraction while the queue reports an integer
    /// percent, and two conventions for one bar is how a progress bar starts
    /// lying. `stage` is `renderStateName`, never a fabricated description.
    void renderProgress(int percent, const std::string& stage);
    void renderCompleted(const fs::path& outputPath);
    void renderFailed(const std::string& error);

private slots:
    /// Translation point for every render verdict. Ignores any job id that is not
    /// the one this class submitted, because the queue is shared with the batch
    /// GUI and a foreign job's progress is not this object's business.
    void onQueueStateChanged(const QString& jobId, int state, int percent);

private:
    /// Push a percentage out, never backwards, and pass -1 through untouched.
    ///
    /// The guard is not paranoia: a progress bar that goes 40% → 10% reads as a
    /// stall, and the queue's own monotonicity is per-`Item` while this is
    /// per-object across a submit/cancel/submit cycle.
    void publishProgress(int percent, const std::string& stage);

    /// Settle a live render as failed: clear the id, record the reason, emit.
    void settleFailed(std::string reason);

    /// The queue's own note for `jobId`, or empty. `RenderQueue::job()` is gone
    /// after a settle, so the batch array is the only place the reason survives —
    /// and the reason is the whole value of a failure report.
    [[nodiscard]] std::string queuedNote(const std::string& jobId) const;

    WorkspaceState state_;
    std::string workspaceId_;
    RenderQueue* queue_{nullptr};

    /// The live job, if any. The single source of truth for "is rendering",
    /// which is why `isRendering()` cannot get stuck: it is false unless an id is
    /// present, an id is present only between `startRender` and a terminal state,
    /// and every terminal state — including a cancel of a job that never started
    /// a frame — reaches `onQueueStateChanged`, because `RenderQueue::cancel`
    /// settles a queued job rather than erasing it.
    std::optional<std::string> activeRenderId_;
    /// Kept so `renderCompleted` can name the file without asking the queue,
    /// which no longer knows the job once it has settled.
    fs::path activeRenderOutput_;
    /// The job's own denominator. 0 means "unknown", and it is the one value this
    /// class has to correct rather than trust: `RenderQueue::enqueue` hardcodes a
    /// literal `0` at its admission emit instead of asking
    /// `RenderResult::progressPercent()`, which returns -1 for exactly this case.
    u64 activeRenderExpectedFrames_{0};
    /// -1 = unknown. See `renderProgressPercent()`.
    int lastRenderPercent_{-1};
};

} // namespace vc::suno