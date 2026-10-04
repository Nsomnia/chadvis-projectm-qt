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
 * On macOS the answer is measured to be "the context works off-thread, the
 * drawable does not", and the three routes that could have provided one are all
 * closed. `runOffscreenRenderProbe` measures that on whatever machine it runs on
 * rather than encoding a platform check, so a Linux or Windows build of the
 * same tree gets its own answer and a render worker can branch on it.
 *
 * @section What it deliberately does not do
 * It never runs the routes that terminate the process. On macOS/cocoa:
 *
 *  - `QWindow::create()` on a worker thread makes AppKit raise
 *    `NSWindow should only be instantiated on the main thread!`. That is an
 *    Obj-C exception crossing Qt's C++ frames, so it lands on AppKit's own
 *    `ud2` and kills the process (measured: `EXC_BAD_INSTRUCTION`), and it is
 *    *not* catchable from the caller even with `-fobjc-exceptions`.
 *  - `QOpenGLContext::makeCurrent(QWindow*)` from a worker thread, with the
 *    window created on the main thread, raises inside
 *    `-[NSOpenGLContext setView:]` and dies the same way. So the "hidden window
 *    owned by the GUI thread, driven from a worker" hybrid is not a fallback.
 *
 * Both are therefore documented here and measured once, out of band, rather than
 * executed by a test that would take the whole ctest entry with it.
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

/// One measured frame, with the numbers rather than adjectives: a solid black or
/// a failed readback is what a `frame != nullptr` check would happily accept.
struct FrameProbe {
    u32 width{0};
    u32 height{0};
    usize bytes{0};
    u64 nonZeroBytes{0};
    double nonZeroFraction{0.0};
    u64 checksum{0};   ///< FNV-1a over the raw RGBA bytes; 0 for an all-zero buffer.
    u32 redBits{0};
    u32 depthBits{0};
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
        return width != 0 && height != 0 && nonZeroBytes > 0 && glError == 0 &&
               sentinelHeld;
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
[[nodiscard]] std::expected<OffscreenProbeReport, std::string> runOffscreenRenderProbe(
    const OffscreenProbeConfig& config = {});

/// Resident set size in bytes, or 0 when the platform cannot report it.
[[nodiscard]] usize residentSetBytes() noexcept;

/// One-line-per-measurement summary, for `std::println` in a test or a bug report.
[[nodiscard]] std::string describeOffscreenProbe(const OffscreenProbeReport& report);

/// Lines of `describeOffscreenProbe`, for `std::println` with a range.
[[nodiscard]] std::vector<std::string> offscreenProbeLines(const OffscreenProbeReport& report);

} // namespace vc