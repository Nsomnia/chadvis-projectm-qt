#pragma once
// VisualizerWindow.hpp - QWindow-based visualization (fixes black canvas issue)
// Uses manual GL context management instead of QOpenGLWidget

#include "ProjectMBridge.hpp"
#include "RenderTarget.hpp"
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

public:
    explicit VisualizerWindow(QWindow* parent = nullptr);
    ~VisualizerWindow() override;

    // ProjectM access
    ProjectMBridge& projectM() {
        return projectM_;
    }
    const ProjectMBridge& projectM() const {
        return projectM_;
    }

    // Preset loading
    void loadPresetFromManager();
    void updateSettings();

    // Overlay
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

signals:
    void frameReady();
    void fpsChanged(f32 actualFps);

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void render(); // Called by timer or requestUpdate()
    void updateFPS();
    void onPresetRotationTimeout();

private:
    void initialize();
    void renderFrame();
    void renderOverlay();
    void cleanup();

    std::unique_ptr<QOpenGLContext> context_;

    ProjectMBridge projectM_;
    bool presetLoading_{false}; // Pause audio during preset transition
    OverlayEngine* overlayEngine_{nullptr};

    RenderTarget renderTarget_;
    RenderTarget overlayTarget_;

    QTimer renderTimer_;
    QTimer fpsTimer_;
    QTimer presetRotationTimer_;

    bool recording_{false};
    u32 recordWidth_{1920};
    u32 recordHeight_{1080};

    u32 targetFps_{60};
    u32 frameCount_{0};
    f32 actualFps_{0.0f};

    bool initialized_{false};
    bool fullscreen_{false};
    QRect normalGeometry_;

    // Audio buffer for thread-safe feeding
    // Uses a queue to accumulate audio data and feed at render rate
    std::mutex audioMutex_;
    std::vector<f32> audioQueue_; // Accumulates incoming audio data
    u32 audioSampleRate_{48000}; // Current audio sample rate

    // Preset loading protection
    std::mutex presetLoadMutex_; // Prevent concurrent preset loading
    bool presetLoadInProgress_{false}; // Guard against reentrancy
};

} // namespace vc
