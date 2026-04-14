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
 * @version 2.1.0
 */

#pragma once

#include <QQuickItem>
#include <QTimer>
#include <QOpenGLFunctions_3_3_Core>
#include "util/Types.hpp"
#include <memory>

namespace vc {
class VisualizerRenderer;
class AudioEngine;
class PresetManager;
}

namespace qml_bridge {

class VisualizerItem : public QQuickItem, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
    QML_ELEMENT

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
	void onPcmReceived(const std::vector<float>& data, vc::u32 frames, vc::u32 channels, vc::u32 sampleRate);

signals:
	void fpsChanged();

private slots:
	void sync();
	void onBeforeRendering();
	void onBeforeRenderPassRecording();
	void feedSilentAudio();

private:
	void initializeRenderer();
	void updateDimensions();
	void connectAudioSignal();

	static vc::AudioEngine* s_audioEngine;
	static vc::PresetManager* s_presetManager;

	vc::VisualizerRenderer* renderer_{nullptr};

	bool initialized_{false};
	bool audioConnected_{false};
	int fps_{60};
	std::unique_ptr<QTimer> renderTimer_;
	std::unique_ptr<QTimer> silentAudioTimer_;
	vc::u32 width_{0};
	vc::u32 height_{0};
	vc::u32 x_{0};
	vc::u32 y_{0};
};

} // namespace qml_bridge
