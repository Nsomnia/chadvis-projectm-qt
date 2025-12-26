/**
 * @file VisualizerWidget.cpp
 * @brief Implementation of OpenGL visualization widget.
 *
 * CRITICAL: OpenGL context is ONLY valid inside:
 * - initializeGL()
 * - paintGL()
 * - resizeGL()
 * Or after calling makeCurrent().
 */
#include "VisualizerWidget.hpp"
#include "projectm/ProjectMWrapper.hpp"
#include <QDebug>
#include <QApplication>
#include <QScreen>
#include <QHideEvent>
#include <QShowEvent>

VisualizerWidget::VisualizerWidget(QWidget *parent)
: QOpenGLWidget(parent)
{
    // Request ~60 FPS refresh
    m_frameTimer.setInterval(16); // ~60 FPS
    connect(&m_frameTimer, &QTimer::timeout, this, &VisualizerWidget::onFrameTimer);
    
    // Prevent Qt compositor caching that causes ghosting
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_PaintOnScreen);
    
    // Ensure we get our own GL context
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

VisualizerWidget::~VisualizerWidget()
{
    if (m_initialized) {
        makeCurrent();
        m_projectM.reset();
        doneCurrent();
    }
}

void VisualizerWidget::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        qCritical() << "Failed to initialize OpenGL functions!";
        return;
    }
    
    // Clear any existing GL errors
    while (glGetError() != GL_NO_ERROR);
    
    qDebug() << "=== OpenGL Initialization ===";
    qDebug() << " Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    qDebug() << " Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    qDebug() << " Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    
    // Set GL state that projectM expects
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    
    // Initialize projectM
    // CRITICAL: This must happen with GL context current
    m_projectM = std::make_unique<ProjectMWrapper>();
    if (!m_projectM->initialize()) {
        qCritical() << "Failed to initialize projectM!";
        return;
    }
    m_projectM->resize(width(), height());
    m_initialized = true;
    
    // Start render loop
    m_frameTimer.start();
    qDebug() << "✓ VisualizerWidget fully initialized";
    qDebug() << "=== Ready for rendering ===";
}

void VisualizerWidget::paintGL()
{
    if (!m_initialized || !m_projectM) {
        // Fallback: clear to red to show error
        glClearColor(1.0f, 0.0f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    
    // Explicitly clear before rendering to prevent ghosting
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Let projectM render
    m_projectM->renderFrame();
    
    // Check for GL errors
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        qWarning() << "OpenGL error after render:" << err;
    }
}

void VisualizerWidget::resizeGL(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    
    if (m_projectM) {
        m_projectM->resize(w, h);
    }
    glViewport(0, 0, w, h);
}

void VisualizerWidget::onFrameTimer()
{
    if (!m_initialized || !m_projectM) return;
    
    // Feed silent audio if no source active
    // This keeps projectM visualization alive
    m_projectM->feedSilence();
    
    // Request repaint - this triggers paintGL()
    update();
}

void VisualizerWidget::hideEvent(QHideEvent* event)
{
    // Stop timer when hidden to save resources
    if (m_frameTimer.isActive()) {
        m_frameTimer.stop();
    }
    QOpenGLWidget::hideEvent(event);
}

void VisualizerWidget::showEvent(QShowEvent* event)
{
    // Restart timer when shown
    if (m_initialized && !m_frameTimer.isActive()) {
        m_frameTimer.start();
    }
    QOpenGLWidget::showEvent(event);
}

void VisualizerWidget::changeEvent(QEvent* event)
{
    // Handle fullscreen/minimize transitions
    if (event->type() == QEvent::WindowStateChange) {
        // Force GL context to be made current before any state changes
        if (isVisible() && windowState() & Qt::WindowFullScreen) {
            makeCurrent();
            // Reinitialize GL state if needed
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            doneCurrent();
        }
    }
    QOpenGLWidget::changeEvent(event);
}

// Audio control methods
bool VisualizerWidget::startAudioCapture()
{
    if (!m_initialized || !m_projectM) {
        qWarning() << "Cannot start audio capture - not initialized";
        return false;
    }
    return m_projectM->startAudioCapture();
}

void VisualizerWidget::stopAudioCapture()
{
    if (m_projectM) {
        m_projectM->stopAudioCapture();
    }
}

bool VisualizerWidget::isAudioCapturing() const
{
    if (!m_projectM) return false;
    return m_projectM->isAudioCapturing();
}
