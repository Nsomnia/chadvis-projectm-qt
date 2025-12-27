#pragma once
// VisualizerWidget.hpp - Qt OpenGL widget for visualization
// The pretty colors live here

#include "util/Types.hpp"
#include "ProjectMBridge.hpp"
#include "RenderTarget.hpp"

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <memory>

namespace vc {

class OverlayEngine;

class VisualizerWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
    
public:
    explicit VisualizerWidget(QWidget* parent = nullptr);
    ~VisualizerWidget() override;
    
    // ProjectM access
    ProjectMBridge& projectM() { return projectM_; }
    const ProjectMBridge& projectM() const { return projectM_; }
    
    // Overlay
    void setOverlayEngine(OverlayEngine* engine) { overlayEngine_ = engine; }
    
    // Recording support
    RenderTarget& renderTarget() { return renderTarget_; }
    void setRecordingSize(u32 width, u32 height);
    bool isRecording() const { return recording_; }
    void startRecording();
    void stopRecording();
    void setRenderRate(int fps);
    void feedAudio(const f32* data, u32 frames, u32 channels);

public slots:
    void toggleFullscreen();
    
signals:
    void frameReady();  // Emitted after each frame render (for recording)
    void fpsChanged(f32 actualFps);
    
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    
private slots:
    void onTimer();
    void updateFPS();
    
private:
    void renderFrame();
    void renderOverlay();
    
    ProjectMBridge projectM_;
    OverlayEngine* overlayEngine_{nullptr};
    
    RenderTarget renderTarget_;
    RenderTarget overlayTarget_;
    
    QTimer renderTimer_;
    QTimer fpsTimer_;
    
    bool recording_{false};
    u32 recordWidth_{1920};
    u32 recordHeight_{1080};
    
    u32 targetFps_{60};
    u32 frameCount_{0};
    f32 actualFps_{0.0f};
    
    bool initialized_{false};
    bool fullscreen_{false};
    QRect normalGeometry_;
};

} // namespace vc