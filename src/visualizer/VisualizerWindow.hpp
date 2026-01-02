#pragma once
// VisualizerWindow.hpp - QWindow-based visualization
// Now with Async PBO Recording for peak performance.

#include "ProjectMBridge.hpp"
#include "RenderTarget.hpp"
#include "util/GLIncludes.hpp"
#include "util/Types.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QWindow>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace vc {

class OverlayEngine;

class VisualizerWindow : public QWindow, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

signals:
    void presetNameUpdated(const QString& name);
    void frameReady();
    void frameCaptured(std::vector<u8> data,
                       u32 width,
                       u32 height,
                       i64 timestamp);
    void fpsChanged(f32 actualFps);

public:
    explicit VisualizerWindow(QWindow* parent = nullptr);
    ~VisualizerWindow() override;

    ProjectMBridge& projectM() {
        return projectM_;
    }
    const ProjectMBridge& projectM() const {
        return projectM_;
    }

    void loadPresetFromManager();
    void updateSettings();
    void setOverlayEngine(OverlayEngine* engine) {
        overlayEngine_ = engine;
    }

    // Recording support
    RenderTarget& renderTarget() {
        return renderTarget_;
    }
    void setRecordingSize(u32 width, u32 height);
    bool isRecording() const {
        return recording_;
    }
    void startRecording();
    void stopRecording();
    void setRenderRate(int fps);
    void feedAudio(const f32* data, u32 frames, u32 channels, u32 sampleRate);

public slots:
    void toggleFullscreen();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void render();
    void updateFPS();
    void onPresetRotationTimeout();

private:
    void initialize();
    void renderFrame();
    void setupPBOs();
    void destroyPBOs();
    void captureAsync();
    void cleanup();

    std::unique_ptr<QOpenGLContext> context_;
    ProjectMBridge projectM_;
    bool presetLoading_{false};
    OverlayEngine* overlayEngine_{nullptr};

    RenderTarget renderTarget_;
    RenderTarget overlayTarget_;

    QTimer renderTimer_;
    QTimer fpsTimer_;
    QTimer presetRotationTimer_;

    // Recording & PBOs
    bool recording_{false};
    u32 recordWidth_{1920};
    u32 recordHeight_{1080};
    GLuint pbos_[2]{0, 0};
    u32 pboIndex_{0};
    bool pboAvailable_{false};
    std::vector<u8> captureBuffer_;

    u32 targetFps_{60};
    u32 frameCount_{0};
    f32 actualFps_{0.0f};

    bool initialized_{false};
    bool fullscreen_{false};
    QRect normalGeometry_;

    std::mutex audioMutex_;
    std::vector<f32> audioQueue_;
    u32 audioSampleRate_{48000};

    std::mutex presetLoadMutex_;
    bool presetLoadInProgress_{false};
};

} // namespace vc
