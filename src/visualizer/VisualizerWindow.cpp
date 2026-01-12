#include "VisualizerWindow.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <chrono>

namespace vc {

VisualizerWindow::VisualizerWindow(QWindow* parent) : QWindow(parent) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    format.setSamples(0);
    format.setAlphaBufferSize(0);
    format.setDepthBufferSize(24);
    setFormat(format);

    context_ = std::make_unique<QOpenGLContext>(this);
    context_->setFormat(format);
    setSurfaceType(QWindow::OpenGLSurface);

    fpsTimer_.setInterval(1000);
    connect(&fpsTimer_, &QTimer::timeout, this, &VisualizerWindow::updateFPS);
    connect(&renderTimer_, &QTimer::timeout, this, &VisualizerWindow::render);
}

VisualizerWindow::~VisualizerWindow() {
    if (context_ && context_->makeCurrent(this)) {
        if (overlayEngine_) {
            overlayEngine_->cleanup();
        }
        this->destroyPBOs();
        projectM_.shutdown();
        renderTarget_.destroy();
        overlayTarget_.destroy();
        context_->doneCurrent();
    }
}

void VisualizerWindow::exposeEvent(QExposeEvent* event) {
    Q_UNUSED(event);
    if (isExposed()) {
        if (!initialized_)
            initialize();
        render();
    }
}

void VisualizerWindow::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);
    if (!initialized_) {
        LOG_INFO("VisualizerWindow: resizeEvent triggering lazy initialization");
        initialize();
    }
    if (context_ && context_->makeCurrent(this)) {
        context_->doneCurrent();
    }
}

void VisualizerWindow::initialize() {
    LOG_INFO("VisualizerWindow: initialize() started");
    if (!context_->create()) {
        LOG_ERROR("VisualizerWindow: Failed to create OpenGL context");
        return;
    }
    LOG_DEBUG("VisualizerWindow: OpenGL context created");

    if (!context_->makeCurrent(this)) {
        LOG_ERROR("VisualizerWindow: Failed to make context current");
        return;
    }
    LOG_DEBUG("VisualizerWindow: Context made current");

    if (!initializeOpenGLFunctions()) {
        LOG_ERROR("VisualizerWindow: Failed to initialize OpenGL functions");
        return;
    }
    LOG_DEBUG("VisualizerWindow: OpenGL functions initialized");

    initBlitResources();
    LOG_DEBUG("VisualizerWindow: Blit resources initialized");

    const auto& vizConfig = CONFIG.visualizer();
    pm::ProjectMConfig pmConfig;
    pmConfig.width = width();
    pmConfig.height = height();
    pmConfig.fps = vizConfig.fps;
    pmConfig.beatSensitivity = vizConfig.beatSensitivity;
    pmConfig.presetPath = vizConfig.presetPath;
    pmConfig.presetDuration = vizConfig.presetDuration;
    pmConfig.transitionDuration = vizConfig.smoothPresetDuration;
    pmConfig.shufflePresets = vizConfig.shufflePresets;
    pmConfig.useDefaultPreset = vizConfig.useDefaultPreset;
    pmConfig.texturePaths = vizConfig.texturePaths;

    LOG_INFO(
            "VisualizerWindow: Initializing projectM with preset path: '{}', "
            "{} texture paths",
            pmConfig.presetPath.string(),
            pmConfig.texturePaths.size());

    projectM_.presetLoading.connect(
            [this](bool loading) { presetLoading_ = loading; });
    projectM_.presetChanged.connect(
            [this](const std::string& name) { 
                emit presetNameUpdated(QString::fromStdString(name)); 
            });

    if (auto result = projectM_.init(pmConfig); !result) {
        LOG_ERROR("VisualizerWindow: projectM init failed: {}",
                  result.error().message);
        return;
    }
    LOG_INFO("VisualizerWindow: projectM initialized successfully");

    if (!context_->makeCurrent(this)) {
        LOG_ERROR("VisualizerWindow: Failed to re-bind context for target creation");
        return;
    }

    if (auto res = renderTarget_.create(width(), height(), true); !res) {
        LOG_ERROR("VisualizerWindow: Failed to create render target: {}",
                  res.error().message);
    }
    if (auto res = overlayTarget_.create(width(), height(), false); !res) {
        LOG_ERROR("VisualizerWindow: Failed to create overlay target: {}",
                  res.error().message);
    }

    setRenderRate(vizConfig.fps);
    renderTimer_.start();
    fpsTimer_.start();

    initialized_ = true;
    LOG_INFO("VisualizerWindow: initialization complete");
    context_->doneCurrent();
}

void VisualizerWindow::render() {
    if (!initialized_ || !isExposed())
        return;
    if (context_->makeCurrent(this)) {
        renderFrame();
        context_->swapBuffers(this);
        context_->doneCurrent();
    }
}

void VisualizerWindow::renderFrame() {
    u32 w = width();
    u32 h = height();
    if (w == 0 || h == 0 || !projectM_.isInitialized())
        return;

    if (!renderTarget_.isValid()) {
        return;
    }

    // CRITICAL: Sync pending projectM state changes with active context
    projectM_.syncState();

    u32 renderW = recording_ ? recordWidth_ : w;
    u32 renderH = recording_ ? recordHeight_ : h;

    if (CONFIG.visualizer().lowResourceMode && !recording_) {
        renderW = std::max(160u, w / 2);
        renderH = std::max(120u, h / 2);
    }

    {
        std::lock_guard lock(audioMutex_);
        if (!audioQueue_.empty()) {
            u32 framesToFeed = (audioSampleRate_ + targetFps_ - 1) / targetFps_;
            u32 availableFrames = audioQueue_.size() / 2;
            u32 feedFrames = std::min(framesToFeed, availableFrames);
            if (feedFrames > 0) {
                projectM_.engine().addPCMDataInterleaved(
                        audioQueue_.data(), feedFrames, 2);
                audioQueue_.erase(audioQueue_.begin(),
                                  audioQueue_.begin() + (feedFrames * 2));
            }
        }
    }

    bool useFBO = recording_ || CONFIG.visualizer().lowResourceMode;

    if (useFBO) {
        if (renderTarget_.width() != renderW ||
            renderTarget_.height() != renderH) {
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
            if (overlayEngine_) {
                overlayEngine_->render(renderW, renderH);
            }
            this->captureAsync();
            renderTarget_.unbind();
            emit frameReady();
        }

        GLuint targetFbo = context_->defaultFramebufferObject();
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glViewport(0, 0, w, h);

        GLuint tex = renderTarget_.texture();
        if (tex) {
            drawTexture(tex);
        }

        if (!recording_ && overlayEngine_) {
            overlayEngine_->render(w, h);
        }
    } else {
        GLuint targetFbo = context_->defaultFramebufferObject();
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glViewport(0, 0, w, h);

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

        if (overlayEngine_) {
            overlayEngine_->render(w, h);
        }
    }

    ++frameCount_;
}

void VisualizerWindow::initBlitResources() {
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

    float vertices[] = {
            -1.0f, 1.0f,  0.0f, 1.0f, // Top Left
            -1.0f, -1.0f, 0.0f, 0.0f, // Bottom Left
            1.0f,  -1.0f, 1.0f, 0.0f, // Bottom Right
            -1.0f, 1.0f,  0.0f, 1.0f, // Top Left
            1.0f,  -1.0f, 1.0f, 0.0f, // Bottom Right
            1.0f,  1.0f,  1.0f, 1.0f // Top Right
    };

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

    LOG_DEBUG("Blit resources initialized");
}

void VisualizerWindow::drawTexture(GLuint textureId) {
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

void VisualizerWindow::setupPBOs() {
    this->destroyPBOs();
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

void VisualizerWindow::destroyPBOs() {
    if (pbos_[0])
        glDeleteBuffers(2, pbos_);
    pbos_[0] = pbos_[1] = 0;
}

void VisualizerWindow::captureAsync() {
    u32 nextIndex = (pboIndex_ + 1) % 2;
    u32 size = recordWidth_ * recordHeight_ * 4;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[pboIndex_]);
    glReadPixels(0, 0, recordWidth_, recordHeight_, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (pboAvailable_) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos_[nextIndex]);
        u8* ptr = (u8*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
            std::vector<u8> buffer(ptr, ptr + size);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            emit frameCaptured(
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

void VisualizerWindow::feedAudio(const f32* data,
                                 u32 frames,
                                 u32 channels,
                                 u32 sampleRate) {
    std::lock_guard lock(audioMutex_);
    if (sampleRate != audioSampleRate_)
        audioSampleRate_ = sampleRate;

    usize offset = audioQueue_.size();
    if (channels == 2) {
        audioQueue_.resize(offset + frames * 2);
        std::memcpy(
                audioQueue_.data() + offset, data, frames * 2 * sizeof(f32));
    } else if (channels == 1) {
        audioQueue_.resize(offset + frames * 2);
        f32* dest = audioQueue_.data() + offset;
        for (u32 i = 0; i < frames; ++i) {
            dest[i * 2] = data[i];
            dest[i * 2 + 1] = data[i];
        }
    } else {
        audioQueue_.resize(offset + frames * 2);
        f32* dest = audioQueue_.data() + offset;
        for (u32 i = 0; i < frames; ++i) {
            dest[i * 2] = data[i * channels];
            dest[i * 2 + 1] = (channels > 1) ? data[i * channels + 1]
                                             : data[i * channels];
        }
    }
}

void VisualizerWindow::setRenderRate(int fps) {
    if (fps > 0) {
        targetFps_ = fps;
        renderTimer_.start(1000 / fps);
        projectM_.engine().setFPS(fps);
    } else
        renderTimer_.stop();
}

void VisualizerWindow::setRecordingSize(u32 width, u32 height) {
    recordWidth_ = width;
    recordHeight_ = height;
}

void VisualizerWindow::startRecording() {
    recording_ = true;
    if (context_ && context_->makeCurrent(this)) {
        renderTarget_.resize(recordWidth_, recordHeight_);
        projectM_.engine().resize(recordWidth_, recordHeight_);
        this->setupPBOs();
        context_->doneCurrent();
    }
}

void VisualizerWindow::stopRecording() {
    recording_ = false;
    if (context_ && context_->makeCurrent(this)) {
        this->destroyPBOs();
        context_->doneCurrent();
    }
}

void VisualizerWindow::toggleFullscreen() {
    if (fullscreen_) {
        showNormal();
        setGeometry(normalGeometry_);
        fullscreen_ = false;
    } else {
        normalGeometry_ = geometry();
        auto* screen = QGuiApplication::primaryScreen();
        if (screen)
            setGeometry(screen->geometry());
        showFullScreen();
        fullscreen_ = true;
    }
}

void VisualizerWindow::nextPreset(bool smooth) {
    projectM_.nextPreset(smooth);
}

void VisualizerWindow::previousPreset(bool smooth) {
    projectM_.previousPreset(smooth);
}

void VisualizerWindow::randomPreset(bool smooth) {
    projectM_.randomPreset(smooth);
}

void VisualizerWindow::lockPreset(bool locked) {
    projectM_.lockPreset(locked);
}

void VisualizerWindow::updateFPS() {
    actualFps_ = static_cast<f32>(frameCount_);
    frameCount_ = 0;
    emit fpsChanged(actualFps_);
}

void VisualizerWindow::loadPresetFromManager() {
    std::lock_guard lock(presetLoadMutex_);
    if (presetLoadInProgress_ || !initialized_)
        return;
    presetLoadInProgress_ = true;
    
    const auto* preset = projectM_.presets().current();
    if (preset) {
        LOG_DEBUG("VisualizerWindow: Loading current preset from manager: {}", preset->name);
        projectM_.presets().selectByName(preset->name);
    }
    
    presetLoadInProgress_ = false;
}

void VisualizerWindow::updateSettings() {
    if (!initialized_)
        return;
    const auto& vizConfig = CONFIG.visualizer();
    setRenderRate(vizConfig.fps);
    
    if (context_ && context_->makeCurrent(this)) {
        projectM_.engine().setBeatSensitivity(vizConfig.beatSensitivity);
        projectM_.lockPreset(false);

        if (vizConfig.useDefaultPreset) {
            projectM_.engine().setPresetDuration(0);
        } else {
            projectM_.engine().setPresetDuration(vizConfig.presetDuration);
        }
        projectM_.engine().setSoftCutDuration(vizConfig.smoothPresetDuration);
        context_->doneCurrent();
    }
}

void VisualizerWindow::keyPressEvent(QKeyEvent* event) {
    const auto& keys = CONFIG.keyboard();
    QString key = event->text().toUpper();
    if (key.isEmpty())
        key = QKeySequence(event->key()).toString();
    std::string keyStr = key.toStdString();

    if (keyStr == keys.toggleFullscreen || event->key() == Qt::Key_F11)
        toggleFullscreen();
    else if (keyStr == keys.nextPreset || event->key() == Qt::Key_Right)
        nextPreset();
    else if (keyStr == keys.prevPreset || event->key() == Qt::Key_Left)
        previousPreset();
    else if (event->key() == Qt::Key_R)
        randomPreset();
    else if (event->key() == Qt::Key_L)
        lockPreset(!projectM_.isPresetLocked());
    else if (event->key() == Qt::Key_Escape && fullscreen_)
        toggleFullscreen();
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        toggleFullscreen();
    QWindow::mouseDoubleClickEvent(event);
}

} // namespace vc
