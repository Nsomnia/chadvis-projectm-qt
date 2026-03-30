// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Visualizer QFBO implementation with lock-free queue

#include "VisualizerQFBO.hpp"
#include "audio/AudioEngine.hpp"
#include "audio/AudioQueue.hpp"
#include "visualizer/VisualizerRenderer.hpp"
#include "visualizer/PresetManager.hpp"
#include "core/Logger.hpp"

#include <QQuickWindow>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

namespace qml_bridge {

using namespace vc;

// Static globals initialization
vc::AudioEngine* VisualizerQFBO::s_audioEngine = nullptr;
vc::PresetManager* VisualizerQFBO::s_presetManager = nullptr;

// ============================================================================
// VisualizerQFBO (GUI Thread)
// ============================================================================

VisualizerQFBO::VisualizerQFBO(QQuickItem* parent)
: QQuickFramebufferObject(parent)
, silentAudioTimer_(std::make_unique<QTimer>())
, renderTimer_(std::make_unique<QTimer>())
{
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::windowChanged, this, &VisualizerQFBO::handleWindowChanged);
}

VisualizerQFBO::~VisualizerQFBO() = default;

void VisualizerQFBO::setGlobalAudioEngine(vc::AudioEngine* engine) {
    s_audioEngine = engine;
}

void VisualizerQFBO::setGlobalPresetManager(vc::PresetManager* manager) {
    s_presetManager = manager;
}

vc::AudioEngine* VisualizerQFBO::globalAudioEngine() {
    return s_audioEngine;
}

vc::PresetManager* VisualizerQFBO::globalPresetManager() {
    return s_presetManager;
}

void VisualizerQFBO::setFps(int fps) {
    if (fps > 0 && fps <= 120 && fps_.load() != fps) {
        fps_.store(fps);
        emit fpsChanged();
    }
}

void VisualizerQFBO::setPresetIndex(int index) {
    if (presetIndex_.load() != index) {
        presetIndex_.store(index);
        emit presetIndexChanged();
    }
}

void VisualizerQFBO::setFullscreen(bool fullscreen) {
    if (fullscreen_.load() != fullscreen) {
        fullscreen_.store(fullscreen);
        emit fullscreenChanged();
    }
}

void VisualizerQFBO::handleWindowChanged(QQuickWindow* window) {
    if (!window) return;

    connect(window, &QQuickWindow::sceneGraphInvalidated, this, &VisualizerQFBO::cleanup,
            Qt::DirectConnection);

    window->setColor(Qt::black);

    if (renderTimer_) {
        int interval = 1000 / fps_.load();
        renderTimer_->setInterval(interval);
        connect(renderTimer_.get(), &QTimer::timeout, window, &QQuickWindow::update,
                Qt::DirectConnection);
        renderTimer_->start();
        LOG_INFO("VisualizerQFBO: Render timer started at {} FPS", fps_.load());
    }

    if (silentAudioTimer_) {
        silentAudioTimer_->setInterval(16);
        connect(silentAudioTimer_.get(), &QTimer::timeout, this, &VisualizerQFBO::feedSilentAudio,
                Qt::DirectConnection);
        silentAudioTimer_->start();
    }

    connectAudioSignal();

    QTimer::singleShot(0, this, [this, window]() {
        update();
        window->update();
    });
}

void VisualizerQFBO::cleanup() {
}

void VisualizerQFBO::connectAudioSignal() {
	if (audioConnected_.load() || !s_audioEngine) return;

	connect(s_audioEngine, &vc::AudioEngine::pcmReceived,
		this, [this](const std::vector<float>& data, u32 frames, u32 channels, u32 sampleRate) {
			onPcmReceived(data, frames, channels, sampleRate);
		}, Qt::QueuedConnection);

    audioConnected_.store(true);
    LOG_INFO("VisualizerQFBO: Connected to audio engine PCM signal");
}

void VisualizerQFBO::onPcmReceived(const std::vector<float>&, vc::u32,
                                   vc::u32, vc::u32) {
    // Audio is now handled via lock-free queue in AudioEngine
    // This callback is kept for backward compatibility but does nothing
}

void VisualizerQFBO::feedSilentAudio() {
    // Silent audio no longer needed - lock-free queue handles everything
}

void VisualizerQFBO::updateDimensions() {
    if (!window()) return;

    devicePixelRatio_ = window()->devicePixelRatio();
    width_.store(static_cast<vc::u32>(width() * devicePixelRatio_));
    height_.store(static_cast<vc::u32>(height() * devicePixelRatio_));
}

void VisualizerQFBO::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        updateDimensions();
    }
}

QQuickFramebufferObject::Renderer* VisualizerQFBO::createRenderer() const {
    return new VisualizerQFBORenderer();
}

// ============================================================================
// VisualizerQFBORenderer (Render Thread)
// ============================================================================

VisualizerQFBORenderer::VisualizerQFBORenderer() = default;

VisualizerQFBORenderer::~VisualizerQFBORenderer() {
    // Don't call cleanup() - OpenGL context may be destroyed
    // Just delete the renderer, let it handle its own cleanup in destructor
    delete renderer_;
    renderer_ = nullptr;
}

QOpenGLFramebufferObject* VisualizerQFBORenderer::createFramebufferObject(const QSize& size) {
    QOpenGLFramebufferObjectFormat format;
    format.setSamples(16);
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);

    auto* fbo = new QOpenGLFramebufferObject(size, format);

    // Store the FBO size for render()
    width_ = static_cast<u32>(size.width());
    height_ = static_cast<u32>(size.height());

    if (!renderer_) {
        renderer_ = new VisualizerRenderer();
    }

    if (!initialized_) {
        renderer_->initialize(width_, height_);

        if (VisualizerQFBO::globalPresetManager()) {
            const auto& presets = VisualizerQFBO::globalPresetManager()->allPresets();
            if (!presets.empty()) {
                renderer_->projectM().engine().loadPreset(presets[0].path.string());
                LOG_INFO("VisualizerQFBORenderer: Loaded preset: {}", presets[0].name);
            }
        }

        initialized_ = true;
        LOG_INFO("VisualizerQFBORenderer: Initialized {}x{}", width_, height_);
    }

    return fbo;
}

void VisualizerQFBORenderer::synchronize(QQuickFramebufferObject* item) {
    auto* qmlItem = static_cast<VisualizerQFBO*>(item);
    if (!qmlItem) return;

    width_ = qmlItem->width_.load();
    height_ = qmlItem->height_.load();
    presetIndex_ = qmlItem->presetIndex_.load();
    recording_ = qmlItem->isRecording();

    // Set audio queue on renderer (lock-free, no data copy needed)
    if (renderer_ && qmlItem->globalAudioEngine()) {
        renderer_->setAudioQueue(&qmlItem->globalAudioEngine()->audioQueue());
    }
}

void VisualizerQFBORenderer::render() {
    if (!renderer_ || !initialized_ || width_ == 0 || height_ == 0) {
        static int logCount = 0;
        if (logCount++ < 5) {
            LOG_INFO("QFBO render() early return: renderer={}, init={}, w={}, h={}",
                     (void*)renderer_, initialized_, width_, height_);
        }
        return;
    }

    if (!glInitialized_) {
        if (!initializeOpenGLFunctions()) {
            LOG_ERROR("QFBO: Failed to initialize GL functions");
            return;
        }
        glInitialized_ = true;
        LOG_INFO("QFBO: GL functions initialized");
    }

    auto* audioQueue = renderer_->audioQueue();
    if (audioQueue) {
        constexpr u32 BATCH_SIZE = 4096;
        alignas(64) float batchBuffer[BATCH_SIZE * 2];
        u32 framesToFeed = 2048;
        u32 popped = audioQueue->popVizBatch(batchBuffer, framesToFeed);
        if (popped > 0) {
            renderer_->projectM().engine().addPCMDataInterleaved(batchBuffer, popped, 2);
        }
    }

    renderer_->projectM().syncState();

    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));

    // TEST: Clear to red to verify FBO is rendering at all
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

} // namespace qml_bridge
