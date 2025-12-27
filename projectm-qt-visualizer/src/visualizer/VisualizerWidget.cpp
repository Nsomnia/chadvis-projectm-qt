#include "VisualizerWidget.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>
#include "util/GLIncludes.hpp"

namespace vc {

VisualizerWidget::VisualizerWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Set OpenGL format
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);  // VSync
    format.setSamples(4);  // MSAA
    setFormat(format);
    
    // Focus for keyboard input
    setFocusPolicy(Qt::StrongFocus);
    
    // FPS counter
    fpsTimer_.setInterval(1000);
    connect(&fpsTimer_, &QTimer::timeout, this, &VisualizerWidget::updateFPS);
}

VisualizerWidget::~VisualizerWidget() {
    makeCurrent();
    projectM_.shutdown();
    renderTarget_.destroy();
    overlayTarget_.destroy();
    doneCurrent();
}

void VisualizerWidget::initializeGL() {
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        LOG_ERROR("GLEW init failed: {}", reinterpret_cast<const char*>(glewGetErrorString(err)));
        return;
    }
    
    initializeOpenGLFunctions();
    
    LOG_INFO("OpenGL: {} - {}", 
             reinterpret_cast<const char*>(glGetString(GL_VERSION)),
             reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    
    // Initialize ProjectM
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
    
    if (auto result = projectM_.init(pmConfig); !result) {
        LOG_ERROR("ProjectM init failed: {}", result.error().message);
        return;
    }
    
    // Create render targets
    renderTarget_.create(width(), height());
    overlayTarget_.create(width(), height());
    
    // Connect render timer timeout
    connect(&renderTimer_, &QTimer::timeout, this, &VisualizerWidget::onTimer);
    
    fpsTimer_.start();
    
    initialized_ = true;
    LOG_INFO("Visualizer widget initialized");
}

void VisualizerWidget::resizeGL(int w, int h) {
    if (!initialized_) return;
    
    projectM_.resize(w, h);
    
    if (!recording_) {
        renderTarget_.resize(w, h);
        overlayTarget_.resize(w, h);
    }
}

void VisualizerWidget::paintGL() {
    if (!initialized_) return;
    renderFrame();
}

void VisualizerWidget::onTimer() {
    update();  // Trigger paintGL
}

void VisualizerWidget::renderFrame() {
    // Render to target (either widget size or recording size)
    u32 targetW = recording_ ? recordWidth_ : width();
    u32 targetH = recording_ ? recordHeight_ : height();
    
    // Ensure render target is correct size
    if (renderTarget_.width() != targetW || renderTarget_.height() != targetH) {
        renderTarget_.resize(targetW, targetH);
        overlayTarget_.resize(targetW, targetH);
    }
    
    // Render ProjectM to target
    projectM_.renderToTarget(renderTarget_);
    
    // Render overlay on top
    if (overlayEngine_) {
        overlayTarget_.bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Blit visualizer first
        renderTarget_.blitTo(overlayTarget_, true);
        
        // Then render overlay
        overlayEngine_->render(overlayTarget_.width(), overlayTarget_.height());
        overlayTarget_.unbind();
        
        // Blit result to screen
        overlayTarget_.blitToScreen(width(), height(), true);
    } else {
        // Just blit visualizer to screen
        renderTarget_.blitToScreen(width(), height(), true);
    }
    
    ++frameCount_;
    
    if (recording_) {
        emit frameReady();
    }
}

void VisualizerWidget::renderOverlay() {
    // Overlay rendering is handled in renderFrame()
}

void VisualizerWidget::feedAudio(const f32* data, u32 frames, u32 channels) {
    projectM_.addPCMDataInterleaved(data, frames, channels);
}

void VisualizerWidget::setRenderRate(int fps) {
    if (fps > 0) {
        targetFps_ = fps;
        renderTimer_.start(1000 / fps);
        projectM_.setFPS(fps);
    } else {
        renderTimer_.stop();
    }
}

void VisualizerWidget::setRecordingSize(u32 width, u32 height) {
    recordWidth_ = width;
    recordHeight_ = height;
}

void VisualizerWidget::startRecording() {
    recording_ = true;
    makeCurrent();
    renderTarget_.resize(recordWidth_, recordHeight_);
    overlayTarget_.resize(recordWidth_, recordHeight_);
    doneCurrent();
    LOG_INFO("Started recording at {}x{}", recordWidth_, recordHeight_);
}

void VisualizerWidget::stopRecording() {
    recording_ = false;
    makeCurrent();
    renderTarget_.resize(width(), height());
    overlayTarget_.resize(width(), height());
    doneCurrent();
    LOG_INFO("Stopped recording");
}

void VisualizerWidget::toggleFullscreen() {
    if (fullscreen_) {
        // Exit fullscreen
        setWindowFlags(Qt::Widget);
        setGeometry(normalGeometry_);
        show();
        fullscreen_ = false;
    } else {
        // Enter fullscreen
        normalGeometry_ = geometry();
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        
        // Get screen geometry
        auto* screen = QGuiApplication::primaryScreen();
        if (screen) {
            setGeometry(screen->geometry());
        }
        
        showFullScreen();
        fullscreen_ = true;
    }
}

void VisualizerWidget::updateFPS() {
    actualFps_ = static_cast<f32>(frameCount_);
    frameCount_ = 0;
    emit fpsChanged(actualFps_);
}

void VisualizerWidget::keyPressEvent(QKeyEvent* event) {
    const auto& keys = CONFIG.keyboard();
    QString key = event->text().toUpper();
    if (key.isEmpty()) {
        key = QKeySequence(event->key()).toString();
    }
    
    std::string keyStr = key.toStdString();
    
    if (keyStr == keys.toggleFullscreen || event->key() == Qt::Key_F11) {
        toggleFullscreen();
    }
    else if (keyStr == keys.nextPreset || event->key() == Qt::Key_Right) {
        projectM_.nextPreset();
    }
    else if (keyStr == keys.prevPreset || event->key() == Qt::Key_Left) {
        projectM_.previousPreset();
    }
    else if (event->key() == Qt::Key_R) {
        projectM_.randomPreset();
    }
    else if (event->key() == Qt::Key_L) {
        projectM_.lockPreset(!projectM_.isPresetLocked());
    }
    else if (event->key() == Qt::Key_Escape && fullscreen_) {
        toggleFullscreen();
    }
    else {
        QOpenGLWidget::keyPressEvent(event);
    }
}

void VisualizerWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggleFullscreen();
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

} // namespace vc

#include "moc_VisualizerWidget.cpp"
