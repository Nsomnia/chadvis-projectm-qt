/**
 * @file VisualizerItem.hpp
 * @brief QQuickItem for ProjectM OpenGL rendering in QML
 *
 * This class integrates projectM visualizer as a QML item, handling:
 * - OpenGL context management via QQuickItem
 * - Frame rendering at target FPS
 * - Audio feeding from AudioEngine
 * - Touch/mouse interaction
 *
 * @version 1.0.0
 */

#pragma once

#include <QQuickItem>
#include <QTimer>
#include <memory>

namespace vc {
class VisualizerRenderer;
class AudioEngine;
class VisualizerWindow;
}

namespace qml_bridge {

/**
 * @brief QQuickItem that renders ProjectM visualizer
 *
 * Usage in QML:
 * @code
 * VisualizerItem {
 *     anchors.fill: parent
 *     fps: 60
 * }
 * @endcode
 */
class VisualizerItem : public QQuickItem {
    Q_OBJECT

    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)

public:
    explicit VisualizerItem(QQuickItem* parent = nullptr);
    ~VisualizerItem() override;

    static VisualizerItem* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    void setVisualizerWindow(vc::VisualizerWindow* window);
    void setAudioEngine(vc::AudioEngine* engine);

    int fps() const { return fps_; }
    void setFps(int fps);

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

public slots:
    void handleWindowChanged(QQuickWindow* window);
    void renderFrame();

signals:
    void fpsChanged();

private:
    void initialize();
    void cleanup();

    vc::VisualizerWindow* vizWindow_{nullptr};
    vc::AudioEngine* audioEngine_{nullptr};

    bool initialized_{false};
    int fps_{60};
    std::unique_ptr<QTimer> renderTimer_;
};

} // namespace qml_bridge
