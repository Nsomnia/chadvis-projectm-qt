/**
 * @file VisualizerWindow.hpp
 * @brief Qt Window for the visualizer.
 *
 * This file defines the VisualizerWindow class which manages the Qt window
 * lifecycle, OpenGL context creation, input events, and timers. It delegates
 * the actual rendering to VisualizerRenderer.
 *
 * @section Embedding
 * This window is never a top-level window.  `Application` constructs it with
 * no parent and hands the pointer to the QML `WindowContainer` in
 * `src/qml/views/VideoView.qml`; Qt's container then parents the embedded
 * QWindow to the QQuickWindow (`QQuickWindowContainer::updatePolish` calls
 * `QWindow::setParent`) and re-lays it out from the item rect on every
 * polish.  Two consequences have already produced dead code, so do not add
 * more without re-reading this:
 *
 * - `showFullScreen()` is inert on a container child, and the nav rail,
 *   header, footer, overlay and karaoke layers are QML siblings in the same
 *   scene that would stay on screen regardless.  Fullscreen is therefore
 *   owned by the QML `ApplicationWindow` (`src/qml/main.qml`) alone.
 * - A container child is not the activated window, so `keyPressEvent` is not
 *   a dependable input path.  The preset bindings it still carries are kept
 *   only because `R` (random preset) and `L` (lock preset) have no QML
 *   equivalent, and unprovable unreachability is not a reason to drop a
 *   capability.  Do not treat them as the primary binding path — `main.qml`
 *   owns the QML-side `Shortcut`s.
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
    void frameCaptured(std::vector<u8> data, u32 width, u32 height, i64 timestamp);
    void fpsChanged(f32 actualFps);

public:
    explicit VisualizerWindow(PresetManager& presetManager, QWindow* parent = nullptr);
    ~VisualizerWindow() override;

    VisualizerRenderer& renderer() { return *renderer_; }

    pm::Bridge& projectM() { return renderer_->projectM(); }
    const pm::Bridge& projectM() const { return renderer_->projectM(); }

    void loadPresetFromManager();
    void updateSettings();

    void nextPreset(bool smooth = true);
    void previousPreset(bool smooth = true);
    void randomPreset(bool smooth = true);
    void lockPreset(bool locked);

    void setRecordingSize(u32 width, u32 height);
    void startRecording();
    void stopRecording();
    void setRenderRate(int fps);
    [[nodiscard]] f32 actualFps() const { return actualFps_; }
    void feedAudio(const f32* data, u32 frames, u32 channels, u32 sampleRate);

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void render();
    void updateFPS();

private:
    void initialize();

    /// Actual GL framebuffer size in device pixels (logical size x DPR).
    /// On macOS Retina DPR=2; feeding logical points to glViewport renders
    /// into 1/(DPR^2) of the surface.
    QSize framebufferSize() const {
        const qreal dpr = devicePixelRatio();
        return QSize(qRound(width() * dpr), qRound(height() * dpr));
    }

    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<VisualizerRenderer> renderer_;

    QTimer renderTimer_;
    QTimer fpsTimer_;

    u32 frameCount_{0};
    f32 actualFps_{0.0f};
    bool initialized_{false};
    // A recording request can arrive before the native QWindow has been
    // exposed (for example, the Video shortcut navigates and starts in the
    // same QML turn).  Keep the request until the GL context is initialized.
    bool recordingRequested_{false};
};

} // namespace vc
