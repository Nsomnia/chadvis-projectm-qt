// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Visualizer renderer implementation with lock-free queue

#include "VisualizerRenderer.hpp"
#include "audio/AudioQueue.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "recorder/FrameGrabber.hpp"
#include <chrono>


namespace vc {

VisualizerRenderer::VisualizerRenderer() = default;

VisualizerRenderer::~VisualizerRenderer() {
    cleanup();
}

void VisualizerRenderer::initialize(u32 width, u32 height) {
    if (!initializeOpenGLFunctions()) {
        LOG_ERROR("VisualizerRenderer: Failed to initialize OpenGL functions");
        return;
    }

    const auto& vizConfig = CONFIG.visualizer();
    if (vizConfig.fps > 0)
        targetFps_ = vizConfig.fps;
    const auto pmConfig = pm::ProjectMConfig::fromVisualizer(vizConfig, width, height);

    projectM_.presetLoading.connect(
            [this](bool loading) { presetLoading_ = loading; });

    if (auto result = projectM_.init(pmConfig); !result) {
        LOG_ERROR("VisualizerRenderer: projectM init failed: {}",
            result.error().message);
        return;
    }

    LOG_INFO("VisualizerRenderer: Initialized successfully ({}x{})", width, height);
    initialized_ = true;
}

void VisualizerRenderer::cleanup() {
    recording_ = false;
    destroyPBOs();
    projectM_.shutdown();
    captureTarget_.destroy();
    initialized_ = false;
}

void VisualizerRenderer::render(u32 width, u32 height, bool isExposed) {
render(0, 0, width, height, isExposed);
}

void VisualizerRenderer::render(u32 x, u32 y, u32 width, u32 height, bool isExposed) {
if (!initialized_ || !isExposed)
return;
renderFrame(x, y, width, height);
}

void VisualizerRenderer::renderFrame(u32 x, u32 y, u32 w, u32 h) {
    if (w == 0 || h == 0 || !projectM_.isInitialized())
        return;

    projectM_.syncState();

    // Pop audio from lock-free queue (no mutex)
    if (audioQueue_) {
        u32 framesToFeed = (audioSampleRate_ + targetFps_ - 1) / targetFps_;
        static constexpr usize BATCH_BUFFER_SIZE = 4096;
        alignas(64) float batchBuffer[BATCH_BUFFER_SIZE * 2];

        u32 popped = audioQueue_->popVizBatch(batchBuffer, framesToFeed);
        if (popped > 0) {
            projectM_.engine().addPCMDataInterleaved(batchBuffer, popped, 2);
        }
    }

    // projectM v4 has exactly one render destination: the default framebuffer.
    // Its final texture copy is preceded by an unconditional
    // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) carrying the upstream comment
    // "ToDo: Allow external apps to provide a custom target framebuffer", so an
    // FBO bound here is never written and holds nothing but its own clear
    // colour. The picture therefore lands in framebuffer 0, the recorder reads
    // it back out of that same buffer, and the live view stays untouched.
    glViewport(x, y, w, h);
    glScissor(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);

    if (presetLoading_) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        projectM_.engine().resize(w, h);
        projectM_.engine().render();
        // Presets leave alpha undefined; force it opaque so neither the
        // encoder nor the readback sees garbage in the fourth channel.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    glDisable(GL_SCISSOR_TEST);

    if (recording_)
        captureDefaultFramebuffer(w, h);
}

void VisualizerRenderer::setupPBOs() {
    destroyPBOs();
    glGenBuffers(2, pbos_);
    u32 size = recordWidth_ * recordHeight_ * 4;
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, size, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    pboIndex_ = 0;
    pboAvailable_ = false;
}

void VisualizerRenderer::destroyPBOs() {
    if (pbos_[0])
        glDeleteBuffers(2, pbos_);
    pbos_[0] = pbos_[1] = 0;
}

// Reads the recorded frame out of framebuffer 0, where projectM put it. When
// the recording resolution differs from the window's, the readback is scaled
// first: projectM cannot render into an FBO, so a size change has to be a copy
// taken after the fact rather than a different render target.
void VisualizerRenderer::captureDefaultFramebuffer(u32 width, u32 height) {
    if (recordWidth_ == 0 || recordHeight_ == 0 || width == 0 || height == 0)
        return;

    GLuint source = 0;
    if (recordWidth_ != width || recordHeight_ != height) {
        if (captureTarget_.width() != recordWidth_ ||
            captureTarget_.height() != recordHeight_) {
            const auto created =
                    captureTarget_.create(recordWidth_, recordHeight_, false);
            if (!created) {
                LOG_ERROR("VisualizerRenderer: capture target failed: {}",
                          created.error().message);
                return;
            }
        }
        captureTarget_.blitFromDefault(width, height, true);
        source = captureTarget_.fbo();
    }

    captureAsync(source);
}

void VisualizerRenderer::captureAsync(GLuint readFramebuffer) {
    const u32 nextIndex = (pboIndex_ + 1) % 2;
    const u32 size = recordWidth_ * recordHeight_ * 4;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[pboIndex_]);
    glReadPixels(0,
                 0,
                 recordWidth_,
                 recordHeight_,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);
    if (pboAvailable_) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[nextIndex]);
        auto* ptr = static_cast<u8*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
        if (ptr) {
            std::vector<u8> buffer(ptr, ptr + size);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            // glReadPixels hands back rows from the bottom of the framebuffer
            // up; the encoder uploads them as-is, so the picture would land in
            // the file upside down without this.
            FrameGrabber::flipImage(buffer, recordWidth_, recordHeight_);
            frameCaptured.emitSignal(
                    std::move(buffer),
                    recordWidth_,
                    recordHeight_,
                    std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    pboIndex_ = nextIndex;
    pboAvailable_ = true;
}

void VisualizerRenderer::setRecordingSize(u32 width, u32 height) {
    recordWidth_ = width;
    recordHeight_ = height;
}

void VisualizerRenderer::startRecording() {
    if (!initialized_ || !projectM_.isInitialized()) {
        LOG_WARN("VisualizerRenderer: Cannot start recording before initialization");
        return;
    }
    if (recordWidth_ == 0 || recordHeight_ == 0) {
        LOG_ERROR("VisualizerRenderer: Refusing to record at {}x{}", recordWidth_,
                  recordHeight_);
        return;
    }
    if (recordWidth_ % 2 != 0 || recordHeight_ % 2 != 0) {
        LOG_ERROR("VisualizerRenderer: Refusing to record at odd resolution "
                  "{}x{}; chroma-subsampled formats need even dimensions",
                  recordWidth_, recordHeight_);
        return;
    }

    // The encoder stamps presentation timestamps from its own frame counter at
    // record fps, and the renderer produces frames at visualizer fps. Any
    // disagreement silently retimes the finished file, so say so loudly rather
    // than writing a video that runs at the wrong speed.
    const u32 recordFps = CONFIG.recording().video.fps;
    if (recordFps != targetFps_) {
        LOG_WARN("VisualizerRenderer: recording {} fps at {}x{} but the visualizer "
                 "renders {} fps; the encoded file will be retimed",
                 recordFps, recordWidth_, recordHeight_, targetFps_);
    }

    recording_ = true;
    setupPBOs();
}

void VisualizerRenderer::stopRecording() {
    recording_ = false;
    destroyPBOs();
    captureTarget_.destroy();
}

} // namespace vc
