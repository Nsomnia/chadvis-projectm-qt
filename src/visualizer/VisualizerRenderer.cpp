// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Visualizer renderer implementation with lock-free queue

#include "VisualizerRenderer.hpp"
#include "audio/AudioQueue.hpp"
#include <chrono>
#include "core/Config.hpp"
#include "core/Logger.hpp"


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

    initBlitResources();

    const auto& vizConfig = CONFIG.visualizer();
    pm::ProjectMConfig pmConfig;
    pmConfig.width = width;
    pmConfig.height = height;
    pmConfig.fps = vizConfig.fps;
    pmConfig.beatSensitivity = vizConfig.beatSensitivity;
    pmConfig.presetPath = vizConfig.presetPath;
    pmConfig.presetDuration = vizConfig.presetDuration;
    pmConfig.transitionDuration = vizConfig.smoothPresetDuration;
    pmConfig.shufflePresets = vizConfig.shufflePresets;
    pmConfig.useDefaultPreset = vizConfig.useDefaultPreset;
    pmConfig.texturePaths = vizConfig.texturePaths;

    projectM_.presetLoading.connect(
            [this](bool loading) { presetLoading_ = loading; });

    if (auto result = projectM_.init(pmConfig); !result) {
        LOG_ERROR("VisualizerRenderer: projectM init failed: {}",
            result.error().message);
        return;
    }

    if (auto result = renderTarget_.create(width, height, true); !result) {
        LOG_ERROR("VisualizerRenderer: Failed to create render target: {}",
            result.error().message);
        return;
    }


    LOG_INFO("VisualizerRenderer: Initialized successfully ({}x{})", width, height);
    initialized_ = true;
}

void VisualizerRenderer::cleanup() {
    destroyPBOs();
    projectM_.shutdown();
    renderTarget_.destroy();
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

    if (recording_ && !renderTarget_.isValid())
        return;

    projectM_.syncState();

    u32 renderW = recording_ ? recordWidth_ : w;
    u32 renderH = recording_ ? recordHeight_ : h;

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

bool useFBO = recording_;

if (useFBO) {
if (renderTarget_.width() != renderW || renderTarget_.height() != renderH) {
renderTarget_.resize(renderW, renderH);
projectM_.engine().resize(renderW, renderH);
}

if (presetLoading_) {
renderTarget_.bind();
glClearColor(0, 0, 0, 1);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
renderTarget_.unbind();
} else {
projectM_.engine().renderToTarget(renderTarget_);
}

if (recording_) {
renderTarget_.bind();
captureAsync();
renderTarget_.unbind();
}

glViewport(x, y, w, h);
GLuint tex = renderTarget_.texture();
if (tex)
drawTexture(tex, w, h);
} else {
glViewport(x, y, w, h);
glScissor(x, y, w, h);
glEnable(GL_SCISSOR_TEST);

if (presetLoading_) {
glClearColor(0, 0, 0, 1);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
} else {
projectM_.engine().resize(w, h);
projectM_.engine().render();
glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
glClearColor(0, 0, 0, 1);
glClear(GL_COLOR_BUFFER_BIT);
glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
glDisable(GL_SCISSOR_TEST);
}
}

void VisualizerRenderer::initBlitResources() {
    if (blitProgram_)
        return;
    blitProgram_ = std::make_unique<QOpenGLShaderProgram>();
    const char* vertSource = R"(
        #version 330 core
        layout (location = 0) in vec2 position;
        layout (location = 1) in vec2 texCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            TexCoord = texCoord;
        }
    )";
    const char* fragSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 color;
        uniform sampler2D tex;
        void main() {
            vec4 c = texture(tex, TexCoord);
            color = vec4(c.rgb, 1.0);
        }
    )";
    blitProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertSource);
    blitProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSource);
    blitProgram_->link();

    float vertices[] = {-1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
                        1.0f,  -1.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f,
                        1.0f,  -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 1.0f};

    blitVao_.create();
    blitVao_.bind();
    blitVbo_.create();
    blitVbo_.bind();
    blitVbo_.allocate(vertices, sizeof(vertices));
    blitProgram_->enableAttributeArray(0);
    blitProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    blitProgram_->enableAttributeArray(1);
    blitProgram_->setAttributeBuffer(
            1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    blitVbo_.release();
    blitVao_.release();
}

void VisualizerRenderer::drawTexture(GLuint textureId, u32 w, u32 h) {
    if (!blitProgram_ || !textureId)
        return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    blitProgram_->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    blitProgram_->setUniformValue("tex", 0);
    blitVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    blitVao_.release();
    glBindTexture(GL_TEXTURE_2D, 0);
    blitProgram_->release();
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

void VisualizerRenderer::captureAsync() {
    u32 nextIndex = (pboIndex_ + 1) % 2;
    u32 size = recordWidth_ * recordHeight_ * 4;
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
        u8* ptr = (u8*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
            std::vector<u8> buffer(ptr, ptr + size);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
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
    pboIndex_ = nextIndex;
    pboAvailable_ = true;
}

void VisualizerRenderer::setRecordingSize(u32 width, u32 height) {
    recordWidth_ = width;
    recordHeight_ = height;
}

void VisualizerRenderer::startRecording() {
    recording_ = true;
    renderTarget_.resize(recordWidth_, recordHeight_);
    projectM_.engine().resize(recordWidth_, recordHeight_);
    setupPBOs();
}

void VisualizerRenderer::stopRecording() {
    recording_ = false;
    destroyPBOs();
}

} // namespace vc
