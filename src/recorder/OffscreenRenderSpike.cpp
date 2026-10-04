#include "recorder/OffscreenRenderSpike.hpp"

#include "visualizer/projectm/Engine.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>
#include <QThread>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#    include <mach/mach.h>
#elif defined(__linux__)
#    include <unistd.h>
#endif

namespace vc {

namespace {

using Clock = std::chrono::steady_clock;

/// GL 3.3 core, matching `VisualizerRenderer`'s
/// `QOpenGLFunctions_3_3_Core` and projectM's `#version 330` shaders.
QSurfaceFormat coreContextFormat() {
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    return format;
}

/// The drawable has to exist before a context can be made current on it, and on
/// cocoa the window system delivers the expose event from the event loop, so this
/// cannot be a plain sleep.
constexpr int kExposeTimeoutMs = 5000;

bool waitForExpose(QWindow& window) {
    if (window.isExposed())
        return true;
    QElapsedTimer deadline;
    deadline.start();
    while (!window.isExposed() && deadline.elapsed() < kExposeTimeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return window.isExposed();
}

/// Wait for the drawable, *then* make the context current, in that order.
///
/// The order is load-bearing: expose is delivered by the window system through
/// the event loop, and draining events around a GL surface can leave some other
/// context current, so `makeCurrent` then `processEvents` is a race that shows up
/// as `initializeOpenGLFunctions()` returning false on the *second* context in a
/// process rather than on the first. Doing it this way removed that failure.
bool exposeThenMakeCurrent(QOpenGLContext& context, QWindow& window) {
    if (!waitForExpose(window))
        return false;
    return context.makeCurrent(&window);
}

/// Deterministic percussive PCM so projectM's analysis has energy to work with.
/// A silent buffer would leave the idle preset motionless and every measurement
/// would report an all-black frame -- indistinguishable from a failed readback.
std::vector<f32> percussiveTick(const OffscreenProbeConfig& cfg, u32 frameIndex) {
    const u32 framesPerTick = cfg.sampleRate / (cfg.fps != 0 ? cfg.fps : 1);
    std::vector<f32> pcm(static_cast<usize>(framesPerTick) * 2);
    const f32 decay = std::exp(-4.0f * static_cast<f32>(frameIndex % 12) / 12.0f);
    const f32 freq = 55.0f + 40.0f * decay;
    for (u32 i = 0; i < framesPerTick; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(cfg.sampleRate);
        const f32 sample = std::sin(2.0f * 3.14159265f * freq * t) * decay * 0.4f;
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }
    return pcm;
}

u64 fnv1a(std::span<const u8> bytes) {
    u64 hash = 1469598103934665603ULL;
    for (const u8 byte : bytes) {
        hash ^= static_cast<u64>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

/// A sentinel that identifies its own bytes, so a readback can be told apart from
/// "the driver happened to leave something in that memory". The four channels are
/// distinct, and 0xA5 is not a value projectM is likely to emit in bulk.
constexpr std::array<u8, 4> kSentinel{0xA5, 0x5A, 0x3C, 0xC3};

struct SentinelVerdict {
    bool clearAccepted{false};
    bool bytesMatchSentinel{false};
    usize matchCount{0};
    u32 clearError{0};
    u32 readError{0};
};

/// glClear framebuffer 0 to a known colour and read it back.
///
/// This is the check that separates "the readback returned non-zero bytes" from
/// "the drawable actually holds the picture": a driver that leaves the destination
/// buffer untouched on a failed read reports non-zero content that has nothing to
/// do with what was rendered. Measured behaviour differs between platforms here,
/// so it is measured rather than assumed -- see the report's `sentinel` fields.
SentinelVerdict probeSentinel(QOpenGLFunctions_3_3_Core& gl,
                              const OffscreenProbeConfig& cfg,
                              std::vector<u8>& scratch) {
    SentinelVerdict verdict;
    scratch.assign(static_cast<usize>(cfg.width) * cfg.height * 4, 0);

    // Bind framebuffer 0 explicitly on both sides first. projectM leaves whatever
    // it last bound in place, and clearing an incomplete FBO reports
    // GL_INVALID_FRAMEBUFFER_OPERATION while reading a different attachment --
    // which measures the driver's leftover state, not the drawable.
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    while (gl.glGetError() != GL_NO_ERROR) {
    }
    gl.glDisable(GL_SCISSOR_TEST);
    gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.glClearColor(static_cast<GLfloat>(kSentinel[0]) / 255.0f,
                    static_cast<GLfloat>(kSentinel[1]) / 255.0f,
                    static_cast<GLfloat>(kSentinel[2]) / 255.0f,
                    static_cast<GLfloat>(kSentinel[3]) / 255.0f);
    gl.glClear(GL_COLOR_BUFFER_BIT);
    const GLenum clearError = gl.glGetError();
    verdict.clearAccepted = clearError == GL_NO_ERROR;
    verdict.clearError = static_cast<u32>(clearError);

    gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.glReadPixels(0,
                    0,
                    static_cast<GLsizei>(cfg.width),
                    static_cast<GLsizei>(cfg.height),
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    scratch.data());
    verdict.readError = static_cast<u32>(gl.glGetError());

    for (usize i = 0; i + 3 < scratch.size(); i += 4) {
        if (scratch[i] == kSentinel[0] && scratch[i + 1] == kSentinel[1] &&
            scratch[i + 2] == kSentinel[2] && scratch[i + 3] == kSentinel[3])
            ++verdict.matchCount;
    }
    const usize pixels = scratch.size() / 4;
    verdict.bytesMatchSentinel = pixels != 0 && verdict.matchCount == pixels;
    return verdict;
}

struct ContextRun {
    bool contextCreated{false};
    bool contextCurrent{false};
    bool projectmInitialized{false};
    bool projectmRendered{false};
    FrameProbe frame;
    /// True when every one of the four channels in the readback matches the
    /// sentinel colour, i.e. the drawable demonstrably holds what was written to
    /// it. This is the check that makes the readback evidence rather than a
    /// coincidence.
    bool sentinelHeld{false};
    u32 sentinelClearError{0};
    u32 sentinelReadError{0};
    std::string failure;
};

/// Renders into whatever is bound to framebuffer 0 on the calling thread and
/// reads the result back. Called only with a current context, and always before
/// the context goes out of scope, so the `pm::Engine` destructor (which calls
/// `projectm_destroy`) still has a current context.
ContextRun renderOnCurrentContext(QOpenGLFunctions_3_3_Core& gl,
                                  const OffscreenProbeConfig& cfg,
                                  u32 warmupFrames,
                                  u32 timedFrames,
                                  std::vector<double>* timingsMs) {
    ContextRun run;

    while (gl.glGetError() != GL_NO_ERROR) {
    }
    GLint redBits = 0;
    GLint depthBits = 0;
    gl.glGetIntegerv(GL_RED_BITS, &redBits);
    gl.glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    while (gl.glGetError() != GL_NO_ERROR) {
    }
    run.frame.width = cfg.width;
    run.frame.height = cfg.height;
    run.frame.redBits = static_cast<u32>(redBits < 0 ? 0 : redBits);
    run.frame.depthBits = static_cast<u32>(depthBits < 0 ? 0 : depthBits);
    run.frame.bytes = static_cast<usize>(cfg.width) * cfg.height * 4;

    pm::EngineConfig engineConfig;
    engineConfig.width = cfg.width;
    engineConfig.height = cfg.height;
    engineConfig.fps = cfg.fps;

    pm::Engine engine;
    const auto init = engine.init(engineConfig);
    run.projectmInitialized = init.isOk();
    if (!init) {
        run.failure = "projectM init failed on this context: " + init.error().message;
        return run;
    }

    const auto renderOnce = [&](u32 frameIndex) {
        const std::vector<f32> pcm = percussiveTick(cfg, frameIndex);
        engine.addPCMDataInterleaved(pcm.data(),
                                     static_cast<u32>(pcm.size() / 2),
                                     2);
        gl.glViewport(0, 0, static_cast<GLsizei>(cfg.width), static_cast<GLsizei>(cfg.height));
        engine.render();
        gl.glFinish();
    };

    for (u32 i = 0; i < warmupFrames; ++i)
        renderOnce(i);

    timingsMs->reserve(static_cast<usize>(timedFrames));
    for (u32 i = 0; i < timedFrames; ++i) {
        const auto start = Clock::now();
        renderOnce(warmupFrames + i);
        timingsMs->push_back(
            std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    run.projectmRendered = timedFrames > 0 || warmupFrames > 0;

    // Drain every error projectM left behind *before* the readback, so the error
    // reported for `glReadPixels` belongs to `glReadPixels`. Without this the
    // reported error belongs to whichever projectM call tripped first and the
    // readback's own success or failure is invisible. Binding framebuffer 0 first
    // is the same reason.
    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    while (gl.glGetError() != GL_NO_ERROR) {
    }
    gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
    std::vector<u8> pixels(run.frame.bytes, 0);
    gl.glReadPixels(0,
                    0,
                    static_cast<GLsizei>(cfg.width),
                    static_cast<GLsizei>(cfg.height),
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    pixels.data());
    run.frame.glError = static_cast<u32>(gl.glGetError());
    run.frame.framesRendered = warmupFrames + timedFrames;

    const auto nonZero = static_cast<u64>(std::count_if(
        pixels.begin(), pixels.end(), [](u8 byte) { return byte != 0; }));
    run.frame.nonZeroBytes = nonZero;
    run.frame.nonZeroFraction =
        run.frame.bytes ? static_cast<double>(nonZero) / static_cast<double>(run.frame.bytes)
                        : 0.0;
    run.frame.checksum = nonZero ? fnv1a(pixels) : 0;

    const SentinelVerdict sentinel = probeSentinel(gl, cfg, pixels);
    run.sentinelHeld = sentinel.bytesMatchSentinel;
    run.frame.sentinelHeld = sentinel.bytesMatchSentinel;
    run.frame.sentinelClearError = sentinel.clearError;
    run.frame.sentinelReadError = sentinel.readError;
    run.sentinelClearError = sentinel.clearError;
    run.sentinelReadError = sentinel.readError;
    return run;
}

/// The off-thread attempt: a `QOffscreenSurface` and a `QOpenGLContext` both
/// created on a worker thread, with no event loop and no window. This is the only
/// off-thread route that does not terminate the process, so it is the only one the
/// spike runs.
ContextRun probeOffThread(const OffscreenProbeConfig& cfg) {
    ContextRun run;
    std::thread worker([&] {
        QOffscreenSurface surface;
        surface.setFormat(coreContextFormat());
        surface.create();
        if (!surface.isValid()) {
            run.failure = "QOffscreenSurface::create() produced no valid surface on a "
                          "worker thread";
            return;
        }

        QOpenGLContext context;
        context.setFormat(coreContextFormat());
        run.contextCreated = context.create();
        if (!run.contextCreated) {
            run.failure = "QOpenGLContext::create() returned false on a worker thread";
            return;
        }
        run.contextCurrent = context.makeCurrent(&surface);
        if (!run.contextCurrent) {
            run.failure = "QOpenGLContext::makeCurrent(QOffscreenSurface*) returned false on a "
                          "worker thread";
            return;
        }

        QOpenGLFunctions_3_3_Core gl;
        if (!gl.initializeOpenGLFunctions()) {
            run.failure = "QOpenGLFunctions_3_3_Core::initializeOpenGLFunctions() returned "
                          "false on a worker thread";
            return;
        }

        std::vector<double> timings;
        ContextRun inner = renderOnCurrentContext(gl, cfg, cfg.warmupFrames, 0, &timings);
        run.projectmInitialized = inner.projectmInitialized;
        run.projectmRendered = inner.projectmRendered;
        run.frame = inner.frame;
        run.sentinelHeld = inner.sentinelHeld;
        run.sentinelClearError = inner.sentinelClearError;
        run.sentinelReadError = inner.sentinelReadError;
        run.failure = inner.failure;
        // ~QOpenGLContext releases the context and ~pm::Engine (inside
        // renderOnCurrentContext) already destroyed projectM, so both teardowns
        // happened with a current context.
    });
    worker.join();
    return run;
}

double meanOf(std::span<const double> values) {
    if (values.empty())
        return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double minimumOf(std::span<const double> values) {
    return values.empty() ? 0.0 : *std::min_element(values.begin(), values.end());
}

double maximumOf(std::span<const double> values) {
    return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
}

double percentileOf(std::span<const double> values, double percentile) {
    if (values.empty())
        return 0.0;
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const auto rank = static_cast<usize>(
        std::clamp(percentile * static_cast<double>(sorted.size()), 1.0,
                   static_cast<double>(sorted.size())));
    return sorted[rank - 1];
}

} // namespace

const char* toString(RenderThreadVerdict verdict) noexcept {
    switch (verdict) {
        case RenderThreadVerdict::Supported:
            return "Supported";
        case RenderThreadVerdict::NeedsMainThread:
            return "NeedsMainThread";
        case RenderThreadVerdict::Unsupported:
            return "Unsupported";
    }
    return "Unsupported";
}

usize residentSetBytes() noexcept {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return static_cast<usize>(info.resident_size);
#elif defined(__linux__)
    // /proc/self/statm reports resident pages as the second field. Reading it per
    // cycle is cheap and, unlike a ps parse, needs no allocation.
    const int resident = [] {
        std::FILE* file = std::fopen("/proc/self/statm", "r");
        if (!file)
            return -1;
        long total = 0;
        long residentPages = 0;
        const int read = std::fscanf(file, "%ld %ld", &total, &residentPages);
        std::fclose(file);
        return read == 2 ? static_cast<int>(residentPages) : -1;
    }();
    if (resident < 0)
        return 0;
    return static_cast<usize>(resident) * static_cast<usize>(::sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

std::expected<OffscreenProbeReport, std::string> runOffscreenRenderProbe(
    const OffscreenProbeConfig& config) {
    auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (!guiApp)
        return std::unexpected("runOffscreenRenderProbe needs a QGuiApplication; "
                              "unit_tests runs a QCoreApplication and cannot host GL");
    if (QThread::currentThread() != guiApp->thread())
        return std::unexpected("runOffscreenRenderProbe must be called on the QGuiApplication "
                              "thread: it creates its own windows and QWindow may only be "
                              "constructed there");

    OffscreenProbeReport report;
    report.platform = QGuiApplication::platformName().toStdString();

    // ---------------------------------------------------------- main-thread control
    // A QWindow on this thread with a GL 3.3 core context: the one drawable shape
    // that is known to work everywhere, and the baseline every other number is
    // compared against.
    if (config.measureMainThreadControl) {
        QWindow window;
        window.setSurfaceType(QWindow::OpenGLSurface);
        window.setFormat(coreContextFormat());
window.resize(static_cast<int>(config.width), static_cast<int>(config.height));
            window.show();
            if (window.winId() == 0) {
            report.mainThreadFailure = "QWindow::create() produced no native handle";
        } else {
            QOpenGLContext context;
            context.setFormat(coreContextFormat());
            if (!context.create()) {
                report.mainThreadFailure =
                    "QOpenGLContext::create() failed for a window-backed context; this QPA "
                    "plugin cannot provide a GL 3.3 core context";
            } else if (!exposeThenMakeCurrent(context, window)) {
                report.mainThreadFailure =
                    window.isExposed()
                        ? "QOpenGLContext::makeCurrent(QWindow*) failed on the GUI thread"
                        : "the window never became exposed, so framebuffer 0 has no "
                          "drawable";
            } else {
                QOpenGLFunctions_3_3_Core gl;
                if (!gl.initializeOpenGLFunctions()) {
                    report.mainThreadFailure =
                        "QOpenGLFunctions_3_3_Core::initializeOpenGLFunctions() failed for a "
                        "window-backed context";
                } else {
                    std::vector<double> timings;
                    const ContextRun run =
                        renderOnCurrentContext(gl, config, config.warmupFrames, 0, &timings);
                    report.mainThreadDrawableAvailable = true;
                    report.mainThreadProjectmInitialized = run.projectmInitialized;
                    report.mainThreadFrame = run.frame;
                    report.mainThreadFrame.sentinelHeld = run.sentinelHeld;
                    report.mainThreadFrame.sentinelClearError = run.sentinelClearError;
                    report.mainThreadFrame.sentinelReadError = run.sentinelReadError;
                    report.mainThreadFailure = run.failure;
                }
            }
        }
        // The window and its context are released here, which is also the
        // per-job teardown the leak loop below repeats at scale.
    }

    // ------------------------------------------------------------- background thread
    if (config.measureOffThread) {
        const ContextRun run = probeOffThread(config);
        report.offThreadContextCreated = run.contextCreated;
        report.offThreadContextCurrent = run.contextCurrent;
        report.offThreadProjectmInitialized = run.projectmInitialized;
        report.offThreadProjectmRendered = run.projectmRendered;
        report.offThreadFrame = run.frame;
        report.offThreadFrame.sentinelHeld = run.sentinelHeld;
        report.offThreadFrame.sentinelClearError = run.sentinelClearError;
        report.offThreadFrame.sentinelReadError = run.sentinelReadError;
        report.offThreadFailure = run.failure;
    }

    // -------------------------------------------------------------------- frame cost
    if (config.measureFrameCost) {
        QWindow window;
        window.setSurfaceType(QWindow::OpenGLSurface);
        window.setFormat(coreContextFormat());
        window.resize(static_cast<int>(config.width), static_cast<int>(config.height));
        window.show();
        if (window.winId() != 0) {
            QOpenGLContext context;
            context.setFormat(coreContextFormat());
            if (context.create() && exposeThenMakeCurrent(context, window)) {
                QOpenGLFunctions_3_3_Core gl;
                if (gl.initializeOpenGLFunctions()) {
                    std::vector<double> timings;
                    const ContextRun run = renderOnCurrentContext(
                        gl, config, config.warmupFrames, config.measureFrames, &timings);
                    report.measuredFrames = static_cast<u32>(timings.size());
                    report.meanFrameMs = meanOf(timings);
                    report.p95FrameMs = percentileOf(timings, 0.95);
                    report.minFrameMs = minimumOf(timings);
                    report.maxFrameMs = maximumOf(timings);
                    if (!run.frame.usable())
                        report.mainThreadFailure +=
                            std::string(report.mainThreadFailure.empty() ? "" : "; ") +
                            "frame-cost pass produced no usable frame";
                }
            }
        }

        // Two contexts alive at once. On a platform that permits off-thread
        // drawables this is what `kDefaultMaxConcurrent = 2` is betting on; here it
        // is measured on the thread that owns them, because that is the only thread
        // on which they exist.
        if (config.measureDualContext) {
            auto fail = [&report](std::string reason) {
                report.dualContextMeasured = false;
                report.dualFailure = std::move(reason);
            };
            QOpenGLContext firstContext;
            QOpenGLContext secondContext;
            std::vector<double> alternating;
            // Each context gets its own projectM instance, which is what two real
            // workers would hold. The first is booted *completely* -- create, expose,
            // makeCurrent, one frame -- before the second context is created at all.
            // Creating both first and booting afterwards measured a silent downgrade:
            // the second-and-later contexts reported `format() == 2.1`, which projectM
            // cannot run at all because its shaders are #version 330. Booting in
            // sequence negotiated 4.1 core for both, so the order is the fix and not
            // luck.
            QOpenGLFunctions_3_3_Core firstGl;
            QOpenGLFunctions_3_3_Core secondGl;
            auto boot = [&](QWindow& window,
                            QOpenGLContext& context,
                            QOpenGLFunctions_3_3_Core& gl,
                            pm::Engine& engine) {
                window.setSurfaceType(QWindow::OpenGLSurface);
                window.setFormat(coreContextFormat());
                window.resize(static_cast<int>(config.width),
                              static_cast<int>(config.height));
                window.show();
                if (window.winId() == 0)
                    return std::string{"QWindow::show() produced no native handle"};
                context.setFormat(coreContextFormat());
                if (!context.create())
                    return std::string{"QOpenGLContext::create() failed"};
                if (!exposeThenMakeCurrent(context, window)) {
                    return window.isExposed()
                               ? std::string{"makeCurrent failed for this window"}
                               : std::string{"the window never became exposed"};
                }
                if (!gl.initializeOpenGLFunctions())
                    return std::string{"QOpenGLFunctions_3_3_Core init failed for a "} +
                           std::to_string(context.format().majorVersion()) + "." +
                           std::to_string(context.format().minorVersion()) +
                           " context; projectM needs 3.3";
                pm::EngineConfig engineConfig;
                engineConfig.width = config.width;
                engineConfig.height = config.height;
                engineConfig.fps = config.fps;
                const auto init = engine.init(engineConfig);
                if (!init)
                    return "projectM init failed: " + init.error().message;
                const std::vector<f32> warm = percussiveTick(config, 0);
                engine.addPCMDataInterleaved(warm.data(),
                                             static_cast<u32>(warm.size() / 2),
                                             2);
                gl.glViewport(0,
                              0,
                              static_cast<GLsizei>(config.width),
                              static_cast<GLsizei>(config.height));
                engine.render();
                gl.glFinish();
                return std::string{};
            };
            pm::Engine firstEngine;
            pm::Engine secondEngine;
            QWindow firstWindow;
            QWindow secondWindow;
            const std::string firstError =
                boot(firstWindow, firstContext, firstGl, firstEngine);
            const std::string secondError =
                firstError.empty()
                    ? boot(secondWindow, secondContext, secondGl, secondEngine)
                    : std::string{"not attempted because context 1 did not come up"};
            if (!firstError.empty() || !secondError.empty()) {
                fail("context 1: " + (firstError.empty() ? std::string("ok") : firstError) +
                     "; context 2: " + (secondError.empty() ? std::string("ok") : secondError));
            } else {
                alternating.reserve(config.measureFrames);
                for (u32 i = 0; i < config.measureFrames; ++i) {
                    const bool useFirst = (i % 2) == 0;
                    QOpenGLFunctions_3_3_Core& gl = useFirst ? firstGl : secondGl;
                    pm::Engine& engine = useFirst ? firstEngine : secondEngine;
                    QOpenGLContext& context = useFirst ? firstContext : secondContext;
                    if (!context.makeCurrent(useFirst ? &firstWindow : &secondWindow))
                        continue;
                    const std::vector<f32> pcm = percussiveTick(config, i);
                    const auto start = Clock::now();
                    engine.addPCMDataInterleaved(pcm.data(),
                                                 static_cast<u32>(pcm.size() / 2),
                                                 2);
                    gl.glViewport(0,
                                  0,
                                  static_cast<GLsizei>(config.width),
                                  static_cast<GLsizei>(config.height));
                    engine.render();
                    gl.glFinish();
                    alternating.push_back(std::chrono::duration<double, std::milli>(
                                              Clock::now() - start)
                                              .count());
                }
                report.dualMeasuredFrames = static_cast<u32>(alternating.size());
                report.dualContextMeasured = !alternating.empty();
                report.dualMeanFrameMs = meanOf(alternating);
                report.dualP95FrameMs = percentileOf(alternating, 0.95);
                report.dualMinFrameMs = minimumOf(alternating);
                report.dualMaxFrameMs = maximumOf(alternating);
                if (!report.dualContextMeasured)
                    fail("both contexts booted but no frame could be made current");
            }
        }
    }

    // ---------------------------------------------------------------------- teardown
    // One context and one projectM instance created and destroyed per cycle, with
    // a full-resolution readback each time, because that is exactly what one render
    // job costs. A leaked context per job is invisible in a single run and obvious
    // at four hundred.
    if (config.measureTeardown && config.teardownCycles > 0) {
        QWindow window;
        window.setSurfaceType(QWindow::OpenGLSurface);
        window.setFormat(coreContextFormat());
        window.resize(static_cast<int>(config.width), static_cast<int>(config.height));
        window.show();
        if (window.winId() != 0 && waitForExpose(window)) {
            report.teardownCycles = config.teardownCycles;
            report.rssBeforeBytes = residentSetBytes();
            usize peak = report.rssBeforeBytes;
            report.teardownRssBytes.reserve(config.teardownCycles + 1);
            report.teardownRssBytes.push_back(report.rssBeforeBytes);
            std::vector<u8> pixels(static_cast<usize>(config.width) * config.height * 4, 0);
            double cycleMs = 0;
            u32 completed = 0;
            // One cycle's work as a lambda rather than a loop body full of
            // `continue`s, because the RSS series has to contain one reading per
            // *attempted* cycle: a series that silently omits the cycles that bailed
            // out early cannot support a slope claim.
            const auto runCycle = [&](u32 cycle) {
                QOpenGLContext context;
                context.setFormat(coreContextFormat());
                if (!context.create() || !context.makeCurrent(&window))
                    return false;
                QOpenGLFunctions_3_3_Core gl;
                if (!gl.initializeOpenGLFunctions())
                    return false;
                pm::Engine engine;
                if (config.teardownInitializeProjectm) {
                    pm::EngineConfig engineConfig;
                    engineConfig.width = config.width;
                    engineConfig.height = config.height;
                    engineConfig.fps = config.fps;
                    if (!engine.init(engineConfig))
                        return false;
                    const std::vector<f32> pcm = percussiveTick(config, cycle);
                    engine.addPCMDataInterleaved(pcm.data(),
                                                 static_cast<u32>(pcm.size() / 2),
                                                 2);
                    gl.glViewport(0,
                                  0,
                                  static_cast<GLsizei>(config.width),
                                  static_cast<GLsizei>(config.height));
                    engine.render();
                    gl.glFinish();
                }
                if (!config.teardownReadback)
                    return true;
                gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
                gl.glReadPixels(0,
                                0,
                                static_cast<GLsizei>(config.width),
                                static_cast<GLsizei>(config.height),
                                GL_RGBA,
                                GL_UNSIGNED_BYTE,
                                pixels.data());
                const auto nonZero = static_cast<u64>(
                    std::count_if(pixels.begin(), pixels.end(), [](u8 b) { return b != 0; }));
                return nonZero > 0;
            };
            for (u32 cycle = 0; cycle < config.teardownCycles; ++cycle) {
                const auto start = Clock::now();
                const bool drew = runCycle(cycle);
                ++completed;
                if (drew || !config.teardownReadback)
                    ++report.teardownRenderedCycles;
                cycleMs += std::chrono::duration<double, std::milli>(Clock::now() - start)
                               .count();
                const usize now = residentSetBytes();
                peak = std::max(peak, now);
                report.teardownRssBytes.push_back(now);
            }
            // A loop in which no cycle completed has not measured a teardown, so it
            // must not claim to have. Reporting it as available is how a leak number
            // for a platform that never rendered gets quoted as a leak.
            report.rssAvailable = completed > 0;
            report.teardownCycleMs = completed > 0 ? cycleMs / static_cast<double>(completed) : 0.0;
            // Taken from the series rather than re-read here: two RSS readings taken a
            // microsecond apart differ, and a report whose `rssAfterBytes` disagrees
            // with its own last series entry cannot be checked by a reader.
            report.rssAfterBytes = report.teardownRssBytes.empty()
                                       ? report.rssBeforeBytes
                                       : report.teardownRssBytes.back();
            report.peakRssBytes = peak;
            report.rssDeltaBytes = static_cast<i64>(report.rssAfterBytes) -
                                   static_cast<i64>(report.rssBeforeBytes);
        } else {
            report.rssAvailable = false;
            report.mainThreadFailure +=
                std::string(report.mainThreadFailure.empty() ? "" : "; ") +
                "teardown pass could not obtain a window drawable";
        }
    }

    // ----------------------------------------------------------------------- verdict
    // Derived from what was observed, never from a platform check, so the same
    // tree answers the question on Linux and Windows too.
    if (!report.mainThreadDrawableAvailable) {
        report.verdict = RenderThreadVerdict::Unsupported;
        report.reason = report.mainThreadFailure.empty()
                            ? "no window-backed GL context was available for the control "
                              "measurement on the GUI thread"
                            : "no window-backed GL context was available for the control "
                              "measurement: " + report.mainThreadFailure;
    } else if (report.offThreadFrame.usable()) {
        report.verdict = RenderThreadVerdict::Supported;
        report.reason = "a background-thread context produced a full-resolution frame with "
                        + std::to_string(report.offThreadFrame.nonZeroBytes) + " of " +
                        std::to_string(report.offThreadFrame.bytes) +
                        " non-zero bytes, so workers may render off the GUI thread";
    } else if (report.offThreadContextCurrent) {
        report.verdict = RenderThreadVerdict::NeedsMainThread;
        report.reason =
            "the context creates and becomes current on a worker thread (projectM "
            "initialized: " +
            std::string(report.offThreadProjectmInitialized ? "yes" : "no") +
            ", frames rendered: " + std::to_string(report.offThreadFrame.framesRendered) +
            "), but the drawable it gets there does not hold what is written to it: "
            "GL_RED_BITS=" + std::to_string(report.offThreadFrame.redBits) +
            " readPixels error 0x" + std::to_string(report.offThreadFrame.glError) +
            " nonZero=" + std::to_string(report.offThreadFrame.nonZeroBytes) + "/" +
            std::to_string(report.offThreadFrame.bytes) + " sentinelHeld=" +
            std::string(report.offThreadFrame.sentinelHeld ? "yes" : "no") +
            " sentinelReadError=0x" +
            std::to_string(report.offThreadFrame.sentinelReadError) +
            ". The GL work has to be marshalled onto the GUI thread, so N concurrent "
            "render jobs become N interleaved jobs over one context.";
    } else {
        report.verdict = RenderThreadVerdict::Unsupported;
        report.reason = "no GL context could be created or made current on a worker thread" +
                        (report.offThreadFailure.empty() ? "" : ": " + report.offThreadFailure);
    }

    return report;
}

std::vector<std::string> offscreenProbeLines(const OffscreenProbeReport& report) {
    const auto fixed = [](double value, int precision) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return std::string(buffer);
    };
    return {
        "platform: " + report.platform,
        "verdict: " + std::string(toString(report.verdict)),
        "reason: " + report.reason,
        "off-thread: contextCreated=" + std::string(report.offThreadContextCreated ? "yes" : "no") +
            " current=" + std::string(report.offThreadContextCurrent ? "yes" : "no") +
            " projectmInit=" +
            std::string(report.offThreadProjectmInitialized ? "yes" : "no") +
            " framesRendered=" + std::to_string(report.offThreadFrame.framesRendered),
        "off-thread frame: " + std::to_string(report.offThreadFrame.width) + "x" +
            std::to_string(report.offThreadFrame.height) + " bytes=" +
            std::to_string(report.offThreadFrame.bytes) + " nonZero=" +
            std::to_string(report.offThreadFrame.nonZeroBytes) + " (" +
            fixed(report.offThreadFrame.nonZeroFraction * 100.0, 3) +
            "%) redBits=" + std::to_string(report.offThreadFrame.redBits) +
            " depthBits=" + std::to_string(report.offThreadFrame.depthBits) +
            " glError=0x" + std::to_string(report.offThreadFrame.glError) +
            " checksum=" + std::to_string(report.offThreadFrame.checksum) +
            " sentinelHeld=" + std::string(report.offThreadFrame.sentinelHeld ? "yes" : "no") +
            " sentinelClearErr=0x" + std::to_string(report.offThreadFrame.sentinelClearError) +
            " sentinelReadErr=0x" + std::to_string(report.offThreadFrame.sentinelReadError),
        "main-thread control: available=" +
            std::string(report.mainThreadDrawableAvailable ? "yes" : "no") +
            " projectmInit=" + std::string(report.mainThreadProjectmInitialized ? "yes" : "no"),
        "main-thread frame: nonZero=" +
            std::to_string(report.mainThreadFrame.nonZeroBytes) + " of " +
            std::to_string(report.mainThreadFrame.bytes) + " (" +
            fixed(report.mainThreadFrame.nonZeroFraction * 100.0, 3) + "%) redBits=" +
            std::to_string(report.mainThreadFrame.redBits) + " depthBits=" +
            std::to_string(report.mainThreadFrame.depthBits) + " checksum=" +
            std::to_string(report.mainThreadFrame.checksum) + " sentinelHeld=" +
            std::string(report.mainThreadFrame.sentinelHeld ? "yes" : "no"),
        "frame cost: n=" + std::to_string(report.measuredFrames) + " min=" +
            fixed(report.minFrameMs, 3) + " mean=" + fixed(report.meanFrameMs, 3) +
            " p95=" + fixed(report.p95FrameMs, 3) + " max=" +
            fixed(report.maxFrameMs, 3) + " ms",
        "dual context: measured=" + std::string(report.dualContextMeasured ? "yes" : "no") +
            " n=" + std::to_string(report.dualMeasuredFrames) + " min=" +
            fixed(report.dualMinFrameMs, 3) + " mean=" + fixed(report.dualMeanFrameMs, 3) +
            " p95=" + fixed(report.dualP95FrameMs, 3) + " max=" +
            fixed(report.dualMaxFrameMs, 3) + " ms" + (report.dualFailure.empty() ? "" : " failure=" + report.dualFailure),
        "teardown rss series (MiB): " + [&report] {
            std::string joined;
            for (const usize value : report.teardownRssBytes) {
                joined += std::to_string(double(value) / (1024.0 * 1024.0));
                joined += ' ';
            }
            return joined;
        }(),
        "teardown: cycles=" + std::to_string(report.teardownCycles) + " rendered=" +
            std::to_string(report.teardownRenderedCycles) + " rss " +
            std::to_string(report.rssBeforeBytes) + " -> " +
            std::to_string(report.rssAfterBytes) + " (" +
            std::to_string(report.rssDeltaBytes) + " bytes) peak=" +
            std::to_string(report.peakRssBytes) + " rssAvailable=" +
            std::string(report.rssAvailable ? "yes" : "no") + " cycleMean=" +
            fixed(report.teardownCycleMs, 3) + " ms",
    };
}

std::string describeOffscreenProbe(const OffscreenProbeReport& report) {
    const std::vector<std::string> lines = offscreenProbeLines(report);
    std::string joined;
    for (const std::string& line : lines) {
        joined += line;
        joined += '\n';
    }
    if (!joined.empty())
        joined.pop_back();
    return joined;
}

} // namespace vc