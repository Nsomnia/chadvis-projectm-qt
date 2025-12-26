/**
 * @file ProjectMWindow.hpp
 * @brief QWindow-based projectM visualizer
 * 
 * Based on musicvisqt pattern - uses QWindow with manual OpenGL context
 * instead of QOpenGLWidget for better control.
 */
#ifndef PROJECTMWINDOW_HPP
#define PROJECTMWINDOW_HPP

#include <QWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <memory>
#include <vector>

#include "projectm/ProjectMWrapper.hpp"

class ProjectMWindow : public QWindow, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit ProjectMWindow(QWindow *parent = nullptr);
    ~ProjectMWindow() override;

    // Audio control
    void loadAudioFile(const QString& filePath);
    void toggleAudioCapture();
    bool isAudioCapturing() const;

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void render();
    void handleMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void handleMediaError(QMediaPlayer::Error error, const QString& errorString);

private:
    void initialize();
    void cleanup();
    void processAudio();

    QOpenGLContext *m_context;
    std::unique_ptr<ProjectMWrapper> m_projectM;
    
    // Audio
    QMediaPlayer *m_mediaPlayer;
    QAudioOutput *m_audioOutput;
    QTimer m_renderTimer;
    
    // State
    bool m_initialized;
    int m_width;
    int m_height;
    bool m_audioCaptureActive;
    
    // For silent audio when no file/capture
    std::vector<float> m_silenceBuffer;
    int m_silenceCounter;
};

#endif // PROJECTMWINDOW_HPP
