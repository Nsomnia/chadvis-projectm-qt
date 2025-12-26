#include "ProjectMWindow.hpp"
#include <QExposeEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QDebug>
#include <QCoreApplication>
#include <cmath>

ProjectMWindow::ProjectMWindow(QWindow *parent)
    : QWindow(parent),
      m_context(nullptr),
      m_mediaPlayer(nullptr),
      m_audioOutput(nullptr),
      m_initialized(false),
      m_width(1280),
      m_height(720),
      m_audioCaptureActive(false),
      m_silenceBuffer(2048, 0.0f),
      m_silenceCounter(0)
{
    // Set surface format
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setDepthBufferSize(24);
    setFormat(format);
    
    // Create OpenGL context
    m_context = new QOpenGLContext(this);
    m_context->setFormat(format);
    if (!m_context->create()) {
        qCritical() << "Failed to create OpenGL context!";
        return;
    }
    
    setSurfaceType(QWindow::OpenGLSurface);
    resize(m_width, m_height);
    setTitle("projectM Visualizer - Chad Edition");
    
    // Setup audio player
    m_mediaPlayer = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.5);
    
    connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged,
            this, &ProjectMWindow::handleMediaStatusChanged);
    connect(m_mediaPlayer, &QMediaPlayer::errorOccurred,
            this, &ProjectMWindow::handleMediaError);
    
    // Render timer
    connect(&m_renderTimer, &QTimer::timeout, this, &ProjectMWindow::render);
    m_renderTimer.setInterval(16); // ~60 FPS
    m_renderTimer.setTimerType(Qt::PreciseTimer);
}

ProjectMWindow::~ProjectMWindow()
{
    cleanup();
}

void ProjectMWindow::initialize()
{
    if (!m_context || !m_context->makeCurrent(this)) {
        qCritical() << "Failed to make context current!";
        return;
    }
    
    if (!initializeOpenGLFunctions()) {
        qCritical() << "Failed to initialize OpenGL functions!";
        return;
    }
    
    qDebug() << "OpenGL Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    qDebug() << "OpenGL Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    qDebug() << "OpenGL Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    
    // GL state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    
    // Initialize projectM
    m_projectM = std::make_unique<ProjectMWrapper>();
    if (!m_projectM->initialize()) {
        qCritical() << "projectM initialization failed!";
        return;
    }
    m_projectM->resize(m_width, m_height);
    
    m_initialized = true;
    m_renderTimer.start();
    
    qDebug() << "ProjectMWindow initialized successfully";
}

void ProjectMWindow::cleanup()
{
    if (m_context && m_context->makeCurrent(this)) {
        m_projectM.reset();
        m_context->doneCurrent();
    }
    delete m_context;
    m_context = nullptr;
}

void ProjectMWindow::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);
    
    if (isExposed()) {
        if (!m_initialized) {
            initialize();
        }
        render();
    }
}

void ProjectMWindow::resizeEvent(QResizeEvent *event)
{
    m_width = event->size().width();
    m_height = event->size().height();
    
    if (m_projectM) {
        m_projectM->resize(m_width, m_height);
    }
    
    if (isExposed()) {
        render();
    }
}

void ProjectMWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
    } else if (event->key() == Qt::Key_F11) {
        if (windowState() & Qt::WindowFullScreen) {
            showNormal();
        } else {
            showFullScreen();
        }
    } else if (event->key() == Qt::Key_A && event->modifiers() & Qt::ControlModifier) {
        toggleAudioCapture();
    } else if (event->key() == Qt::Key_N) {
        if (m_projectM) m_projectM->nextPreset();
    } else if (event->key() == Qt::Key_P) {
        if (m_projectM) m_projectM->previousPreset();
    }
    
    QWindow::keyPressEvent(event);
}

void ProjectMWindow::render()
{
    if (!m_initialized || !isExposed() || !m_context || !m_projectM) {
        return;
    }
    
    if (!m_context->makeCurrent(this)) {
        qWarning() << "Failed to make context current for rendering!";
        return;
    }
    
    // Clear
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_width, m_height);
    
    // Process audio
    processAudio();
    
    // Render
    m_projectM->renderFrame();
    
    // Swap buffers
    m_context->swapBuffers(this);
    
    // Request next frame
    requestUpdate();
}

void ProjectMWindow::processAudio()
{
    if (!m_projectM) return;
    
    // If audio capture is active, it's feeding data in the background
    // If not, feed silent data to keep visualization alive
    if (!m_audioCaptureActive && !m_mediaPlayer->isPlaying()) {
        m_projectM->feedSilence();
    }
}

void ProjectMWindow::loadAudioFile(const QString& filePath)
{
    if (m_mediaPlayer) {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(filePath));
        m_mediaPlayer->play();
        qDebug() << "Loading audio file:" << filePath;
    }
}

void ProjectMWindow::toggleAudioCapture()
{
    if (!m_projectM) return;
    
    if (m_audioCaptureActive) {
        m_projectM->stopAudioCapture();
        m_audioCaptureActive = false;
        qDebug() << "Audio capture stopped";
    } else {
        if (m_projectM->startAudioCapture()) {
            m_audioCaptureActive = true;
            qDebug() << "Audio capture started";
        } else {
            qDebug() << "Failed to start audio capture";
        }
    }
}

bool ProjectMWindow::isAudioCapturing() const
{
    return m_audioCaptureActive;
}

void ProjectMWindow::handleMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    switch (status) {
        case QMediaPlayer::LoadedMedia:
            qDebug() << "Media loaded, starting playback";
            m_mediaPlayer->play();
            break;
        case QMediaPlayer::EndOfMedia:
            qDebug() << "End of media, looping";
            m_mediaPlayer->setPosition(0);
            m_mediaPlayer->play();
            break;
        default:
            break;
    }
}

void ProjectMWindow::handleMediaError(QMediaPlayer::Error error, const QString& errorString)
{
    qCritical() << "Media player error:" << error << "-" << errorString;
}
