/**
 * @file VisualizerWidget.hpp
 * @brief OpenGL widget for projectM visualization rendering.
 *
 * Single responsibility: OpenGL context management and render calls.
 * Delegates actual visualization to ProjectMWrapper.
 */
#ifndef VISUALIZERWIDGET_HPP
#define VISUALIZERWIDGET_HPP

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QHideEvent>
#include <QShowEvent>
#include <QEvent>
#include <memory>

class ProjectMWrapper;

class VisualizerWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit VisualizerWidget(QWidget *parent = nullptr);
    ~VisualizerWidget() override;

    // Audio control
    bool startAudioCapture();
    void stopAudioCapture();
    bool isAudioCapturing() const;

protected:
    // QOpenGLWidget overrides
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onFrameTimer();

private:
    std::unique_ptr<ProjectMWrapper> m_projectM;
    QTimer m_frameTimer;
    bool m_initialized = false;
};

#endif // VISUALIZERWIDGET_HPP
