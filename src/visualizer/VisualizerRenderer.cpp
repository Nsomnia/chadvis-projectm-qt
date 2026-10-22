// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Visualizer renderer implementation with lock-free queue

#include "VisualizerRenderer.hpp"
#include <chrono>
#include "audio/AudioQueue.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "recorder/FrameGrabber.hpp"


namespace vc {

namespace {

/// Saves and restores the GL state projectM is allowed to disturb.
///
/// Upstream will not do this: PR #981 ("preserve gl state of calling
/// application") was reviewed and closed unmerged on 2026-03-10, with the
/// maintainer's own "Can't test it currently so someone else would have to do
/// it." libprojectM resets only FBO 0 and the viewport, and its GLES comment
/// warns that per-FBO draw-buffer state "leaks into FBO 0 on some drivers". The
/// host therefore owns viewport, scissor, colour mask, blend, depth and the
/// framebuffer bindings around every projectM call (T0027).
///
/// Deliberately not included: texture units/bindings and the program object.
/// projectM binds its own textures and shaders, but nothing the host draws
/// afterwards shares this context -- the Qt Quick scene graph is a sibling on a
/// separate surface -- so restoring them buys nothing and adds calls to the
/// per-frame path. The listed set is the one a following render in *this*
/// context actually reads.
class GlStateGuard {
public:
    explicit GlStateGuard(QOpenGLFunctions_3_3_Core& gl) : gl_(gl) {
        gl_.glGetIntegerv(GL_VIEWPORT, viewport_);
        gl_.glGetIntegerv(GL_SCISSOR_BOX, scissor_);
        gl_.glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_);
        gl_.glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb_);
        gl_.glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb_);
        gl_.glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha_);
        gl_.glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha_);
        gl_.glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb_);
        gl_.glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha_);
        gl_.glGetIntegerv(GL_DEPTH_FUNC, &depthFunc_);
        gl_.glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask_);
        gl_.glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode_);
        gl_.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        gl_.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
        scissorEnabled_ = gl_.glIsEnabled(GL_SCISSOR_TEST);
        blendEnabled_ = gl_.glIsEnabled(GL_BLEND);
        depthEnabled_ = gl_.glIsEnabled(GL_DEPTH_TEST);
        cullEnabled_ = gl_.glIsEnabled(GL_CULL_FACE);
    }

    ~GlStateGuard() {
        gl_.glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer_));
        gl_.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer_));
        gl_.glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        gl_.glScissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
        gl_.glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        gl_.glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb_),
                                static_cast<GLenum>(blendDstRgb_),
                                static_cast<GLenum>(blendSrcAlpha_),
                                static_cast<GLenum>(blendDstAlpha_));
        gl_.glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb_),
                                    static_cast<GLenum>(blendEquationAlpha_));
        gl_.glDepthFunc(static_cast<GLenum>(depthFunc_));
        gl_.glDepthMask(depthWriteMask_);
        gl_.glCullFace(static_cast<GLenum>(cullFaceMode_));
        restoreCapability(GL_SCISSOR_TEST, scissorEnabled_);
        restoreCapability(GL_BLEND, blendEnabled_);
        restoreCapability(GL_DEPTH_TEST, depthEnabled_);
        restoreCapability(GL_CULL_FACE, cullEnabled_);
    }

    GlStateGuard(const GlStateGuard&) = delete;
    GlStateGuard& operator=(const GlStateGuard&) = delete;

private:
    void restoreCapability(const GLenum capability, const GLboolean enabled) {
        if (enabled == GL_TRUE) gl_.glEnable(capability);
        else gl_.glDisable(capability);
    }

    QOpenGLFunctions_3_3_Core& gl_;
    GLint viewport_[4]{};
    GLint scissor_[4]{};
    GLboolean colorMask_[4]{};
    GLint blendSrcRgb_{};
    GLint blendDstRgb_{};
    GLint blendSrcAlpha_{};
    GLint blendDstAlpha_{};
    GLint blendEquationRgb_{};
    GLint blendEquationAlpha_{};
    GLint depthFunc_{};
    GLboolean depthWriteMask_{};
    GLint cullFaceMode_{};
    GLint readFramebuffer_{};
    GLint drawFramebuffer_{};
    GLboolean scissorEnabled_{};
    GLboolean blendEnabled_{};
    GLboolean depthEnabled_{};
    GLboolean cullEnabled_{};
};

} // namespace

VisualizerRenderer::VisualizerRenderer(PresetManager& presetManager) : projectM_(presetManager) {}

VisualizerRenderer::~VisualizerRenderer() { cleanup(); }

void VisualizerRenderer::initialize(u32 width, u32 height) {
    if (!initializeOpenGLFunctions()) {
        LOG_ERROR("VisualizerRenderer: Failed to initialize OpenGL functions");
        return;
    }

    const auto& vizConfig = CONFIG.visualizer();
    if (vizConfig.fps > 0) targetFps_ = vizConfig.fps;
    const auto pmConfig = pm::ProjectMConfig::fromVisualizer(vizConfig, width, height);

    projectM_.presetLoading.connect([this](bool loading) { presetLoading_ = loading; });

    if (auto result = projectM_.init(pmConfig); !result) {
        LOG_ERROR("VisualizerRenderer: projectM init failed: {}", result.error().message);
        return;
    }

    LOG_INFO("VisualizerRenderer: Initialized successfully ({}x{})", width, height);
    initialized_ = true;
}

void VisualizerRenderer::cleanup() {
    recording_ = false;
    if (abandoned_) {
        // T0026: the GL handles were deliberately leaked by abandonGlObjects()
        // because there was no usable context to delete them under. Touching
        // them again here would repeat the exact hazard the abandon avoided.
        initialized_ = false;
        return;
    }
    destroyPBOs();
    projectM_.shutdown();
    captureTarget_.destroy();
    initialized_ = false;
}

void VisualizerRenderer::abandonGlObjects() {
    recording_ = false;
    // Order matters only for clarity: each abandon is independent and none
    // issues a GL call. destroyPBOs() is skipped -- it would glDeleteBuffers
    // under whatever context (if any) happens to be current -- so the PBO
    // names are zeroed here and the buffers leak.
    pbos_[0] = pbos_[1] = 0;
    pboIndex_ = 0;
    pboAvailable_ = false;
    captureTarget_.abandon();
    projectM_.abandon();
    abandoned_ = true;
    initialized_ = false;
}

void VisualizerRenderer::render(u32 width, u32 height, bool isExposed) {
    render(0, 0, width, height, isExposed);
}

void VisualizerRenderer::render(u32 x, u32 y, u32 width, u32 height, bool isExposed) {
    if (!initialized_ || !isExposed) return;
    renderFrame(x, y, width, height);
}

void VisualizerRenderer::renderFrame(u32 x, u32 y, u32 w, u32 h) {
    if (w == 0 || h == 0 || !projectM_.isInitialized()) return;

    // The only hand-off of this context to upstream-owned code, so the callers'
    // state is saved here and restored on every path out -- including the capture
    // below, which changes the read framebuffer and PBO binding itself.
    GlStateGuard glState(*this);

    projectM_.syncState();

    // Pop audio from lock-free queue (no mutex). The batch is sized from the
    // rate the audio thread actually pushed — the observed sink rate, 44.1
    // kHz included — not a hardcoded constant: a wrong rate misfeeds projectM
    // a proportional slice of each second's PCM and misaligns beat detection.
    if (audioQueue_) {
        static constexpr u32 BATCH_BUFFER_SIZE = 4096;
        alignas(64) float batchBuffer[BATCH_BUFFER_SIZE * 2];
        const u32 rate = audioQueue_->sampleRate();
        const u32 framesToFeed = framesPerRenderTick(rate, targetFps_, BATCH_BUFFER_SIZE);

        u32 popped = audioQueue_->popVizBatch(batchBuffer, framesToFeed);
        if (popped > 0) {
            if (!audioArrivalLogged_) {
                audioArrivalLogged_ = true;
                LOG_INFO("VisualizerRenderer: audio is reaching projectM ({} frames at {} Hz, "
                         "queue depth {}), so presets can react",
                         popped, rate, audioQueue_->vizDepth());
            }
            projectM_.engine().addPCMDataInterleaved(batchBuffer, popped, 2);
        } else if (!audioArrivalLogged_ && ++framesWithoutAudio_ >= 600) {
            // ~10 s at 60 fps, then every 10 s until audio arrives. Deliberately
            // bounded: a static canvas has two very different causes -- the
            // producer never pushed, or the consumer is not this queue -- and
            // the depth distinguishes them. `dropped` rules out a queue that
            // filled because nothing drained it. Without this line "the
            // visualizer does not react to music" is a report nobody can act on.
            framesWithoutAudio_ = 0;
            LOG_WARN("VisualizerRenderer: {} frames rendered with no PCM in the visualizer queue "
                     "(depth {}, dropped {}, rate {} Hz) -- nothing can react until audio reaches "
                     "this queue",
                     600, audioQueue_->vizDepth(), audioQueue_->vizDropCount(), rate);
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

    if (recording_) captureDefaultFramebuffer(w, h);
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
    if (pbos_[0]) glDeleteBuffers(2, pbos_);
    pbos_[0] = pbos_[1] = 0;
}

// Reads the recorded frame out of framebuffer 0, where projectM put it. When
// the recording resolution differs from the window's, the readback is scaled
// first: projectM cannot render into an FBO, so a size change has to be a copy
// taken after the fact rather than a different render target.
void VisualizerRenderer::captureDefaultFramebuffer(u32 width, u32 height) {
    if (recordWidth_ == 0 || recordHeight_ == 0 || width == 0 || height == 0) return;

    GLuint source = 0;
    if (recordWidth_ != width || recordHeight_ != height) {
        if (captureTarget_.width() != recordWidth_ || captureTarget_.height() != recordHeight_) {
            const auto created = captureTarget_.create(recordWidth_, recordHeight_, false);
            if (!created) {
                LOG_ERROR("VisualizerRenderer: capture target failed: {}", created.error().message);
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
    glReadPixels(0, 0, recordWidth_, recordHeight_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
            frameCaptured.emitSignal(std::move(buffer), recordWidth_, recordHeight_,
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
        LOG_ERROR("VisualizerRenderer: Refusing to record at {}x{}", recordWidth_, recordHeight_);
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
