#include "VisualizerItem.hpp"
#include "audio/AudioEngine.hpp"
#include "visualizer/VisualizerRenderer.hpp"
#include "visualizer/PresetManager.hpp"
#include "core/Logger.hpp"

#include <QQuickWindow>
#include <QOpenGLContext>

namespace qml_bridge {

using namespace vc;

vc::AudioEngine* VisualizerItem::s_audioEngine = nullptr;
vc::PresetManager* VisualizerItem::s_presetManager = nullptr;

VisualizerItem::VisualizerItem(QQuickItem* parent)
	: QQuickItem(parent)
	, renderTimer_(std::make_unique<QTimer>())
	, silentAudioTimer_(std::make_unique<QTimer>())
{
	setFlag(ItemHasContents);
	connect(this, &QQuickItem::windowChanged, this, &VisualizerItem::handleWindowChanged);
}

VisualizerItem::~VisualizerItem() {
	cleanup();
}

void VisualizerItem::setGlobalAudioEngine(vc::AudioEngine* engine) {
	s_audioEngine = engine;
}

void VisualizerItem::setGlobalPresetManager(vc::PresetManager* manager) {
	s_presetManager = manager;
}

vc::AudioEngine* VisualizerItem::globalAudioEngine() {
	return s_audioEngine;
}

vc::PresetManager* VisualizerItem::globalPresetManager() {
	return s_presetManager;
}

void VisualizerItem::setFps(int fps) {
	if (fps_ != fps && fps > 0 && fps <= 120) {
		fps_ = fps;
		if (renderTimer_) {
			renderTimer_->setInterval(1000 / fps_);
		}
		emit fpsChanged();
	}
}

void VisualizerItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
	QQuickItem::geometryChange(newGeometry, oldGeometry);

	if (newGeometry.size() != oldGeometry.size()) {
		width_ = static_cast<vc::u32>(newGeometry.width());
		height_ = static_cast<vc::u32>(newGeometry.height());
	}
}

void VisualizerItem::handleWindowChanged(QQuickWindow* window) {
	if (!window) return;

	connect(window, &QQuickWindow::beforeSynchronizing, this, &VisualizerItem::sync, Qt::DirectConnection);
	connect(window, &QQuickWindow::sceneGraphInvalidated, this, &VisualizerItem::cleanup, Qt::DirectConnection);
	window->setColor(Qt::black);

	if (renderTimer_) {
		renderTimer_->setInterval(1000 / fps_);
		connect(renderTimer_.get(), &QTimer::timeout, window, &QQuickWindow::update, Qt::DirectConnection);
		renderTimer_->start();
	}

	// Timer to feed silent audio when no audio is playing (keeps projectM active)
	if (silentAudioTimer_) {
		silentAudioTimer_->setInterval(16); // ~60fps
		connect(silentAudioTimer_.get(), &QTimer::timeout, this, &VisualizerItem::feedSilentAudio, Qt::DirectConnection);
		silentAudioTimer_->start();
	}

	// Connect to audio engine's PCM signal for incremental audio feeding
	connectAudioSignal();
}

void VisualizerItem::connectAudioSignal() {
	if (audioConnected_ || !s_audioEngine) return;

	s_audioEngine->pcmReceived.connect([this](const std::vector<float>& data, u32 frames, u32 channels, u32 sampleRate) {
		onPcmReceived(data, frames, channels, sampleRate);
	});

	audioConnected_ = true;
	LOG_INFO("VisualizerItem: Connected to audio engine PCM signal");
}

void VisualizerItem::onPcmReceived(const std::vector<float>& data, vc::u32 frames, vc::u32 channels, vc::u32 sampleRate) {
	if (renderer_ && initialized_) {
		renderer_->feedAudio(data.data(), frames, channels, sampleRate);
	}
}

void VisualizerItem::feedSilentAudio() {
	if (!renderer_ || !initialized_) return;

	// Only feed silent audio when not playing
	if (s_audioEngine && !s_audioEngine->isPlaying()) {
		static constexpr vc::u32 SILENT_FRAMES = 1024;
		static std::vector<float> silentBuffer(SILENT_FRAMES * 2, 0.0f);
		renderer_->feedAudio(silentBuffer.data(), SILENT_FRAMES, 2, 48000);
	}
}

void VisualizerItem::sync() {
	if (!renderer_) {
		renderer_ = new VisualizerRenderer();
		connect(window(), &QQuickWindow::beforeRendering, this, &VisualizerItem::onBeforeRendering, Qt::DirectConnection);
		connect(window(), &QQuickWindow::beforeRenderPassRecording, this, &VisualizerItem::onBeforeRenderPassRecording, Qt::DirectConnection);
	}
}

void VisualizerItem::onBeforeRendering() {
	if (!initialized_) {
		initializeRenderer();
	}
}

void VisualizerItem::onBeforeRenderPassRecording() {
	if (renderer_ && initialized_ && window()) {
		updateDimensions();
		renderer_->render(x_, y_, width_, height_, window()->isExposed());
	}
}

void VisualizerItem::updateDimensions() {
	if (!window()) return;

	auto dpr = window()->devicePixelRatio();
	width_ = static_cast<vc::u32>(width() * dpr);
	height_ = static_cast<vc::u32>(height() * dpr);

	auto posInWindow = mapToScene(QPointF(0, 0));
	x_ = static_cast<vc::u32>(posInWindow.x() * dpr);
	y_ = static_cast<vc::u32>(posInWindow.y() * dpr);
}

void VisualizerItem::initializeRenderer() {
	if (initialized_ || !window()) return;

	initializeOpenGLFunctions();

	if (!renderer_) {
		renderer_ = new VisualizerRenderer();
	}

	renderer_->initialize(width_, height_);

	if (s_presetManager) {
		const auto& presets = s_presetManager->allPresets();
		if (!presets.empty()) {
			renderer_->projectM().engine().loadPreset(presets[0].path.string());
			LOG_INFO("VisualizerItem: Loaded preset: {}", presets[0].name);
		}
	}

	initialized_ = true;
	LOG_INFO("VisualizerItem: Initialized {}x{}", width_, height_);
}

void VisualizerItem::cleanup() {
	if (renderer_) {
		delete renderer_;
		renderer_ = nullptr;
	}
	initialized_ = false;
	audioConnected_ = false;
}

} // namespace qml_bridge
