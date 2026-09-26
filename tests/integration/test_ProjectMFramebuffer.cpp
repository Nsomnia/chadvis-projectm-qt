// Rendering-destination regression test for projectM v4.
//
// projectM renders into whatever framebuffer is bound when it reaches its final
// texture copy, and it hard-binds framebuffer 0 immediately before that copy
// (libprojectM/ProjectM.cpp: "ToDo: Allow external apps to provide a custom
// target framebuffer." then glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0)). So an
// offscreen FBO is never written by projectM: anything the recorder captures
// from such an FBO is only ever that FBO's own glClearColor. These tests pin
// the destination that does receive pixels, which is the default framebuffer,
// and prove it with a non-black readback rather than a "frame is not null"
// check that a solid black buffer would pass.

#include <QGuiApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QtTest>

#include "core/Config.hpp"
#include "recorder/EncoderSettings.hpp"
#include "recorder/FFmpegUtils.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/Types.hpp"
#include "visualizer/RenderTarget.hpp"
#include "visualizer/VisualizerRenderer.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include "visualizer/projectm/Engine.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace vc;

namespace {

constexpr u32 kFrameSize = 256;
constexpr u32 kSampleRate = 48000;
constexpr u32 kFps = 60;
constexpr u32 kFrames = 30;
constexpr qint64 kExposureTimeoutMs = 5000;

// Whether this QPA plugin can produce a GL context at all. The macOS
// "offscreen" plugin cannot ("This plugin does not support
// createPlatformOpenGLContext!"), and a GL test that quietly passed there would
// be a test that proves nothing, so every GL test in this file skips with a
// stated reason instead.
QByteArray glUnavailableReason() {
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    QOpenGLContext probe;
    probe.setFormat(format);
    if (probe.create())
        return {};
    return QByteArrayLiteral("QOpenGLContext failed to create; this QPA plugin "
                             "cannot provide a GL 3.3 core context");
}

// GL 3.3 core on a real window, matching VisualizerRenderer's
// QOpenGLFunctions_3_3_Core initialization and projectM's #version 330
// shaders. A QOffscreenSurface is not usable here: on macOS Qt gives it a 1x1
// drawable, so any readback larger than one pixel is out of bounds and comes
// back as zeros. Framebuffer 0 has to mean a real drawable, because a real
// drawable is the only destination projectM can be given.
class GlContextFixture {
public:
    explicit GlContextFixture(u32 width = kFrameSize, u32 height = kFrameSize)
        : width_(width), height_(height) {
        reason_ = glUnavailableReason();
        if (!reason_.isEmpty())
            return;

        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(3, 3);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setAlphaBufferSize(8);
        window_.setFormat(format);
        window_.setSurfaceType(QWindow::OpenGLSurface);
        window_.resize(static_cast<int>(width), static_cast<int>(height));
        window_.show();
        context_.setFormat(format);
        context_.create();
        if (!context_.isValid()) {
            reason_ = "QOpenGLContext failed to create; this QPA plugin cannot "
                      "provide a GL 3.3 core context";
            window_.hide();
            return;
        }
        if (!context_.makeCurrent(&window_)) {
            reason_ = QStringLiteral("QOpenGLContext::makeCurrent failed on the test "
                                     "window (window format %1 %2.%3 profile %4, "
                                     "context format %5 %6.%7 profile %8)")
                          .arg(static_cast<int>(window_.format().renderableType()))
                          .arg(window_.format().majorVersion())
                          .arg(window_.format().minorVersion())
                          .arg(static_cast<int>(window_.format().profile()))
                          .arg(static_cast<int>(context_.format().renderableType()))
                          .arg(context_.format().majorVersion())
                          .arg(context_.format().minorVersion())
                          .arg(static_cast<int>(context_.format().profile()))
                          .toLatin1();
            window_.hide();
            return;
        }
        // Exposure is delivered by the window system, so it needs the event
        // loop to run; an unexposed window has no drawable to read back from.
        QElapsedTimer deadline;
        deadline.start();
        while (!window_.isExposed() && deadline.elapsed() < kExposureTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QTest::qWait(5);
        }
        if (!window_.isExposed()) {
            reason_ = "test window never became exposed; no drawable to read back";
            context_.makeCurrent(nullptr);
            window_.hide();
            return;
        }
        functions_ = std::make_unique<QOpenGLFunctions_3_3_Core>();
        valid_ = functions_ && functions_->initializeOpenGLFunctions();
        if (!valid_)
            reason_ = "QOpenGLFunctions_3_3_Core failed to initialize";
    }

    ~GlContextFixture() {
        functions_.reset();
        if (context_.isValid()) {
            context_.makeCurrent(nullptr);
            context_.doneCurrent();
        }
        window_.hide();
    }

    GlContextFixture(const GlContextFixture&) = delete;
    GlContextFixture& operator=(const GlContextFixture&) = delete;

    [[nodiscard]] bool isValid() const { return valid_; }

    // Why the fixture is unusable, for a skip message that says something more
    // useful than "GL failed". The offscreen QPA plugin on macOS, for example,
    // reports "This plugin does not support createPlatformOpenGLContext!".
    [[nodiscard]] const QByteArray& invalidReason() const { return reason_; }

    // Describes the drawable the reads are taken from, so a zero-byte readback
    // is distinguishable from a genuinely black picture.
    [[nodiscard]] QString describe() const {
        if (!valid_)
            return QStringLiteral("no context");
        GLint maxDims[2]{0, 0};
        GLint viewport[4]{0, 0, 0, 0};
        GLint version = 0;
        auto& gl = const_cast<QOpenGLFunctions_3_3_Core&>(*functions_);
        gl.glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxDims);
        gl.glGetIntegerv(GL_VIEWPORT, viewport);
        gl.glGetIntegerv(GL_MAJOR_VERSION, &version);
        const GLenum error = gl.glGetError();
        return QStringLiteral("drawable=%1x%2 maxViewport=%3x%4 viewport=%5,%6 %7x%8 "
                              "glMajor=%9 glError=0x%10")
            .arg(width_)
            .arg(height_)
            .arg(maxDims[0])
            .arg(maxDims[1])
            .arg(viewport[0])
            .arg(viewport[1])
            .arg(viewport[2])
            .arg(viewport[3])
            .arg(version)
            .arg(static_cast<uint>(error), 0, 16);
    }

    QOpenGLFunctions_3_3_Core& gl() { return *functions_; }

    // Reads a rectangle of a framebuffer, bottom row first, the way OpenGL
    // hands pixels over.
    [[nodiscard]] static std::vector<u8> readPixels(GLenum readFramebuffer,
                                                    u32 width,
                                                    u32 height) {
        std::vector<u8> pixels(static_cast<usize>(width) * height * 4, 0);
        QOpenGLFunctions_3_3_Core gl;
        if (!gl.initializeOpenGLFunctions())
            return pixels;
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        gl.glPixelStorei(GL_PACK_ALIGNMENT, 4);
        gl.glReadPixels(0,
                        0,
                        static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height),
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        pixels.data());
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        return pixels;
    }

private:
    QWindow window_;
    u32 width_;
    u32 height_;
    QOpenGLContext context_;
    std::unique_ptr<QOpenGLFunctions_3_3_Core> functions_;
    QByteArray reason_;
    bool valid_{false};
};

// Deterministic percussive PCM so projectM's analysis has real energy to work
// with instead of a silent buffer that would leave every preset motionless.
void feedPcm(pm::Engine& engine, u32 frameIndex) {
    const u32 framesPerTick = kSampleRate / kFps;
    std::vector<f32> pcm(static_cast<usize>(framesPerTick) * 2);
    const f32 decay = std::exp(-4.0f * static_cast<f32>(frameIndex % 12) / 12.0f);
    for (u32 i = 0; i < framesPerTick; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(kSampleRate);
        const f32 bass = std::sin(2.0f * 3.14159265f * (55.0f + 40.0f * decay) * t);
        const f32 sample = bass * decay * 0.4f;
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
    }
    engine.addPCMDataInterleaved(pcm.data(), framesPerTick, 2);
}

// Boots a VisualizerRenderer against a GL context the fixture owns. The
// renderer refuses to initialize projectM when the preset directory does not
// exist, so it is pointed at an empty temporary directory and projectM falls
// back to its own idle preset.
class RendererHarness {
public:
    RendererHarness(GlContextFixture& fixture, u32 size)
        : fixture_(fixture), presetDir_() {
        const auto savedPresetPath = Config::instance().visualizer().presetPath;
        Config::instance().visualizer().presetPath =
            fs::path(presetDir_.path().toStdString());
        renderer_.initialize(size, size);
        Config::instance().visualizer().presetPath = savedPresetPath;
    }

    [[nodiscard]] bool isInitialized() const { return renderer_.projectM().isInitialized(); }
    VisualizerRenderer& renderer() { return renderer_; }

    // Records the first emitted frame and returns it, rendering up to
    // `maxFrames` times. The PBO pipeline emits the previous frame's readback,
    // so one render is never enough.
    std::vector<u8> captureFirstFrame(u32 width, u32 height, u32 maxFrames = 6) {
        std::vector<u8> captured;
        renderer_.frameCaptured.connect(
                [&](std::vector<u8> pixels, u32, u32, i64) {
                    if (captured.empty())
                        captured = std::move(pixels);
                });
        renderer_.setRecordingSize(width, height);
        renderer_.startRecording();
        for (u32 frame = 0; frame < maxFrames && captured.empty(); ++frame) {
            renderer_.render(0, 0, windowSize_, windowSize_, true);
            fixture_.gl().glFinish();
        }
        renderer_.stopRecording();
        return captured;
    }

    void setWindowSize(u32 size) { windowSize_ = size; }

private:
    GlContextFixture& fixture_;
    QTemporaryDir presetDir_;
    VisualizerRenderer renderer_;
    u32 windowSize_{kFrameSize};
};

// Row of an RGBA buffer.
std::vector<u8> row(const std::vector<u8>& pixels, u32 width, u32 index) {
    const usize rowSize = static_cast<usize>(width) * 4;
    return {pixels.begin() + static_cast<isize>(index * rowSize),
            pixels.begin() + static_cast<isize>((index + 1) * rowSize)};
}

// Decodes the leading video frames of a file and reports the brightest frame's
// lit-pixel count. Reading the luma plane of a yuv420p stream is enough: a
// transparent-black encode has nothing lit anywhere in it.
struct DecodedVideo {
    bool hasVideo{false};
    i32 width{0};
    i32 height{0};
    i64 bestLitPixels{0};
    i32 framesDecoded{0};
};

// The shared AVFormatContextDeleter is write-side only: it reads c->oformat,
// which a demuxer never sets. A reader has to close the input instead.
struct InputFormatDeleter {
    void operator()(AVFormatContext* context) const {
        if (context)
            avformat_close_input(&context);
    }
};

DecodedVideo decodeVideo(const std::string& path, i32 maxFrames) {
    DecodedVideo result;

    AVFormatContext* rawFormat = nullptr;
    if (avformat_open_input(&rawFormat, path.c_str(), nullptr, nullptr) != 0)
        return result;
    std::unique_ptr<AVFormatContext, InputFormatDeleter> format(rawFormat);
    if (!format)
        return result;
    if (avformat_find_stream_info(format.get(), nullptr) < 0)
        return result;

    const int streamIndex = av_find_best_stream(
            format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0)
        return result;
    AVStream* stream = format->streams[streamIndex];
    result.hasVideo = true;
    result.width = stream->codecpar->width;
    result.height = stream->codecpar->height;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return result;

    AVCodecContextPtr decoder(avcodec_alloc_context3(codec));
    if (!decoder)
        return result;
    if (avcodec_parameters_to_context(decoder.get(), stream->codecpar) < 0)
        return result;
    if (avcodec_open2(decoder.get(), codec, nullptr) < 0)
        return result;

    AVPacketPtr packet(av_packet_alloc());
    AVFramePtr frame(av_frame_alloc());
    while (result.framesDecoded < maxFrames &&
           av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index != streamIndex) {
            av_packet_unref(packet.get());
            continue;
        }
        if (avcodec_send_packet(decoder.get(), packet.get()) == 0) {
            while (avcodec_receive_frame(decoder.get(), frame.get()) == 0) {
                if (frame->format == AV_PIX_FMT_YUV420P ||
                    frame->format == AV_PIX_FMT_YUVJ420P) {
                    i64 lit = 0;
                    for (int y = 0; y < frame->height; ++y) {
                        const u8* row = frame->data[0] +
                                        static_cast<isize>(y) * frame->linesize[0];
                        for (int x = 0; x < frame->width; ++x) {
                            if (row[x] > 32)
                                ++lit;
                        }
                    }
                    result.bestLitPixels = std::max(result.bestLitPixels, lit);
                }
                ++result.framesDecoded;
                av_frame_unref(frame.get());
            }
        }
        av_packet_unref(packet.get());
    }
    return result;
}

u64 nonZeroBytes(const std::vector<u8>& pixels) {
    return static_cast<u64>(
            std::count_if(pixels.begin(), pixels.end(), [](u8 b) { return b != 0; }));
}

} // namespace

class TestProjectMFramebuffer : public QObject {
    Q_OBJECT

private slots:
    // The assertion that matters: after a handful of frames the *default*
    // framebuffer holds a picture. A `!frame.isNull()` style check would pass
    // on a solid black buffer and catch neither this bug nor the old one.
    void engineDrawsIntoDefaultFramebuffer() {
        GlContextFixture fixture;
        if (!fixture.isValid())
            QSKIP(fixture.invalidReason().constData());

        pm::EngineConfig config;
        config.width = kFrameSize;
        config.height = kFrameSize;
        config.fps = kFps;

        pm::Engine engine;
        const auto initResult = engine.init(config);
        QVERIFY2(initResult.isOk(),
                 initResult.isOk() ? "" : initResult.error().message.c_str());
        QVERIFY(engine.isInitialized());

        u64 bestNonZero = 0;
        for (u32 frame = 0; frame < kFrames; ++frame) {
            feedPcm(engine, frame);
            fixture.gl().glViewport(0, 0, kFrameSize, kFrameSize);
            engine.render();
            fixture.gl().glFinish();
            bestNonZero = std::max(bestNonZero,
                                   nonZeroBytes(GlContextFixture::readPixels(
                                           0, kFrameSize, kFrameSize)));
        }

        qInfo("projectM default framebuffer: %s; best non-zero byte count over %u frames: %llu",
              qPrintable(fixture.describe()),
              kFrames,
              static_cast<unsigned long long>(bestNonZero));
        QVERIFY2(bestNonZero > 0,
                 "projectM left the default framebuffer black across every frame");
    }

    // An FBO is not a projectM destination, no matter who binds it. This test
    // exists so the claim is a runtime fact rather than a reading of upstream
    // source, and it is why the renderer captures framebuffer 0. It binds the
    // FBO itself and calls the same render() the live path calls, so the only
    // thing under test is projectM's draw target.
    void engineIgnoresAnFboBoundAsDrawTarget() {
        GlContextFixture fixture;
        if (!fixture.isValid())
            QSKIP(fixture.invalidReason().constData());

        RenderTarget fbo;
        const auto createResult = fbo.create(kFrameSize, kFrameSize, true);
        QVERIFY2(createResult.isOk(),
                 createResult.isOk() ? "" : createResult.error().message.c_str());

        pm::EngineConfig config;
        config.width = kFrameSize;
        config.height = kFrameSize;
        config.fps = kFps;

        pm::Engine engine;
        const auto initResult = engine.init(config);
        QVERIFY2(initResult.isOk(),
                 initResult.isOk() ? "" : initResult.error().message.c_str());

        for (u32 frame = 0; frame < kFrames; ++frame) {
            feedPcm(engine, frame);
            fixture.gl().glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo.fbo());
            engine.render();
            fixture.gl().glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            fixture.gl().glFinish();
        }

        const u64 fboNonZero = nonZeroBytes(
                GlContextFixture::readPixels(fbo.fbo(), kFrameSize, kFrameSize));
        const u64 defaultNonZero = nonZeroBytes(
                GlContextFixture::readPixels(0, kFrameSize, kFrameSize));
        qInfo("offscreen FBO non-zero bytes: %llu; default framebuffer non-zero bytes: %llu",
              static_cast<unsigned long long>(fboNonZero),
              static_cast<unsigned long long>(defaultNonZero));
        QVERIFY2(defaultNonZero > 0,
                 "the default framebuffer is the destination and must hold a picture");
        QCOMPARE(fboNonZero, 0ULL);
    }

    // The same picture must survive the renderer's own recording path, which is
    // what a music-video encode actually consumes. Before the fix this is the
    // test that failed: the renderer read back an FBO projectM never wrote.
    void rendererRecordingPathDeliversPixels() {
        GlContextFixture fixture;
        if (!fixture.isValid())
            QSKIP(fixture.invalidReason().constData());

        RendererHarness harness(fixture, kFrameSize);
        QVERIFY(harness.isInitialized());

        const std::vector<u8> captured = harness.captureFirstFrame(kFrameSize, kFrameSize);
        QVERIFY2(!captured.empty(), "recording path emitted no frame");
        QCOMPARE(captured.size(), static_cast<usize>(kFrameSize) * kFrameSize * 4);

        qInfo("renderer capture: %llu non-zero bytes of %zu",
              static_cast<unsigned long long>(nonZeroBytes(captured)),
              captured.size());
        QVERIFY2(nonZeroBytes(captured) > 0,
                 "recording path delivered a fully transparent-black frame");
    }

    // Orientation, decided by comparison rather than by what a preset happens
    // to draw. glReadPixels returns the bottom row first; the encoder uploads
    // rows as they arrive, so a missing flip writes the file upside down and a
    // double flip flips it back. The first render is captured while the second
    // one is the frame projectM puts in the framebuffer, so the raw readback
    // taken between them is exactly the picture the emitted frame came from.
    void capturedFramesAreFlippedIntoTopDownRows() {
        GlContextFixture fixture;
        if (!fixture.isValid())
            QSKIP(fixture.invalidReason().constData());

        RendererHarness harness(fixture, kFrameSize);
        QVERIFY(harness.isInitialized());

        std::vector<u8> captured;
        harness.renderer().frameCaptured.connect(
                [&](std::vector<u8> pixels, u32, u32, i64) {
                    if (captured.empty())
                        captured = std::move(pixels);
                });
        harness.renderer().setRecordingSize(kFrameSize, kFrameSize);
        harness.renderer().startRecording();

        harness.renderer().render(0, 0, kFrameSize, kFrameSize, true);
        fixture.gl().glFinish();
        const std::vector<u8> rawFirstFrame =
                GlContextFixture::readPixels(0, kFrameSize, kFrameSize);
        harness.renderer().render(0, 0, kFrameSize, kFrameSize, true);
        fixture.gl().glFinish();

        QVERIFY2(!captured.empty(), "no frame was emitted to check orientation of");
        QCOMPARE(rawFirstFrame.size(), captured.size());
        QCOMPARE(nonZeroBytes(rawFirstFrame), nonZeroBytes(captured));

        QVERIFY2(captured != rawFirstFrame,
                 "captured rows are the raw bottom-up readback, i.e. upside down");
        QCOMPARE(row(captured, kFrameSize, 0), row(rawFirstFrame, kFrameSize, kFrameSize - 1));
        QCOMPARE(row(captured, kFrameSize, kFrameSize - 1), row(rawFirstFrame, kFrameSize, 0));
    }

    // The product-level claim, end to end: a real encoded file with a real
    // picture in it. Every earlier test would still pass with a pipeline that
    // encodes an empty buffer, because they all stop at the readback.
    void encodedFileContainsTheVisualizerPicture() {
        GlContextFixture fixture;
        if (!fixture.isValid())
            QSKIP(fixture.invalidReason().constData());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::string outputPath =
                (fs::path(dir.path().toStdString()) / "picture.mp4").string();

        EncoderSettings settings;
        settings.outputPath = fs::path(outputPath);
        settings.video.codec = VideoCodec::H264;
        settings.video.width = kFrameSize;
        settings.video.height = kFrameSize;
        settings.video.fps = 30;
        settings.video.crf = 18;
        settings.video.preset = EncoderPreset::Ultrafast;
        settings.audio.codec = AudioCodec::AAC;
        settings.audio.bitrate = 64;
        settings.audio.sampleRate = kSampleRate;
        settings.audio.channels = 2;
        settings.container = Container::MP4;

        VideoRecorder recorder;
        const auto startResult = recorder.start(settings);
        QVERIFY2(startResult.isOk(),
                 startResult.isOk() ? "" : startResult.error().message.c_str());

        RendererHarness harness(fixture, kFrameSize);
        QVERIFY(harness.isInitialized());
        harness.renderer().frameCaptured.connect(
                [&](std::vector<u8> pixels, u32 width, u32 height, i64 timestamp) {
                    recorder.submitVideoFrame(std::move(pixels), width, height, timestamp);
                });
        harness.renderer().setRecordingSize(kFrameSize, kFrameSize);
        harness.renderer().startRecording();

        constexpr u32 framesToEncode = 45;
        for (u32 frame = 0; frame < framesToEncode; ++frame) {
            harness.renderer().render(0, 0, kFrameSize, kFrameSize, true);
            fixture.gl().glFinish();
        }
        harness.renderer().stopRecording();

        QTRY_VERIFY_WITH_TIMEOUT(
            recorder.getCurrentStats().framesWritten >= framesToEncode - 2, 20000);
        const auto stopResult = recorder.stop();
        QVERIFY2(stopResult.isOk(),
                 stopResult.isOk() ? "" : stopResult.error().message.c_str());

        const DecodedVideo decoded = decodeVideo(outputPath, 5);
        QVERIFY2(decoded.hasVideo, "the encoded file has no video stream");
        QCOMPARE(decoded.width, static_cast<i32>(kFrameSize));
        QCOMPARE(decoded.height, static_cast<i32>(kFrameSize));
        QVERIFY(decoded.framesDecoded > 0);
        qInfo("encoded file: %dx%d, %d frames decoded, brightest frame has %lld lit pixels",
              decoded.width,
              decoded.height,
              decoded.framesDecoded,
              static_cast<long long>(decoded.bestLitPixels));
        QVERIFY2(decoded.bestLitPixels > 0,
                 "the encoded file is a black video");
    }

    // The live path users actually hit: the native VisualizerWindow with its
    // own context, expose event, render timer and buffer swap. The tests above
    // drive the renderer directly, so nothing here would catch a capture that
    // only works when the window is not presenting.
    void liveWindowCapturesFrames() {
        const QByteArray noGl = glUnavailableReason();
        if (!noGl.isEmpty())
            QSKIP(noGl.constData());

        QTemporaryDir presetDir;
        QVERIFY(presetDir.isValid());
        const fs::path savedPresetPath = Config::instance().visualizer().presetPath;
        Config::instance().visualizer().presetPath =
                fs::path(presetDir.path().toStdString());

        VisualizerWindow window;
        window.resize(kFrameSize, kFrameSize);
        window.show();

        std::vector<u8> captured;
        u32 capturedWidth = 0;
        u32 capturedHeight = 0;
        QObject::connect(&window,
                         &VisualizerWindow::frameCaptured,
                         [&](std::vector<u8> pixels, u32 width, u32 height, i64) {
                             if (captured.empty()) {
                                 captured = std::move(pixels);
                                 capturedWidth = width;
                                 capturedHeight = height;
                             }
                         });
        window.setRecordingSize(kFrameSize, kFrameSize);
        // Deferred until the first expose event initializes the renderer.
        window.startRecording();

        QElapsedTimer deadline;
        deadline.start();
        while (captured.empty() && deadline.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        window.stopRecording();
        Config::instance().visualizer().presetPath = savedPresetPath;

        QVERIFY2(!captured.empty(), "the live window never captured a frame");
        QCOMPARE(capturedWidth, kFrameSize);
        QCOMPARE(capturedHeight, kFrameSize);
        QCOMPARE(captured.size(), static_cast<usize>(kFrameSize) * kFrameSize * 4);
        qInfo("live window capture: %llu non-zero bytes of %zu",
              static_cast<unsigned long long>(nonZeroBytes(captured)),
              captured.size());
        QVERIFY2(nonZeroBytes(captured) > 0,
                 "the live window captured a fully transparent-black frame");
    }

    // A recording resolution that differs from the window's cannot be handed to
    // projectM, so the readback is scaled after the fact. Without that copy the
    // encoder would be fed a wrongly sized buffer and swscale would read past
    // the end of it.
    void recordingAtADifferentResolutionIsScaled() {
        GlContextFixture fixture;
        if (!fixture.isValid())
            QSKIP(fixture.invalidReason().constData());

        constexpr u32 recordSize = 128;
        RendererHarness harness(fixture, kFrameSize);
        QVERIFY(harness.isInitialized());

        const std::vector<u8> captured = harness.captureFirstFrame(recordSize, recordSize);
        QVERIFY2(!captured.empty(), "scaled recording path emitted no frame");
        QCOMPARE(captured.size(), static_cast<usize>(recordSize) * recordSize * 4);
        qInfo("scaled capture: %llu non-zero bytes of %zu",
              static_cast<unsigned long long>(nonZeroBytes(captured)),
              captured.size());
        QVERIFY2(nonZeroBytes(captured) > 0,
                 "scaling the readback produced a fully transparent-black frame");
    }
};

int runTestProjectMFramebuffer(int argc, char** argv) {
    TestProjectMFramebuffer test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ProjectMFramebuffer.moc"
