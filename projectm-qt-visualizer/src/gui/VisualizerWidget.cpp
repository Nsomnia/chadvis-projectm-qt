/**
 * @file VisualizerWidget.cpp
 * @brief Implementation of OpenGL visualization widget.
 */
#include "VisualizerWidget.hpp"
#include "projectm/ProjectMWrapper.hpp"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QHideEvent>
#include <QShowEvent>

VisualizerWidget::VisualizerWidget(QWidget *parent)
: QOpenGLWidget(parent)
{
    // Request ~60 FPS refresh with precise timer
    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &VisualizerWidget::onFrameTimer);
    
    // Qt attributes for better rendering
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_PaintOnScreen);
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
    
    // Log GL info
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    
    qDebug() << "GL Vendor:" << vendor;
    qDebug() << "GL Renderer:" << renderer;
    qDebug() << "GL Version:" << version;
    
    // Set initial GL state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Create and initialize projectM
    m_projectM = std::make_unique<ProjectMWrapper>();
    if (!m_projectM->initialize()) {
        qCritical() << "projectM initialization failed!";
        return;
    }
    
    m_projectM->resize(width(), height());
    m_initialized = true;
    m_frameTimer.start();
    
    qDebug() << "VisualizerWidget initialized successfully";
}

void VisualizerWidget::paintGL()
{
    if (!m_initialized || !m_projectM) {
        // Show error state
        glClearColor(1.0f, 0.0f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    
    // Clear before rendering
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Render projectM frame
    m_projectM->renderFrame();
    
    // Check for OpenGL errors
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        qWarning() << "OpenGL error:" << err;
    }
    
    // Log frame count occasionally
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) {
        qDebug() << "Rendered frames:" << frameCount;
    }
}

void VisualizerWidget::resizeGL(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    
    if (m_projectM) {
        m_projectM->resize(w, h);
    }
    glViewport(0, 0, w, h);
    
    qDebug() << "resizeGL:" << w << "x" << h;
}

void VisualizerWidget::onFrameTimer()
{
    if (!m_initialized || !m_projectM) return;
    
    static int timerCalls = 0;
    timerCalls++;
    
    // Feed audio data (silence if no capture)
    m_projectM->feedSilence();
    
    // Request repaint
    update();
    
    // Log every 30 calls
    if (timerCalls % 30 == 0) {
        qDebug() << "onFrameTimer called" << timerCalls << "times";
    }
}

void VisualizerWidget::hideEvent(QHideEvent* event)
{
    if (m_frameTimer.isActive()) {
        m_frameTimer.stop();
    }
    QOpenGLWidget::hideEvent(event);
}

void VisualizerWidget::showEvent(QShowEvent* event)
{
    if (m_initialized && !m_frameTimer.isActive()) {
        m_frameTimer.start();
    }
    QOpenGLWidget::showEvent(event);
}

void VisualizerWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (isVisible() && windowState() & Qt::WindowFullScreen) {
            makeCurrent();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            doneCurrent();
        }
    }
    QOpenGLWidget::changeEvent(event);
}

bool VisualizerWidget::startAudioCapture()
{
    if (!m_initialized || !m_projectM) return false;
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
