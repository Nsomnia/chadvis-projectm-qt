#include "VisualizerWindow.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>

namespace vc {

VisualizerWindow::VisualizerWindow(QWindow* parent)
    : QWindow(parent)
{
    // Set surface format
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);  // VSync
    format.setSamples(4);  // MSAA
    setFormat(format);
    
    // Create OpenGL context
    context_ = std::make_unique<QOpenGLContext>(this);
    context_->setFormat(format);
    
    // Set surface type
    setSurfaceType(QWindow::OpenGLSurface);
    
    // FPS counter
    fpsTimer_.setInterval(1000);
    connect(&fpsTimer_, &QTimer::timeout, this, &VisualizerWindow::updateFPS);
    
    // Render timer (will be started after initialization)
    connect(&renderTimer_, &QTimer::timeout, this, &VisualizerWindow::render);
}

VisualizerWindow::~VisualizerWindow() {
    if (context_ && context_->makeCurrent(this)) {
        projectM_.shutdown();
        renderTarget_.destroy();
        overlayTarget_.destroy();
        context_->doneCurrent();
    }
}

void VisualizerWindow::exposeEvent(QExposeEvent* event) {
    Q_UNUSED(event);
    
    if (isExposed()) {
        if (!initialized_) {
            initialize();
        }
        render();  // Initial render
    }
}

void VisualizerWindow::resizeEvent(QResizeEvent* event) {
    if (!initialized_) return;
    
    if (context_ && context_->makeCurrent(this)) {
        int w = event->size().width();
        int h = event->size().height();
        
        LOG_DEBUG("Resize event: {}x{}", w, h);
        
        projectM_.resize(w, h);
        
        if (!recording_) {
            renderTarget_.resize(w, h);
            overlayTarget_.resize(w, h);
        }
        
        context_->doneCurrent();
    }
}

void VisualizerWindow::initialize() {
    if (!context_->create()) {
        LOG_ERROR("Failed to create OpenGL context");
        return;
    }
    
    if (!context_->makeCurrent(this)) {
        LOG_ERROR("Failed to make context current");
        return;
    }
    
    if (!initializeOpenGLFunctions()) {
        LOG_ERROR("Failed to initialize OpenGL functions");
        return;
    }
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        LOG_ERROR("GLEW init failed: {}", reinterpret_cast<const char*>(glewGetErrorString(err)));
        return;
    }
    
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
    pmConfig.forcePreset = vizConfig.forcePreset;
    
    if (auto result = projectM_.init(pmConfig); !result) {
        LOG_ERROR("ProjectM init failed: {}", result.error().message);
        return;
    }
    
    // Create render targets
    renderTarget_.create(width(), height());
    overlayTarget_.create(width(), height());
    
    // Start render timer
    setRenderRate(vizConfig.fps);
    fpsTimer_.start();
    
    initialized_ = true;
    LOG_INFO("Visualizer window initialized");
    
    context_->doneCurrent();
}

void VisualizerWindow::render() {
    if (!initialized_ || !isExposed()) {
        LOG_DEBUG("render() called but not initialized or not exposed");
        return;
    }
    
    if (context_->makeCurrent(this)) {
        LOG_DEBUG("render() - frame {}", frameCount_);
        renderFrame();
        context_->swapBuffers(this);
        context_->doneCurrent();
        
        // Request next frame
        if (isExposed()) {
            requestUpdate();
        }
    } else {
        LOG_ERROR("Failed to make context current in render()");
    }
}

void VisualizerWindow::renderFrame() {
    // Feed audio data from queue (thread-safe)
    // Feed at the correct rate: sampleRate / fps
    {
        std::lock_guard lock(audioMutex_);
        
        if (!audioQueue_.empty()) {
            // Calculate frames needed for this render
            // sampleRate / targetFps_ = frames per render
            u32 framesToFeed = (audioSampleRate_ + targetFps_ - 1) / targetFps_;  // Round up
            u32 availableFrames = audioQueue_.size() / 2;
            
            // Feed up to framesToFeed
            u32 feedFrames = std::min(framesToFeed, availableFrames);
            
            if (feedFrames > 0) {
                LOG_DEBUG("VisualizerWindow::renderFrame: Feeding {} of {} available frames (rate: {}Hz, fps: {})", 
                          feedFrames, availableFrames, audioSampleRate_, targetFps_);
                projectM_.addPCMDataInterleaved(audioQueue_.data(), feedFrames, 2);
                
                // Remove fed frames from queue
                u32 feedElements = feedFrames * 2;
                audioQueue_.erase(audioQueue_.begin(), audioQueue_.begin() + feedElements);
            } else {
                LOG_DEBUG("VisualizerWindow::renderFrame: No audio to feed (need {} frames, have {})", 
                          framesToFeed, availableFrames);
            }
        } else {
            LOG_DEBUG("VisualizerWindow::renderFrame: No audio in queue");
        }
    }

    // For recording, use FBO. For normal display, render directly
    if (recording_) {
        // Recording path: render to FBO, emit signal
        if (renderTarget_.width() != recordWidth_ || renderTarget_.height() != recordHeight_) {
            renderTarget_.resize(recordWidth_, recordHeight_);
            overlayTarget_.resize(recordWidth_, recordHeight_);
        }
        
        projectM_.renderToTarget(renderTarget_);
        
        if (overlayEngine_) {
            overlayTarget_.bind();
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            renderTarget_.blitTo(overlayTarget_, true);
            overlayEngine_->render(overlayTarget_.width(), overlayTarget_.height());
            overlayTarget_.unbind();
        }
        
        emit frameReady();
    } else {
        // Normal display: render directly to default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width(), height());
        
        LOG_DEBUG("Rendering frame {} to {}x{} window", frameCount_, width(), height());
        
        // Clear before rendering
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        // Let projectM render directly to screen
        LOG_DEBUG("VisualizerWindow::renderFrame: Calling projectM_.render()");
        projectM_.render();
        LOG_DEBUG("VisualizerWindow::renderFrame: projectM_.render() completed");
        
        // Render overlay if available
        if (overlayEngine_) {
            overlayEngine_->render(width(), height());
        }
    }
    
    ++frameCount_;
}

void VisualizerWindow::feedAudio(const f32* data, u32 frames, u32 channels, u32 sampleRate) {
    std::lock_guard lock(audioMutex_);
    // Update sample rate if it changed
    if (sampleRate != audioSampleRate_) {
        LOG_INFO("Audio sample rate changed: {} -> {} Hz", audioSampleRate_, sampleRate);
        audioSampleRate_ = sampleRate;
    }
    // Append audio data to queue for the render thread to consume
    // For now, assume stereo interleaved
    usize offset = audioQueue_.size();
    audioQueue_.resize(offset + frames * 2);
    std::memcpy(audioQueue_.data() + offset, data, frames * 2 * sizeof(f32));
    LOG_DEBUG("VisualizerWindow::feedAudio: {} frames added, queue size: {} frames ({} bytes)", frames, audioQueue_.size() / 2, audioQueue_.size() * sizeof(f32));
}
void VisualizerWindow::setRenderRate(int fps) {
    if (fps > 0) {
        targetFps_ = fps;
        renderTimer_.start(1000 / fps);
        projectM_.setFPS(fps);
    } else {
        renderTimer_.stop();
    }
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
        context_->doneCurrent();
    }
    LOG_INFO("Started recording at {}x{}", recordWidth_, recordHeight_);
}

void VisualizerWindow::stopRecording() {
    recording_ = false;
    if (context_ && context_->makeCurrent(this)) {
        renderTarget_.resize(width(), height());
        overlayTarget_.resize(width(), height());
        context_->doneCurrent();
    }
    LOG_INFO("Stopped recording");
}

void VisualizerWindow::toggleFullscreen() {
    if (fullscreen_) {
        // Exit fullscreen
        showNormal();
        setGeometry(normalGeometry_);
        fullscreen_ = false;
    } else {
        // Enter fullscreen
        normalGeometry_ = geometry();
        
        // Get screen geometry
        auto* screen = QGuiApplication::primaryScreen();
        if (screen) {
            setGeometry(screen->geometry());
        }
        
        showFullScreen();
        fullscreen_ = true;
    }
}

void VisualizerWindow::updateFPS() {
    actualFps_ = static_cast<f32>(frameCount_);
    frameCount_ = 0;
    emit fpsChanged(actualFps_);
}

void VisualizerWindow::keyPressEvent(QKeyEvent* event) {
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
    else if (event->key() == Qt::Key_N) {
        projectM_.nextPreset();
    }
    else if (event->key() == Qt::Key_P) {
        projectM_.previousPreset();
    }
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        toggleFullscreen();
    }
    QWindow::mouseDoubleClickEvent(event);
}

} // namespace vc

#include "moc_VisualizerWindow.cpp"
