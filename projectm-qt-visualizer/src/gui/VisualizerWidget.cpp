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
VisualizerWidget::VisualizerWidget(QWidget *parent)
: QOpenGLWidget(parent)
{
// Request ~60 FPS refresh
m_frameTimer.setInterval(16); // ~60 FPS
connect(&m_frameTimer, &QTimer::timeout, this, &VisualizerWidget::onFrameTimer);
}
VisualizerWidget::~VisualizerWidget()
{
// Ensure GL context for cleanup
makeCurrent();
m_projectM.reset();
doneCurrent();
}
void VisualizerWidget::initializeGL()
{
if (!initializeOpenGLFunctions()) {
qCritical() << "Failed to initialize OpenGL functions!";
return;
}
qDebug() << "OpenGL initialized:";
qDebug() << " Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
qDebug() << " Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
qDebug() << " Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
// Initialize projectM
m_projectM = std::make_unique<ProjectMWrapper>();
if (!m_projectM->initialize()) {
qCritical() << "Failed to initialize projectM!";
return;
}
m_projectM->resize(width(), height());
m_initialized = true;
// Start render loop
m_frameTimer.start();
qDebug() << "VisualizerWidget fully initialized - Chad mode activated";
}
void VisualizerWidget::paintGL()
{
if (!m_initialized || !m_projectM) {
// Fallback: clear to "I'm broken" pink
glClearColor(1.0f, 0.0f, 0.5f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);
return;
}
// Let projectM render
m_projectM->renderFrame();
}
void VisualizerWidget::resizeGL(int w, int h)
{
if (m_projectM) {
m_projectM->resize(w, h);
}
glViewport(0, 0, w, h);
}
void VisualizerWidget::onFrameTimer()
{
// Feed silent audio if no source active
if (m_projectM) {
m_projectM->feedSilence();
}
// Request repaint
update();
}
