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
    format.setSamples(4);
    format.setAlphaBufferSize(0);
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
    if (!initialized_)
        return;
    if (context_ && context_->makeCurrent(this)) {
        int w = event->size().width();
        int h = event->size().height();
        projectM_.resize(w, h);
        if (!recording_) {
            renderTarget_.resize(w, h);
            overlayTarget_.resize(w, h);
        }
        context_->doneCurrent();
    }
}

void VisualizerWindow::initialize() {
    if (!context_->create())
        return;
    if (!context_->makeCurrent(this))
        return;
    if (!initializeOpenGLFunctions())
        return;

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
        return;

    const auto& vizConfig = CONFIG.visualizer();
    ProjectMConfig pmConfig;
    pmConfig.width = width();
    pmConfig.height = height();
    pmConfig.fps = vizConfig.fps;
    pmConfig.beatSensitivity = vizConfig.beatSensitivity;
    pmConfig.presetPath = vizConfig.presetPath;
    pmConfig.presetDuration = vizConfig.presetDuration;
    pmConfig.transitionDuration = vizConfig.smoothPresetDuration;
    pmConfig.shufflePresets = vizConfig.shufflePresets;
    pmConfig.useDefaultPreset = vizConfig.useDefaultPreset;

    projectM_.presetLoading.connect(
            [this](bool loading) { presetLoading_ = loading; });
    projectM_.presetChanged.connect(
            [this](const std::string& name) { loadPresetFromManager(); });

    if (auto result = projectM_.init(pmConfig); !result)
        return;

    renderTarget_.create(width(), height());
    overlayTarget_.create(width(), height());

    setRenderRate(vizConfig.fps);
    renderTimer_.start();
    fpsTimer_.start();

    if (vizConfig.presetDuration > 0 && !vizConfig.useDefaultPreset) {
        connect(&presetRotationTimer_,
                &QTimer::timeout,
                this,
                &VisualizerWindow::onPresetRotationTimeout);
        presetRotationTimer_.setInterval(vizConfig.presetDuration * 1000);
        presetRotationTimer_.start();
    }

    initialized_ = true;
    context_->doneCurrent();
}

void VisualizerWindow::onPresetRotationTimeout() {
    if (CONFIG.visualizer().shufflePresets)
        projectM_.randomPreset();
    else
        projectM_.nextPreset();
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
    if (presetLoading_) {
        if (recording_) {
            renderTarget_.bind();
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            renderTarget_.unbind();
        } else {
            projectM_.resetViewport(width(), height());
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }

    {
        std::lock_guard lock(audioMutex_);
        if (!audioQueue_.empty()) {
            u32 framesToFeed = (audioSampleRate_ + targetFps_ - 1) / targetFps_;
            u32 availableFrames = audioQueue_.size() / 2;
            u32 feedFrames = std::min(framesToFeed, availableFrames);
            if (feedFrames > 0) {
                projectM_.addPCMDataInterleaved(
                        audioQueue_.data(), feedFrames, 2);
                audioQueue_.erase(audioQueue_.begin(),
                                  audioQueue_.begin() + (feedFrames * 2));
            }
        }
    }

    if (recording_) {
        if (renderTarget_.width() != recordWidth_ ||
            renderTarget_.height() != recordHeight_) {
            renderTarget_.resize(recordWidth_, recordHeight_);
            overlayTarget_.resize(recordWidth_, recordHeight_);
            this->setupPBOs();
        }
        projectM_.renderToTarget(renderTarget_);
        if (overlayEngine_) {
            overlayTarget_.bind();
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            renderTarget_.blitTo(overlayTarget_, true);
            overlayEngine_->render(recordWidth_, recordHeight_);
            this->captureAsync();
            overlayTarget_.unbind();
        } else {
            renderTarget_.bind();
            this->captureAsync();
            renderTarget_.unbind();
        }
        emit frameReady();
    } else {
        projectM_.resetViewport(width(), height());
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        projectM_.render();

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        if (overlayEngine_)
            overlayEngine_->render(width(), height());
    }
    ++frameCount_;
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
            captureBuffer_.assign(ptr, ptr + size);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            emit frameCaptured(
                    captureBuffer_.data(),
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
    audioQueue_.resize(offset + frames * 2);
    std::memcpy(audioQueue_.data() + offset, data, frames * 2 * sizeof(f32));
}

void VisualizerWindow::setRenderRate(int fps) {
    if (fps > 0) {
        targetFps_ = fps;
        renderTimer_.start(1000 / fps);
        projectM_.setFPS(fps);
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
        overlayTarget_.resize(recordWidth_, recordHeight_);
        this->setupPBOs();
        context_->doneCurrent();
    }
}

void VisualizerWindow::stopRecording() {
    recording_ = false;
    if (context_ && context_->makeCurrent(this)) {
        this->destroyPBOs();
        renderTarget_.resize(width(), height());
        overlayTarget_.resize(width(), height());
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

void VisualizerWindow::updateFPS() {
    actualFps_ = static_cast<f32>(frameCount_);
    frameCount_ = 0;
    emit fpsChanged(actualFps_);
}

void VisualizerWindow::loadPresetFromManager() {
    std::lock_guard lock(presetLoadMutex_);
    if (presetLoadInProgress_)
        return;
    presetLoadInProgress_ = true;
    if (!context_ || !context_->isValid() || !context_->makeCurrent(this)) {
        presetLoadInProgress_ = false;
        return;
    }
    presetLoading_ = true;
    const auto* preset = projectM_.presets().current();
    if (preset) {
        projectm_load_preset_file(
                projectM_.getHandle(), preset->path.c_str(), false);
        emit presetNameUpdated(QString::fromStdString(preset->name));
    }
    presetLoading_ = false;
    presetLoadInProgress_ = false;
    context_->doneCurrent();
}

void VisualizerWindow::updateSettings() {
    if (!initialized_)
        return;
    const auto& vizConfig = CONFIG.visualizer();
    setRenderRate(vizConfig.fps);
    projectM_.setBeatSensitivity(vizConfig.beatSensitivity);
    projectM_.setShuffleEnabled(vizConfig.shufflePresets);
    presetRotationTimer_.stop();
    if (vizConfig.presetDuration > 0 && !vizConfig.useDefaultPreset) {
        presetRotationTimer_.setInterval(vizConfig.presetDuration * 1000);
        presetRotationTimer_.start();
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
        projectM_.nextPreset();
    else if (keyStr == keys.prevPreset || event->key() == Qt::Key_Left)
        projectM_.previousPreset();
    else if (event->key() == Qt::Key_R)
        projectM_.randomPreset();
    else if (event->key() == Qt::Key_L)
        projectM_.lockPreset(!projectM_.isPresetLocked());
    else if (event->key() == Qt::Key_Escape && fullscreen_)
        toggleFullscreen();
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        toggleFullscreen();
    QWindow::mouseDoubleClickEvent(event);
}

} // namespace vc
