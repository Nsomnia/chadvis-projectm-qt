#include "VisualizerWindow.hpp"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include "core/Config.hpp"
#include "core/Logger.hpp"

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

    renderer_ = std::make_unique<VisualizerRenderer>();

    fpsTimer_.setInterval(1000);
    connect(&fpsTimer_, &QTimer::timeout, this, &VisualizerWindow::updateFPS);
    connect(&renderTimer_, &QTimer::timeout, this, &VisualizerWindow::render);
}

VisualizerWindow::~VisualizerWindow() {
    if (context_ && context_->makeCurrent(this)) {
        renderer_->cleanup();
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
    if (!initialized_)
        initialize();
    if (context_ && context_->makeCurrent(this)) {
        context_->doneCurrent();
    }
}

void VisualizerWindow::initialize() {
    if (!context_->create()) {
        LOG_ERROR("VisualizerWindow: Failed to create OpenGL context");
        return;
    }
    if (!context_->makeCurrent(this)) {
        LOG_ERROR("VisualizerWindow: Failed to make context current");
        return;
    }

    renderer_->initialize(width(), height());

    renderer_->projectM().presetChanged.connect(
            [this](const std::string& name) {
                emit presetNameUpdated(QString::fromStdString(name));
            });

    renderer_->frameCaptured.connect(
            [this](std::vector<u8> data, u32 w, u32 h, i64 ts) {
                emit frameCaptured(std::move(data), w, h, ts);
            });

    updateSettings();
    renderTimer_.start();
    fpsTimer_.start();

    initialized_ = true;
    context_->doneCurrent();
}

void VisualizerWindow::render() {
    if (!initialized_ || !isExposed())
        return;
    if (context_->makeCurrent(this)) {
        renderer_->render(width(), height(), isExposed());
        context_->swapBuffers(this);
        context_->doneCurrent();
        ++frameCount_;
    }
}

void VisualizerWindow::updateFPS() {
    actualFps_ = static_cast<f32>(frameCount_);
    frameCount_ = 0;
    emit fpsChanged(actualFps_);
}

void VisualizerWindow::loadPresetFromManager() {
    if (!initialized_)
        return;
    const auto* preset = renderer_->projectM().presets().current();
    if (preset) {
        renderer_->projectM().presets().selectByName(preset->name);
    }
}

void VisualizerWindow::updateSettings() {
    if (!initialized_)
        return;
    const auto& vizConfig = CONFIG.visualizer();
    setRenderRate(vizConfig.fps);

    if (context_ && context_->makeCurrent(this)) {
        renderer_->projectM().engine().setBeatSensitivity(
                vizConfig.beatSensitivity);
        renderer_->projectM().lockPreset(false);
        renderer_->projectM().engine().setPresetDuration(
                vizConfig.useDefaultPreset ? 0 : vizConfig.presetDuration);
        renderer_->projectM().engine().setSoftCutDuration(
                vizConfig.smoothPresetDuration);
        context_->doneCurrent();
    }
}

void VisualizerWindow::setOverlayEngine(OverlayEngine* engine) {
    renderer_->setOverlayEngine(engine);
}

void VisualizerWindow::nextPreset(bool smooth) {
    renderer_->projectM().nextPreset(smooth);
}
void VisualizerWindow::previousPreset(bool smooth) {
    renderer_->projectM().previousPreset(smooth);
}
void VisualizerWindow::randomPreset(bool smooth) {
    renderer_->projectM().randomPreset(smooth);
}
void VisualizerWindow::lockPreset(bool locked) {
    renderer_->projectM().lockPreset(locked);
}

void VisualizerWindow::setRecordingSize(u32 width, u32 height) {
    renderer_->setRecordingSize(width, height);
}

void VisualizerWindow::startRecording() {
    if (context_ && context_->makeCurrent(this)) {
        renderer_->startRecording();
        context_->doneCurrent();
    }
}

void VisualizerWindow::stopRecording() {
    if (context_ && context_->makeCurrent(this)) {
        renderer_->stopRecording();
        context_->doneCurrent();
    }
}

void VisualizerWindow::setRenderRate(int fps) {
    if (fps > 0) {
        renderTimer_.start(1000 / fps);
        renderer_->projectM().engine().setFPS(fps);
    } else
        renderTimer_.stop();
}

void VisualizerWindow::feedAudio(const f32* data,
                                 u32 frames,
                                 u32 channels,
                                 u32 sampleRate) {
    renderer_->feedAudio(data, frames, channels, sampleRate);
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
        lockPreset(!renderer_->projectM().isPresetLocked());
    else if (event->key() == Qt::Key_Escape && fullscreen_)
        toggleFullscreen();
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        toggleFullscreen();
    QWindow::mouseDoubleClickEvent(event);
}

} // namespace vc
