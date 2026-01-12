#pragma once

#include "RenderTarget.hpp"
#include "projectm/Bridge.hpp"
#include "util/GLIncludes.hpp"
#include "util/Types.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
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

    pm::Bridge& projectM() {
        return projectM_;
    }
    const pm::Bridge& projectM() const {
        return projectM_;
    }

    void loadPresetFromManager();
    void updateSettings();
    void setOverlayEngine(OverlayEngine* engine) {
        overlayEngine_ = engine;
    }

    // Preset control with GL context safety
    void nextPreset(bool smooth = true);
    void previousPreset(bool smooth = true);
    void randomPreset(bool smooth = true);
    void lockPreset(bool locked);

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

private:
    void initialize();
    void renderFrame();
    void setupPBOs();
    void destroyPBOs();
    void captureAsync();
    void cleanup();

    std::unique_ptr<QOpenGLShaderProgram> blitProgram_;
    QOpenGLVertexArrayObject blitVao_;
    QOpenGLBuffer blitVbo_;
    void initBlitResources();
    void drawTexture(GLuint textureId);

    std::unique_ptr<QOpenGLContext> context_;
    bool presetLoading_{false};
    OverlayEngine* overlayEngine_{nullptr};

    RenderTarget renderTarget_;
    RenderTarget overlayTarget_;

    QTimer renderTimer_;
    QTimer fpsTimer_;

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

    pm::Bridge projectM_;
};

} // namespace vc
