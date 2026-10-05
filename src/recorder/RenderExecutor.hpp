#pragma once
/**
 * @file RenderExecutor.hpp
 * @brief Owns the GL side of a batch render and interleaves N jobs through one
 *        context, because there is no off-main-thread drawable.
 *
 * @section The measurement this file is shaped by
 * `src/recorder/OffscreenRenderSpike` measured, on macOS 14.8.8 / Qt 6.11.1 /
 * QPA `cocoa`, and every design decision below traces to one of five numbers:
 *
 *  1. **A GL context works off the main thread; a *drawable* does not.**
 *     `CGLSetCurrentContext` on a worker succeeds and reports GL 4.1, but
 *     framebuffer 0 there is `GL_FRAMEBUFFER_UNDEFINED`. The control settles
 *     what is at stake: the same `QOffscreenSurface` + `QOpenGLContext` pair on
 *     the **GUI** thread produces the byte-identical failure, while a
 *     window-backed surface on that same thread is `GL_FRAMEBUFFER_COMPLETE`
 *     with 230383/230400 non-zero bytes and a sentinel that held. The variable
 *     is the **drawable shape**, not the thread. So this is not "GL is thread
 *     affine" and no restructuring here moves it: `CGLCreatePBuffer` was removed
 *     in 10.7, `QWindow::create()` from a worker throws
 *     `NSInternalInconsistencyException`, and raw `NSOpenGLContext` + `setView:`
 *     SIGILLs inside AppKit.
 *  2. **The ceiling is 1.** `RenderQueue::kDefaultMaxConcurrent` says so, and the
 *     reason is affinity rather than the 8.3 MB readback it was argued from.
 *     Parallelism therefore moves from contexts to **encoders**.
 *  3. **Two live contexts on the GUI thread cost 31.7 ms/frame against 31.4 ms
 *     for one** -- about 1%, alternating. That is the single most useful number
 *     in the report, because it makes "a second context *and* a second
 *     `pm::Engine`, both on the GUI thread, not the live one" a measured
 *     configuration rather than a hypothesis, and it is why this class does not
 *     share the live visualizer's engine. See @section Why not share the engine.
 *  4. **Teardown cost per cycle:** context only +10 KiB/cycle; context +
 *     `pm::Engine` **+175 KiB/cycle**; context + engine + readback +350 KiB/cycle.
 *     The growth belongs to **booting projectM per cycle**, so a design with one
 *     long-lived engine has a per-job GL cost of zero by construction. See
 *     `RenderExecutor::perJobGlCostIsZero()`.
 *  5. **One frame costs 31.4 ms mean / 25.4 min / 28.4 p95 / 30.3 max** -- at
 *     320x180, which is *below* the encode resolution. Re-measure at 1920x1080
 *     before quoting any wall-clock figure for a real job; see @section The
 *     numbers that are not yet real.
 *
 * @section Why frame-level interleaving, and what policy
 * With one context and one `pm::Engine`, N running jobs cannot exist: they are N
 * jobs taking turns. Three candidate policies were weighed:
 *
 *  - **Round-robin per frame.** Fairest by frame count, and it is the cheapest
 *    to reason about. It is also the *worst* fit here, for a cost this file
 *    measured rather than guessed: a job switch means
 *     `Engine::loadPreset(path, immediate)`, which is a synchronous preset read
 *    plus shader compile **on the GUI thread**. One frame per switch is one
 *    preset load per frame, so a 10800-frame job pays 10800 of them.
 *  - **Priority by frames-remaining.** Rejected outright: it is the textbook
 *    unfair policy, it needs a comparator over a quantity that changes every
 *    frame, and it makes the long job's progress depend on short jobs arriving.
 *  - **Deficit-free round-robin over a frame quantum.** Chosen. A job gets up
 *    to `quantum` consecutive frames, so a batch of M interleaved jobs pays
 *    `frames/quantum` preset loads per job instead of `frames`, and the switch
 *    count is a function of `quantum` that a caller can turn.
 *
 * `quantum = 8` is derived, not preferred. Cancellation latency is the binding
 * constraint (requirement: a cancel lands within one quantum), and at the
 * measured 31.4 ms/frame 8 frames is **~250 ms** -- an order of magnitude below
 * the 3-minute render it replaces and below the threshold at which a user
 * decides a UI is hung. The amortisation argument for 8 over 1 is *unmeasured*:
 * `Engine::loadPreset` was not timed, and `quantumFrames` is a config field for
 * exactly that reason. @section The numbers that are not yet real says so.
 *
 * **The honest consequence, which the policy does partly fix.** A 3-minute job
 * and a 10-second job sharing one context means the short one finishes
 * proportionally later -- if the scheduler is strictly serial. It is not, and
 * that is the one thing this policy genuinely buys: with `maxConcurrent` raised
 * from 1 to `kDefaultInterleaveDepth`, both jobs are *live*, so the short job
 * reaches its last frame after `(1800 + 600)` frames of shared context instead
 * of `1800` plus however long it then waited to be admitted. It still cannot
 * finish sooner than that -- the context is saturated by one job already -- but
 * it finishes without waiting for a *separate* admission, which is the
 * difference between "the 10-second job waits 6 minutes" and "it finishes at
 * 7% overrun".
 *
 * @section Why not share the live visualizer's engine
 * Sharing one `pm::Engine` with the live view would be the smaller change, and
 * it is wrong. One engine means one preset state, one audio stream and one
 * window: the live preview cannot show its own preset while a batch job is
 * mid-scene, and `VisualizerRenderer::setAudioQueue` would have to be repointed
 * per frame at whichever job owns the context. Measurement (3) is what makes
 * the alternative affordable -- a second context on the GUI thread is ~1% more
 * per frame -- so the executor owns its own context, its own `pm::Engine` and
 * its own `QWindow`, and contends with the live view for the GUI **thread** only.
 *
 * @section Why the per-job setup is not on the GUI thread
 * `avformat_open_input` and `avio_open` + `avformat_write_header` are blocking
 * disk I/O, tens of milliseconds each, once per job. Doing them in the
 * `JobRunner` -- which the queue calls synchronously, on the GUI thread --
 * would put a visible hitch at the front of every job. So each slot owns a pump
 * thread that performs setup, decodes audio, and pushes it; the GUI thread does
 * exactly three things: GL, `submitVideoFrame`, and one relaxed atomic store per
 * quantum. This is the whole answer to "what must it not do on that thread".
 *
 * @section The audio gate, stated honestly
 * `AudioQueue` is bounded and drops when full, and a dropped audio frame is a
 * **permanent** A/V desync -- it cannot be repaired after the fact. Three facts
 * make that unreachable here rather than merely unlikely:
 *
 *  1. **Audio owed is a function of frames produced, not of wall clock.** One
 *     video frame is exactly `sampleRate / fps` audio frames, so the pump's
 *     target is derived from the job's own frame counter. A slow context makes
 *     the render slow; it cannot make the producer outrun the consumer.
 *  2. **The GUI thread will not render a frame whose audio has not arrived.**
 *     `ScheduleSlot::audioBehind` is that gate. Skipping GL work for a job is
 *     "pausing the render", and the honest part is that it costs nothing that is
 *     not already true: this pipeline is already realtime-throttled against its
 *     own achievable frame rate, so there is no deadline to violate. It is also
 *     scoped to *one* job -- the others keep rendering.
 *  3. **A refusal cannot be retried, so it must be counted.** `pushAll` pushes a
 *     prefix and returns false, and the decoder has already moved on. So a
 *     refusal advances the cursor by exactly what landed and the loss is carried
 *     into the receipt's note and the job's reported drop count. The prevention
 *     is the queue size, derived in `RenderExecutor::audioQueueEntriesFor()`.
 *
 * @section Why the whole scheduling layer has no GL in it
 * `FrameInterleaver` is a pure value type: slots in, one decision out, no clock,
 * no context, no FFmpeg. That is not a testing convenience -- it is the only
 * reason the interleaving claim can be *proved* rather than asserted by reading.
 * The GL half sits behind `RenderFrameBackend`, which has no implementation yet;
 * see @section What is not here.
 *
 * @section The numbers that are not yet real
 *  - Frame cost was measured at **320x180**. Every wall-clock figure in this
 *    header inherits that resolution. Nothing here claims a 3-minute render takes
 *    5.7 minutes; it claims the *shape*, and the shape is resolution-dependent.
 *  - `Engine::loadPreset` was never timed, so the choice of `quantum = 8` over
 *    1 rests on the cancellation bound, not on a measured switch cost.
 *  - Two long-lived contexts plus two long-lived `pm::Engine`s in one process is
 *    *extrapolated* from the 31.7-vs-31.4 ms dual-context pass. That pass held
 *    two contexts and alternated; it did not hold two booted engines, and the
 *    +175 KiB/cycle figure is a per-cycle *growth* measurement, not a
 *    steady-state cost of a second engine. Two engines is the first thing to
 *    measure, and `RenderExecutor` is written so that measuring it needs no
 *    change to this file.
 *
 * @section What is not here
 * **There is no `RenderFrameBackend` implementation.** It needs a *window-backed*
 * surface, because the spike proved an offscreen surface has no colour buffer,
 * and the only window-backed drawable in the tree belongs to `VisualizerWindow`.
 * So the backend is either a second hidden `QWindow` created and shown on the
 * GUI thread, or a refactor that hands the live window's context to both owners.
 * Both are a runtime decision with a measurement attached, and shipping a
 * guessed one is precisely the `SunoWorkspace` registration trap. The executor
 * therefore *refuses to start a batch* without a backend rather than reporting
 * progress it cannot deliver.
 */

#include "recorder/OffscreenRenderSpike.hpp"
#include "recorder/RenderJob.hpp"
#include "recorder/RenderQueue.hpp"
#include "util/Types.hpp"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vc {

class AudioQueue;

// ═════════════════════════════════════════════════════════════════════════════
// 1. The pure interleaving policy
// ═════════════════════════════════════════════════════════════════════════════

/// Frames one job gets per `FrameInterleaver::next()`.
///
/// 8 frames is ~250 ms at the spike's measured 31.4 ms/frame, which is what
/// bounds cancel latency. See the file header for why this is not 1.
inline constexpr u32 kDefaultQuantumFrames = 8;

/// While the live visualizer is exposed, one frame per turn. The live view gets
/// a render pass between every batch frame instead of every eighth one; see
/// `RenderExecutor::setLiveVisualizerActive`.
inline constexpr u32 kLiveVisualizerQuantumFrames = 1;

/// Backstop for a job whose `expectedFrames` is 0 (legal, and reported as -1%
/// progress). A renderer with no upper bound and a misbehaving decoder is an
/// infinite job, so there is a cap -- three hours at 60 fps.
inline constexpr u64 kDefaultFrameCap = 60 * 60 * 60 * 3;

/// What the policy needs to know about one in-flight job. A value, not a
/// reference to a job: the whole class is free of GL so it can be driven by a
/// table in a test.
struct ScheduleSlot {
    std::string jobId;
    u64 framesDone{0};
    /// 0 means "unknown", and the slot then runs to `kDefaultFrameCap` rather
    /// than to a fabricated denominator.
    u64 framesExpected{0};
    /// `RenderQueue::isCancelRequested` for this id.
    bool cancelRequested{false};
    /// Audio is behind the frame cursor. See @section The audio gate.
    bool audioBehind{true};
};

/// One decision. Exactly one slot is named; the interleaver is a round robin, not
/// a planner, and a step that named several slots would be untestable.
enum class SlotVerdict : u8 {
    /// Spend up to `frames` GL frames on it.
    Render = 0,
    /// Every slot is blocked right now. Render nothing and come back; this is
    /// not a retirement and no slot is dropped.
    Skip = 1,
    /// The job reached its last expected frame. Stop the encoder, take a receipt,
    /// report `RenderQueue::finish`, drop the slot.
    Finish = 2,
    /// Cancelled or failed. Stop, report `RenderQueue::fail`, drop the slot.
    Retire = 3,
};

[[nodiscard]] std::string_view slotVerdictName(SlotVerdict verdict) noexcept;

struct ScheduleStep {
    std::size_t slotIndex{0};
    SlotVerdict verdict{SlotVerdict::Render};
    /// 1..`quantum`. Exactly 0 whenever `verdict != Render`.
    u32 frames{0};
    std::string jobId;
    /// Slots stepped over because they were `audioBehind`. Counted, not hidden:
    /// a persistently blocked job must show up as a diagnostic rather than as
    /// mysterious slowness elsewhere in the rotation.
    u32 skippedBehind{0};
};

/// The interleaving policy as a pure value type. No GL, no clock, no FFmpeg, no
/// projectM -- and that is the point, per the file header.
///
/// `sync()` takes the whole slot table every pass, so there is no incremental
/// bookkeeping to get out of step with the executor, and rotation order is slot
/// index order, i.e. handout order, which is what makes the output deterministic.
class FrameInterleaver {
public:
    FrameInterleaver() = default;
    FrameInterleaver(u32 quantumFrames, u64 frameCap = kDefaultFrameCap);

    FrameInterleaver(const FrameInterleaver&) = delete;
    FrameInterleaver& operator=(const FrameInterleaver&) = delete;

    /// Replace the slot table. A slot whose `jobId` is absent is dropped, which
    /// is how a retired job leaves the rotation without a second call site.
    void sync(std::vector<ScheduleSlot> table);

    /// The decision for this pass. Advances the rotation by one slot.
    [[nodiscard]] ScheduleStep next() noexcept;

    void setQuantumFrames(u32 frames) noexcept;
    [[nodiscard]] u32 quantumFrames() const noexcept { return quantumFrames_; }

    /// 1 while the live visualizer is exposed. See `kLiveVisualizerQuantumFrames`.
    void setLiveVisualizerActive(bool active) noexcept;
    [[nodiscard]] bool liveVisualizerActive() const noexcept { return liveVisualizer_; }

    void setFrameCap(u64 cap) noexcept { frameCap_ = cap; }

    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }
    /// Not named `slots()`: Qt's `slots` keyword macro is still live in this tree
    /// (`QT_NO_KEYWORDS` is not set), and an empty macro in front of a return type
    /// is a spectacular way to lose a declaration.
    [[nodiscard]] std::span<const ScheduleSlot> slotTable() const noexcept { return slots_; }

private:
    [[nodiscard]] bool exhausted(const ScheduleSlot& slot) const noexcept;

    std::vector<ScheduleSlot> slots_;
    /// Index of the slot to consider on the next `next()`. Wraps.
    std::size_t cursor_{0};
    u32 quantumFrames_{kDefaultQuantumFrames};
    u64 frameCap_{kDefaultFrameCap};
    bool liveVisualizer_{false};
};

/// Pure: which scene covers `frameIndex`, or nullopt past the end of the list.
///
/// The executor switches the engine's preset only when this index changes, so
/// a job with 12 scenes of 150 frames each pays 12 `loadPreset` calls rather than
/// 1800. `crossfadeSeconds` is read by the backend, not here.
[[nodiscard]] std::optional<std::size_t> sceneIndexForFrame(const RenderJob& job,
                                                            u64 frameIndex) noexcept;

// ═════════════════════════════════════════════════════════════════════════════
// 2. Verdict to concurrency
// ═════════════════════════════════════════════════════════════════════════════

/// What the measured platform verdict means for an executor's shape.
enum class RenderTopology : u8 {
    /// No drawable anywhere. A batch cannot run and must say so rather than
    /// writing a black file.
    Unsupported = 0,
    /// One context, one engine, on the GUI thread. Several jobs interleave
    /// frames through it. **This is what `RenderExecutor` implements.**
    GuiThreadShared = 1,
    /// One context on the GUI thread, one job at a time. Also this class, with
    /// the interleave depth pinned to 1 by the caller.
    GuiThreadExclusive = 2,
    /// N contexts on N worker threads. A different executor with a different
    /// lifetime model; `RenderExecutor` must refuse rather than fake it.
    WorkerPerJob = 3,
};

[[nodiscard]] std::string_view renderTopologyName(RenderTopology topology) noexcept;

/// What a bounded frame-cost pass measured.
struct FrameCalibration {
    u32 frames{0};
    double meanFrameMs{0.0};
    bool measured{false};
};

struct ConcurrencyDecision {
    RenderTopology topology{RenderTopology::Unsupported};
    /// A value for `RenderQueue::setMaxConcurrent`. Never negative.
    int maxConcurrent{1};
    /// True when a platform permitted workers but nobody has run the frame-cost
    /// pass, so no ceiling follows from the verdict alone.
    bool requiresCalibration{false};
    /// Never empty. A log line and a UI string, and the reason this function
    /// returns a struct rather than an int.
    std::string reason;
};

/// Pure: the machine's measured verdict, plus a frame-cost pass if one has run,
/// becomes a topology and a ceiling.
///
/// The macOS answer is a **topology**, not a number, and the difference is the
/// whole point: `NeedsMainThread` means several jobs can share one context, so
/// the interesting ceiling is *how many to interleave*, which is a product knob
/// (`kDefaultInterleaveDepth`); `Supported` means worker threads could each have
/// their own context, which this class does not implement, so it reports
/// `WorkerPerJob` and refuses. `offscreenVerdictMaxConcurrency()`'s -1 is
/// deliberately **not** turned into a number here: inventing one is exactly the
/// `kDefaultMaxConcurrent = 2` guess the spike was commissioned to replace.
[[nodiscard]] ConcurrencyDecision decideConcurrency(RenderThreadVerdict verdict,
                                                    const FrameCalibration& calibration,
                                                    double throughputBudgetMs) noexcept;

/// Jobs kept interleaved through the one context when the platform needs the GUI
/// thread. Above 1, and deliberately not 1: `RenderQueue::kDefaultMaxConcurrent`
/// is 1 because *two contexts* were impossible, and under interleaving the
/// ceiling no longer bounds contexts -- it bounds how many jobs are admitted at
/// once, which is what decides whether a 10-second job in a batch waits behind a
/// 3-minute one or merely shares the context with it.
inline constexpr int kDefaultInterleaveDepth = 4;

/// Cap on a calibrated ceiling. Eight live `VideoRecorderCore`s is eight FFmpeg
/// encoder threads, and the frame-cost pass says nothing about the memory they
/// need.
inline constexpr int kCalibratedCeilingCap = 8;

/// Default wall-clock a batch may claim per `renderTurn` before yielding. One
/// quantum of 8 frames at the measured 31.4 ms is ~250 ms, so the default is
/// the same order: a turn long enough to amortise the per-turn cost, short
/// enough that the Qt event loop gets control twice a second.
inline constexpr double kDefaultThroughputBudgetMs = 250.0;

// ═════════════════════════════════════════════════════════════════════════════
// 3. The GL seam
// ═════════════════════════════════════════════════════════════════════════════

/// Everything the executor needs from the outside world, and the only interface
/// in this file that touches a drawable.
///
/// **There is no implementation yet.** It needs a window-backed surface -- the
/// spike proved an offscreen surface has no colour buffer -- and the only
/// window-backed drawable in the tree belongs to `VisualizerWindow`. See the file
/// header, @section What is not here.
class RenderFrameBackend {
public:
    virtual ~RenderFrameBackend() = default;

    /// Called on the GUI thread once, before the first quantum. Creating the
    /// context, the surface and the `pm::Engine` is deliberately **outside** the
    /// per-job path: the spike measured +175 KiB/cycle for booting projectM per
    /// cycle, so a job must never pay it.
    [[nodiscard]] virtual std::expected<void, std::string> create() = 0;

    /// Tear down the context, the surface and the engine. Called once, when the
    /// executor is destroyed or the last job leaves.
    virtual void destroy() = 0;

    /// Absolute preset path for a `RenderJob::scenes[].presetName`. The schema
    /// stores names, not paths, so the backend owns the name -> path resolution
    /// (`PresetManager` is its lookup).
    [[nodiscard]] virtual std::expected<std::string, std::string>
    resolvePreset(std::string_view name) = 0;

    /// Point the engine at `presetPath`. Called **only** when the frame's scene
    /// index changes, so a 12-scene job pays 12 calls rather than 1800.
    [[nodiscard]] virtual std::expected<void, std::string>
    selectPreset(const std::string& presetPath) = 0;

    /// One GL frame at `width` x `height`, read back into `out` (resized,
    /// RGBA). `out` is reused across frames by the caller, so a backend that can
    /// read into a stable allocation should.
    [[nodiscard]] virtual std::expected<void, std::string> renderFrame(u32 width, u32 height,
                                                                       std::vector<u8>& out) = 0;

    /// Bounded create/destroy cycles to measure the per-frame cost on *this*
    /// machine at *this* resolution. The spike's 31.4 ms is from 320x180 and
    /// must not be reused for a 1920x1080 job.
    [[nodiscard]] virtual FrameCalibration calibrate(u32 width, u32 height, u32 frames) = 0;
};

// ═════════════════════════════════════════════════════════════════════════════
// 4. The executor
// ═════════════════════════════════════════════════════════════════════════════

/// Owns one context, one `pm::Engine` and one surface for the lifetime of the
/// process, and interleaves every job through them.
///
/// Lives on the GUI thread, because that is the only thread with a drawable --
/// a constraint, not a choice. All of its per-turn work is a GL frame plus an
/// atomic store; everything else (demux, decode, mux, encode) happens on the
/// job's own threads.
class RenderExecutor : public QObject {
    Q_OBJECT

public:
    struct Config {
        u32 quantumFrames{kDefaultQuantumFrames};
        double throughputBudgetMs{kDefaultThroughputBudgetMs};
        u64 frameCap{kDefaultFrameCap};
        /// PCM frames per `pushChunkTo`. Also the granularity of the audio
        /// cursor; see @section The audio gate for the derivation.
        u32 audioChunkFrames{1024};
        /// Warm-up frames fed to projectM before the first timed frame, so a
        /// preset compile does not land in the mean.
        u32 warmupFrames{8};
    };

    /// `queue` and `backend` must outlive the executor. A null queue is a hard
    /// error at construction -- an executor with nowhere to report is the
    /// `SunoWorkspace` trap again.
    RenderExecutor(RenderQueue* queue, RenderFrameBackend* backend, QObject* parent = nullptr);
    ~RenderExecutor() override;

    RenderExecutor(const RenderExecutor&) = delete;
    RenderExecutor& operator=(const RenderExecutor&) = delete;

    void setConfig(const Config& config);
    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /// Branch on the platform's measured verdict and, where the verdict implies
    /// this class's topology, install the ceiling on the queue.
    ///
    /// Returns an error -- and **installs nothing** -- for `Unsupported` and for
    /// `WorkerPerJob`. Refusing is the point: `Supported` means a worker thread
    /// could own a context, which is a different lifetime model, and running one
    /// GUI-thread context at a time while claiming a worker topology would report
    /// progress nobody can deliver.
    std::expected<void, std::string> configureForVerdict(RenderThreadVerdict verdict);

    [[nodiscard]] ConcurrencyDecision decision() const noexcept { return decision_; }

    /// The `JobRunner` to hand `RenderQueue`. Returns promptly: it registers a
    /// slot and starts the slot's pump thread, and all of the rendering happens
    /// later from `renderTurn()`.
    [[nodiscard]] JobRunner jobRunner();

    /// The `CancelHook` to install on the queue, so `RenderQueue::cancel` reaches
    /// an in-flight job rather than waiting for its next `reportProgress`.
    [[nodiscard]] CancelHook cancelHook();

    /// One event-loop turn: at most one quantum of GL frames. **Must be called on
    /// the GUI thread.** Returns the frames rendered, so a caller can watch a
    /// budget without reading private state.
    u32 renderTurn();

    /// Start driving `renderTurn` from a 0 ms timer. Idempotent.
    void start();
    /// Stop the timer. Does not cancel live jobs; call `cancelAll` for that.
    void stop();

    /// While true the quantum is 1 frame and every turn yields to the live
    /// visualizer. See @section Why not share the live engine.
    void setLiveVisualizerActive(bool active) noexcept;
    [[nodiscard]] bool liveVisualizerActive() const noexcept;

    /// Refuse every live job. Each takes its own path: a cancelled job reports
    /// `fail(RenderFailureKind::Cancelled)`, so the queue files it as
    /// `Cancelled` and does not retry it.
    void cancelAll();

    [[nodiscard]] std::size_t activeSlots() const noexcept;
    [[nodiscard]] bool isIdle() const noexcept { return activeSlots() == 0; }

    [[nodiscard]] double lastFrameMs() const noexcept { return lastFrameMs_; }

    /// Per-job GL cost, in objects created or destroyed. **Zero, by
    /// construction**: `create()` runs once and `destroy()` runs once, both
    /// outside every job's lifetime. The spike's +175 KiB/cycle belonged to
    /// booting a `pm::Engine` per cycle, and this design boots one.
    [[nodiscard]] u64 glObjectsCreatedPerJob() const noexcept { return 0; }

    /// Queue entries a job's `AudioQueue` needs, from the quantum.
    ///
    /// One quantum of video is `quantum * sampleRate / fps` PCM frames, which is
    /// the most the producer may legitimately be ahead by; plus one chunk in
    /// flight; plus a margin. `pushAll` writes *both* queues and returns
    /// `a && b`, and nothing ever drains a render job's `viz` queue, so a small
    /// queue would refuse forever -- see the .cpp's `drainUnreadVizQueue`.
    [[nodiscard]] static u32 audioQueueEntriesFor(u32 quantumFrames, u32 sampleRate, u32 fps);

private:
    /// One in-flight job. Everything expensive in here lives on a thread other
    /// than the GUI thread's; the GUI thread touches `framesProduced` (one
    /// relaxed store per quantum) and `submitVideoFrame`.
    struct JobSlot;
    /// Pumps audio for one job: opens the decoder, starts the encoder, and pushes
    /// PCM as fast as the frame counter allows.
    void pumpLoop(JobSlot* slot);

    /// Is this job's audio caught up with the frames it is about to render?
    /// Pure arithmetic on two atomics -- see @section The audio gate.
    [[nodiscard]] static bool audioSatisfiesFrame(JobSlot& slot, u32 sampleRate, u32 fps,
                                                  u32 quantumFrames) noexcept;

    void beginPass();
    void renderSlot(std::size_t index, u32 frames);
    void finishSlot(std::size_t index);
    void retireSlot(std::size_t index, std::string reason);
    /// Call `RenderFrameBackend::create()` **exactly once**, for the process.
    ///
    /// This one line is the whole per-job-GL-cost claim, so it is worth being
    /// explicit about why it is a guard rather than a comment: the spike measured
    /// +175 KiB/cycle for booting a `pm::Engine` per teardown cycle, and the only
    /// thing that makes that zero rather than merely small is that no job's
    /// lifetime can reach this call. `glObjectCounter_` is the audit trail.
    [[nodiscard]] std::expected<void, std::string> ensureBackend();
    /// Stop the pump, join it, stop the recorder, and take the receipt. Blocking,
    /// and it runs on the GUI thread: one encoder flush, tens of ms, once per
    /// job. A cancel cannot skip the trailer -- the file exists and has to be
    /// closed honestly -- but it does not wait for the render, which is the part
    /// that was three minutes long.
    void teardownSlot(std::size_t index);

    [[nodiscard]] JobSlot* slotFor(const std::string& jobId) noexcept;

    RenderQueue* queue_;
    RenderFrameBackend* backend_;
    Config config_;
    ConcurrencyDecision decision_{};
    FrameInterleaver interleaver_{};
    /// One allocation per job and never reallocated: erasing destroys the
    /// `JobSlot`, which destroys the thread it is joined to, so a pump thread's
    /// raw `JobSlot*` is stable exactly as long as this holds.
    std::vector<std::unique_ptr<JobSlot>> slots_;
    QTimer* timer_{nullptr};
    bool started_{false};
    double lastFrameMs_{0.0};
    u64 glObjectCounter_{0};
};

} // namespace vc