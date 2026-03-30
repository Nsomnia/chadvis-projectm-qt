/**
* @file VisualizerQFBO.hpp
* @brief QQuickFramebufferObject wrapper for projectM OpenGL rendering in QML
* @version 1.2.0
* @last-edited 2026-03-30 06:45:00
*
* This class replaces the broken VisualizerItem (QQuickItem-based) approach.
* QFBO provides a dedicated OpenGL FBO context in Qt 6's render thread,
* solving the RHI/OpenGL conflict where Qt 6 defaults to Vulkan/Metal/D3D11.
*
* Architecture:
* - VisualizerQFBO (GUI thread): QML properties, signals, state sync
* - VisualizerQFBORenderer (render thread): OpenGL lifecycle, FBO management
* - VisualizerRenderer: Existing projectM rendering logic (reused)
*/

#pragma once

#include <QQuickFramebufferObject>
#include <QTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <atomic>
#include <vector>
#include "util/Types.hpp"

namespace vc {
class VisualizerRenderer;
class AudioEngine;
class PresetManager;
class AudioQueue;
}

namespace qml_bridge {

class VisualizerQFBORenderer;

class VisualizerQFBO : public QQuickFramebufferObject {
Q_OBJECT

Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
Q_PROPERTY(int presetIndex READ presetIndex WRITE setPresetIndex NOTIFY presetIndexChanged)
Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingChanged)
Q_PROPERTY(bool isFullscreen READ isFullscreen WRITE setFullscreen NOTIFY fullscreenChanged)

friend class VisualizerQFBORenderer;

public:
explicit VisualizerQFBO(QQuickItem* parent = nullptr);
~VisualizerQFBO() override;

Renderer* createRenderer() const override;

static void setGlobalAudioEngine(vc::AudioEngine* engine);
static void setGlobalPresetManager(vc::PresetManager* manager);
static vc::AudioEngine* globalAudioEngine();
static vc::PresetManager* globalPresetManager();

int fps() const { return fps_.load(); }
void setFps(int fps);

int presetIndex() const { return presetIndex_.load(); }
void setPresetIndex(int index);

bool isRecording() const { return recording_.load(); }
bool isFullscreen() const { return fullscreen_.load(); }
void setFullscreen(bool fullscreen);

signals:
void fpsChanged();
void presetIndexChanged();
void recordingChanged();
void fullscreenChanged();

protected:
void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
void itemChange(ItemChange change, const ItemChangeData& value) override;

public slots:
void handleWindowChanged(QQuickWindow* window);
void onPcmReceived(const std::vector<float>& data, vc::u32 frames,
vc::u32 channels, vc::u32 sampleRate);
void feedSilentAudio();
void cleanup();

private:
void connectAudioSignal();
void updateDimensions();
void forceInitialUpdate();

static vc::AudioEngine* s_audioEngine;
static vc::PresetManager* s_presetManager;

std::atomic<int> fps_{60};
std::atomic<int> presetIndex_{0};
std::atomic<bool> recording_{false};
std::atomic<bool> fullscreen_{false};
std::atomic<bool> audioConnected_{false};

std::atomic<vc::u32> width_{0};
std::atomic<vc::u32> height_{0};
qreal devicePixelRatio_{1.0};

std::unique_ptr<QTimer> silentAudioTimer_;
std::unique_ptr<QTimer> renderTimer_;
};

class VisualizerQFBORenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions_3_3_Core {
public:
    explicit VisualizerQFBORenderer();
    ~VisualizerQFBORenderer() override;

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;
    void synchronize(QQuickFramebufferObject* item) override;
    void render() override;

private:
    vc::VisualizerRenderer* renderer_{nullptr};
    bool initialized_{false};
    bool glInitialized_{false};

    vc::u32 width_{0};
    vc::u32 height_{0};
    int presetIndex_{0};
    bool recording_{false};
};

} // namespace qml_bridge
