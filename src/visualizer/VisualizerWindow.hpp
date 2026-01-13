/**
 * @file VisualizerWindow.hpp
 * @brief Qt Window for the visualizer.
 *
 * This file defines the VisualizerWindow class which manages the Qt window
 * lifecycle, OpenGL context creation, input events, and timers. It delegates
 * the actual rendering to VisualizerRenderer.
 *
 * @section Dependencies
 * - Qt GUI (QWindow, QOpenGLContext)
 * - VisualizerRenderer
 *
 * @section Patterns
 * - Composition: Owns VisualizerRenderer.
 * - Event Handler: Processes Qt events.
 */

#pragma once
#include <QOpenGLContext>
#include <QTimer>
#include <QWindow>
#include <memory>
#include "VisualizerRenderer.hpp"

namespace vc {

class VisualizerWindow : public QWindow {
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

    VisualizerRenderer& renderer() {
        return *renderer_;
    }

    pm::Bridge& projectM() {
        return renderer_->projectM();
    }
    const pm::Bridge& projectM() const {
        return renderer_->projectM();
    }

    RenderTarget& renderTarget() {
        return renderer_->renderTarget();
    }

    void loadPresetFromManager();
    void updateSettings();
    void setOverlayEngine(OverlayEngine* engine);

    void nextPreset(bool smooth = true);
    void previousPreset(bool smooth = true);
    void randomPreset(bool smooth = true);
    void lockPreset(bool locked);

    void setRecordingSize(u32 width, u32 height);
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

    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<VisualizerRenderer> renderer_;

    QTimer renderTimer_;
    QTimer fpsTimer_;

    u32 frameCount_{0};
    f32 actualFps_{0.0f};
    bool initialized_{false};
    bool fullscreen_{false};
    QRect normalGeometry_;
};

} // namespace vc
