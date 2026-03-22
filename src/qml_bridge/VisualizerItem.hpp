/**
* @file VisualizerItem.hpp
* @brief QQuickItem for ProjectM OpenGL rendering in QML
*
* This class integrates projectM visualizer as a QML item using the
* "OpenGL under QML" pattern with beforeRendering/beforeRenderPassRecording.
*
* Architecture:
* - VisualizerItem (GUI thread): Manages QML properties, window signals
* - VisualizerRenderer (render thread): Handles all OpenGL/projectM calls
*
* @version 2.0.0
*/

#pragma once

#include <QQuickItem>
#include <QTimer>
#include <QOpenGLFunctions>
#include "util/Types.hpp"
#include <memory>

namespace vc {
class VisualizerRenderer;
class AudioEngine;
class PresetManager;
}

namespace qml_bridge {

/**
* @brief QQuickItem that renders ProjectM visualizer using OpenGL underlay
*
* Uses Qt's "OpenGL under QML" pattern for direct rendering beneath the
* Qt Quick scene graph. This allows projectM to render directly to the
* window's framebuffer.
*
* Usage in QML:
* @code
* VisualizerItem {
* anchors.fill: parent
* fps: 60
* }
* @endcode
*/
class VisualizerItem : public QQuickItem, protected QOpenGLFunctions {
Q_OBJECT

Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)

public:
explicit VisualizerItem(QQuickItem* parent = nullptr);
~VisualizerItem() override;

static void setGlobalAudioEngine(vc::AudioEngine* engine);
static void setGlobalPresetManager(vc::PresetManager* manager);
static vc::AudioEngine* globalAudioEngine();
static vc::PresetManager* globalPresetManager();

int fps() const { return fps_; }
void setFps(int fps);

protected:
void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

public slots:
void handleWindowChanged(QQuickWindow* window);
void cleanup();

signals:
void fpsChanged();

private slots:
void sync();
void onBeforeRendering();
void onBeforeRenderPassRecording();

private:
void initializeRenderer();
void updateDimensions();

static vc::AudioEngine* s_audioEngine;
static vc::PresetManager* s_presetManager;

vc::VisualizerRenderer* renderer_{nullptr};

bool initialized_{false};
int fps_{60};
std::unique_ptr<QTimer> renderTimer_;
vc::u32 width_{0};
vc::u32 height_{0};
vc::u32 x_{0};
vc::u32 y_{0};
};

} // namespace qml_bridge
