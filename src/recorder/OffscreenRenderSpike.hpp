#pragma once
/**
 * @file OffscreenRenderSpike.hpp
 * @brief Measures, on the machine that runs it, whether a GL context can render
 *        pixels on a background thread.
 *
 * @section Why this exists
 * The product is a batch music-video renderer: decode a file, render a frame,
 * encode it, repeat, thousands of times. `RenderQueue` schedules that work and
 * `kDefaultMaxConcurrent` is 2, which assumes two GL contexts can run at once.
 * Nobody had ever checked the assumption underneath it, because it is a
 * platform question and the platform is not documented in a header:
 *
 *  - projectM v4 draws into framebuffer 0 and nothing else. It binds
 *    `GL_DRAW_FRAMEBUFFER` to 0 immediately before its closing texture copy
 *    (`Engine.hpp:67`, and the integration suite pins it as a runtime fact), so
 *    the drawable it needs is the *default* framebuffer, not an FBO.
 *  - `VisualizerRenderer::render` takes `isExposed` as a parameter, so it does
 *    not care whether a window is mapped; but a drawable is still required.
 *  - `VisualizerRenderer` inherits `QOpenGLFunctions_3_3_Core`, whose
 *    `initializeOpenGLFunctions()` returns false without a current
 *    `QOpenGLContext`. So a raw CGL context is not enough to reuse it.
 *
 * @section The answer, measured on macOS 2026-10-04
 * **`NeedsMainThread`, and `RenderQueue::setMaxConcurrent(1)`.** Not 2.
 *
 * A GL context *does* create and become current on a worker thread and projectM *does*
 * initialise and render there. What does not happen is pixels: the drawable that thread
 * gets has no colour buffer, `glReadPixels` returns `GL_INVALID_FRAMEBUFFER_OPERATION` and
 * 0 non-zero bytes. Because every frame of every job must therefore be marshalled onto the
 * GUI thread, two concurrent render jobs cannot be two *running* jobs -- they are two
 * interleaved jobs over one context. `RenderQueue`'s default of 2 was chosen from an
 * argument about an 8.3 MB per-frame readback; the measurement it was waiting for is that
 * the ceiling is 1, and the reason is GL/drawable affinity rather than allocator pressure.
 * So `kDefaultMaxConcurrent` should become 1 and the throughput argument should move to
 * where the parallelism actually is: several *encoders* fed by one interleaved GL context.
 *
 * Two measured numbers that soften that, and are recorded because they change the fix rather
 * than the verdict: a leaked per-job **context** does not exist (120 create/destroy cycles:
 * +10 KiB/cycle, 1.18 MiB total spread, versus +175 KiB/cycle once a `pm::Engine` is booted
 * per cycle), and holding **two** live contexts costs ~1% more per frame than one (31.7 ms vs
 * 31.4 ms, alternating on the GUI thread -- which says the ceiling is about affinity, not
 * capacity). So the executor should own **one long-lived context and one long-lived
 * `pm::Engine`** on the GUI thread and give each job its own `VideoRecorderCore` + FFmpeg
 * encoder, rather than paying context creation per job.
 *
 * On macOS the answer is measured to be "the context works off-thread, the drawable
 * does not", and every route that could have provided one is closed.
 * `runOffscreenRenderProbe` measures that on whatever machine it runs on rather than
 * encoding a platform check, so a Linux or Windows build of the same tree gets its own
 * answer and a render worker can branch on it.
 *
 * @section The closed routes, and how each was closed
 * Measured out of band on macOS 14.8.8 / Qt 6.11.1 / QPA `cocoa`, one process per route,
 * because three of the six terminate the process. None of them is executed by this file or
 * by its test -- a test that took the ctest entry down with it would be worse than no test
 * -- so the facts live here as prose and only the two surviving, non-fatal routes are
 * measured live.
 *
 *  1. **`QOffscreenSurface` + `QOpenGLContext` on a worker thread.** Runs. Creates, becomes
 *     current, projectM initialises and renders frames. But framebuffer 0 is
 *     `GL_FRAMEBUFFER_UNDEFINED` (0x8219) with a `GL_VIEWPORT` of `0x0`, `glClear` and
 *     `glReadPixels` both return `GL_INVALID_FRAMEBUFFER_OPERATION`, and the readback is
 *     byte-for-byte zero. **It is the only off-thread route that does not die**, so it is
 *     the one the spike runs live.
 *  2. **`QOffscreenSurface` on the *main* thread.** Runs, and produces **exactly the same
 *     incomplete framebuffer**. Measured live, because it is the fact that makes the
 *     verdict mean what it says: the missing colour buffer is *cocoa*, not thread affinity.
 *     Without this control the report reads as "GL contexts are thread affine on macOS",
 *     which is false -- `CGLSetCurrentContext` on a worker thread returns `kCGLNoError`
 *     and yields a working GL 4.1 core context (route 3).
 *  3. **Raw CGL on a worker thread**, no Qt in the way. `CGLChoosePixelFormat` and
 *     `CGLCreateContext` succeed, `CGLSetCurrentContext` succeeds on the worker, and the
 *     context reports `GL_VERSION=4.1 INTEL-22.5.15` / `GLSL=4.10`. Framebuffer 0 still has
 *     no colour buffer, and the historical remedy is gone: `CGLCreatePBuffer` returns
 *     **10005**, so CGL pbuffers are unusable on this SDK (they have been
 *     `OPENGL_DEPRECATED(10.3, 10.7)` for a decade).
 *  4. **`QWindow::create()` + `show()` on a worker thread.** AppKit raises
 *     `NSInternalInconsistencyException`, `NSWindow should only be instantiated on the main
 *     thread!`, from `-[NSWindow initWithContentRect:...]` under `QWindowPrivate::create`. It
 *     is an Obj-C exception crossing Qt's C++ frames, so it terminates the process (measured:
 *     `libc++abi: terminating`, exit 134) and is **not** catchable from the caller.
 *  5. **`QOpenGLContext::makeCurrent(QWindow*)` from a worker thread**, with the window owned
 *     by the GUI thread -- the "hidden window on the GUI thread, driven from a worker"
 *     hybrid. **Qt refuses it in its own code**: `Cannot make QOpenGLContext current in a
 *     different thread`, then abort (measured: exit 134). Recorded precisely because this is
 *     a *Qt* guard and not an AppKit one, which is what makes it look bypassable and invites a
 *     hand-rolled `NSOpenGLContext` around Qt. Route 6 is that attempt.
 *  6. **Raw `NSOpenGLContext` + `setView:` on a worker thread.** SIGILL (measured: exit 132,
 *     a `ud2` trap) inside `-[NSOpenGLContext setView:]` when handed a view belonging to a
 *     main-thread `NSWindow`. Bypassing Qt's guard does not buy a drawable; it buys a crash.
 *
 * The conclusion is stronger than "the context is thread affine", and the difference matters
 * for the roadmap: **macOS has no off-main-thread drawable mechanism left at all.** Every
 * window is an `NSWindow` that must be instantiated on the main thread, the only non-window
 * drawable CGL ever had is a pbuffer API that now returns an error, and `NSOpenGLContext`
 * refuses to be pointed at a foreign view off the main thread. That is a platform property,
 * so no restructuring inside this repository moves it.
 *
 * @section Threading
 * `runOffscreenRenderProbe` must be called on the thread that owns the
 * `QGuiApplication`. It creates its worker threads itself and joins them before
 * returning. Everything it measures is written into the returned report; the
 * function logs nothing and touches no config.
 */

#include "util/Types.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace vc {

/// What a render worker should do about background-thread rendering.
enum class RenderThreadVerdict {
    /// A real, full-sized drawable exists off the main thread. Workers may render.
    Supported,
    /// The context creates and becomes current off the main thread, but the
    /// platform will not give it a colour buffer there. All GL work has to be
    /// marshalled onto the GUI thread, so N concurrent jobs must become N
    /// interleaved jobs over one context.
    NeedsMainThread,
    /// No usable GL context at all: no QPA plugin that can provide one, or the
    /// control measurement on the main thread failed too. Offline video rendering
    /// is not possible on this machine and the product has to degrade to a
    /// visible error rather than a silent black file.
    Unsupported,
};

[[nodiscard]] const char* toString(RenderThreadVerdict verdict) noexcept;

/// The concurrency ceiling this verdict permits, as a value for
/// `RenderQueue::setMaxConcurrent`, so the executor branches on a number instead of
/// re-deriving the argument.
///
/// - `Supported` returns **-1**: no ceiling follows from the verdict, and the frame-cost
///   pass is what decides. Returning a number here would put an invented constant back in
///   the code and reintroduce exactly the `kDefaultMaxConcurrent` guess this exists to
///   replace.
/// - `NeedsMainThread` returns **1**. Every frame of every job has to be marshalled onto
///   the GUI thread, so N concurrent jobs cannot be N running jobs -- they are N interleaved
///   jobs over one context. Two contexts on two threads is not merely slower here, it is
///   impossible (measured: `QOpenGLContext::makeCurrent` refuses a foreign thread, and
///   `-[NSOpenGLContext setView:]` traps).
/// - `Unsupported` returns **0**: do not render at all, and say so in the UI rather than
///   writing a black file.
[[nodiscard]] int offscreenVerdictMaxConcurrency(RenderThreadVerdict verdict) noexcept;

/// Human-readable name for a `glGetError()` value, so a report says
/// `GL_INVALID_FRAMEBUFFER_OPERATION` instead of `0x506`. Never null; unknown values come
/// back as `GL_UNKNOWN_ERROR(0x…)`.
[[nodiscard]] const char* toString(u32 glError) noexcept;

/// Human-readable name for a `glCheckFramebufferStatus` value. Never null; unknown values
/// come back as `GL_FRAMEBUFFER_UNKNOWN(0x…)`. The spike reports this rather than
/// `GL_RED_BITS`/`GL_DEPTH_BITS`, because those are **not valid queries on a core-profile
/// context on the macOS driver**: measured `GL_INVALID_ENUM` (0x500) for all three on a
/// window-backed context that demonstrably holds pixels, so they reported a broken drawable
/// and a working one identically.
[[nodiscard]] const char* toStringFramebufferStatus(u32 status) noexcept;

/// One measured frame, with the numbers rather than adjectives: a solid black or
/// a failed readback is what a `frame != nullptr` check would happily accept.
struct FrameProbe {
    u32 width{0};
    u32 height{0};
    usize bytes{0};
    u64 nonZeroBytes{0};
    double nonZeroFraction{0.0};
    u64 checksum{0}; ///< FNV-1a over the raw RGBA bytes; 0 for an all-zero buffer.
    /// `glCheckFramebufferStatus(GL_FRAMEBUFFER)` after binding framebuffer 0, i.e. the
    /// completeness of the *default* framebuffer -- the only destination projectM has.
    u32 framebufferStatus{0};
    /// `GL_VIEWPORT` after the same bind. `0x0` is what a surface with no drawable at all
    /// reports, and it is the datum that separates "no colour buffer" from "a small one".
    u32 viewportWidth{0};
    u32 viewportHeight{0};
    u32 glError{0};
    u32 framesRendered{0};
    /// True when every channel of the readback matched a sentinel colour written
    /// by a preceding `glClear`. This is what makes the readback evidence: a
    /// failed read that leaves the destination buffer alone can still return
    /// non-zero bytes, and those bytes are not a picture.
    bool sentinelHeld{false};
    u32 sentinelClearError{0};
    u32 sentinelReadError{0};

    [[nodiscard]] bool usable() const noexcept {
        return width != 0 && height != 0 && nonZeroBytes > 0 && glError == 0 && sentinelHeld;
    }
};

struct OffscreenProbeConfig {
    /// Encode resolution. projectM renders at the window size, so this is both
    /// the drawable size and the per-frame readback size: 1920x1080x4 = 8.3 MB
    /// per frame, which is the number `kDefaultMaxConcurrent = 2` was argued from.
    u32 width{1920};
    u32 height{1080};
    u32 sampleRate{48000};
    u32 fps{60};

    /// Frames rendered before the timed ones, so preset load and shader compile do
    /// not land in the mean.
    u32 warmupFrames{8};
    /// Timed frames for the single-context and dual-context means.
    u32 measureFrames{30};

    /// create/destroy cycles for the leak measurement. A batch renderer creates
    /// one context per job and a batch is thousands of jobs, so this is the one
    /// step people skip and the one that bites in production.
    u32 teardownCycles{24};

    /// What a teardown cycle owns and destroys. Both are on by default because that
    /// is what a render job costs, and they are separable because the measured leak
    /// has to be *attributed* before it can be fixed: re-running with
    /// `teardownInitializeProjectm` off says whether the growth belongs to projectM's
    /// per-instance GL resources or to the context/driver, and those have opposite
    /// fixes (one long-lived `pm::Engine` versus one long-lived context).
    bool teardownInitializeProjectm{true};
    bool teardownReadback{true};

    bool measureOffThread{true};
    bool measureMainThreadControl{true};
    bool measureFrameCost{true};
    bool measureTeardown{true};
    /// Two contexts alive at once, alternating frames. Only meaningful when
    /// `measureFrameCost` is also on.
    bool measureDualContext{true};
};

struct OffscreenProbeReport {
    RenderThreadVerdict verdict{RenderThreadVerdict::Unsupported};
    /// Human-readable explanation of the verdict. For logs, never for a branch.
    std::string reason;
    /// QPA plugin name ("cocoa", "xcb", "wayland", "offscreen", ...), because the
    /// answer is a property of the plugin and not of the OS name.
    std::string platform;

    // --- background thread ---
    bool offThreadContextCreated{false};
    bool offThreadContextCurrent{false};
    /// projectM initialised and rendered on the worker. Recorded separately from
    /// `offThreadFrame.usable()` because "projectM ran" and "pixels came out" are
    /// different claims and both matter.
    bool offThreadProjectmInitialized{false};
    bool offThreadProjectmRendered{false};
    FrameProbe offThreadFrame{};
    std::string offThreadFailure;

    // --- main-thread control ---
    bool mainThreadDrawableAvailable{false};
    bool mainThreadProjectmInitialized{false};
    FrameProbe mainThreadFrame{};
    std::string mainThreadFailure;

    // --- offscreen-surface control, on the GUI thread ---
    // The same `QOffscreenSurface` + `QOpenGLContext` the background-thread pass uses,
    // built on the thread where a drawable definitely exists. It is the measurement that
    // turns "the worker thread had no colour buffer" into "this platform gives no colour
    // buffer on an offscreen surface, on any thread" -- and it is the difference between
    // the honest verdict and a claim about thread affinity that is simply false.
    bool offscreenSurfaceOnMainThreadAttempted{false};
    bool offscreenSurfaceOnMainThreadValid{false};
    FrameProbe offscreenSurfaceOnMainThreadFrame{};
    std::string offscreenSurfaceOnMainThreadFailure;

    /// Whether the two controls above **disagree**: the window-backed one holds a
    /// complete framebuffer and the offscreen-surface one does not. When this is false the
    /// reason string must not blame thread affinity, because nothing distinguished the
    /// threads -- the surfaces were the variable in both passes.
    bool drawableShapeIsTheVariable{false};

    // --- teardown ---
    u32 teardownCycles{0};
    u32 teardownRenderedCycles{0};
    /// Resident bytes after each cycle, first entry being the pre-loop reading. The
    /// series is what separates a one-off driver allocation from a per-job leak: only
    /// the slope answers that, and a single before/after pair cannot.
    std::vector<usize> teardownRssBytes;
    usize rssBeforeBytes{0};
    usize rssAfterBytes{0};
    usize peakRssBytes{0};
    /// Resident bytes the renderer kept after the cycles. A per-job context leak
    /// shows up here; a driver-level one-off cost does not, which is why `peak`
    /// is reported alongside.
    i64 rssDeltaBytes{0};
    double teardownCycleMs{0.0};
    bool rssAvailable{false};

    // --- frame cost ---
    // min/max alongside mean and p95 because a percentile of a handful of samples is
    // not interpretable on its own: without the range there is no way to tell a
    // genuine p95 from a mis-ordered sample.
    u32 measuredFrames{0};
    double meanFrameMs{0.0};
    double p95FrameMs{0.0};
    double minFrameMs{0.0};
    double maxFrameMs{0.0};
    u32 dualMeasuredFrames{0};
    bool dualContextMeasured{false};
    double dualMeanFrameMs{0.0};
    double dualP95FrameMs{0.0};
    double dualMinFrameMs{0.0};
    double dualMaxFrameMs{0.0};
    /// Why the dual-context pass produced nothing, when it did. An unmeasured pass
    /// with no reason is exactly the kind of gap that reads as a pass.
    std::string dualFailure;
};

/// Runs the probe. Returns the measurements, or a string explaining why it could
/// not run at all (no QGuiApplication, not on the GUI thread, or a QPA plugin
/// that cannot create a GL context).
///
/// Never throws and never terminates the process: the fatal cocoa paths
/// documented in this header are not executed.
[[nodiscard]] std::expected<OffscreenProbeReport, std::string>
runOffscreenRenderProbe(const OffscreenProbeConfig& config = {});

/// Resident set size in bytes, or 0 when the platform cannot report it.
[[nodiscard]] usize residentSetBytes() noexcept;

/// One-line-per-measurement summary, for `std::println` in a test or a bug report.
[[nodiscard]] std::string describeOffscreenProbe(const OffscreenProbeReport& report);

/// Lines of `describeOffscreenProbe`, for `std::println` with a range.
[[nodiscard]] std::vector<std::string> offscreenProbeLines(const OffscreenProbeReport& report);

} // namespace vc