/**
 * @file VisualizerItem.cpp
 * @brief QQuickItem implementation for ProjectM OpenGL rendering
 *
 * @version 1.0.0
 */

#include "VisualizerItem.hpp"
#include "audio/AudioEngine.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include "util/Types.hpp"

#include <QQuickWindow>
#include <QOpenGLContext>

namespace qml_bridge {

using namespace vc;

namespace {
VisualizerItem* s_instance = nullptr;
}

VisualizerItem::VisualizerItem(QQuickItem* parent)
    : QQuickItem(parent)
    , renderTimer_(std::make_unique<QTimer>())
{
    setFlag(ItemHasContents);
    connect(this, &QQuickItem::windowChanged, this, &VisualizerItem::handleWindowChanged);
}

VisualizerItem::~VisualizerItem() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
    cleanup();
}

VisualizerItem* VisualizerItem::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    if (!s_instance) {
        s_instance = new VisualizerItem();
    }
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    return s_instance;
}

void VisualizerItem::setVisualizerWindow(vc::VisualizerWindow* window) {
    vizWindow_ = window;
}

void VisualizerItem::setAudioEngine(vc::AudioEngine* engine) {
    audioEngine_ = engine;
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
        if (vizWindow_) {
            u32 w = static_cast<u32>(newGeometry.width());
            u32 h = static_cast<u32>(newGeometry.height());
            vizWindow_->resize(QSize(w, h));
        }
    }
}

void VisualizerItem::handleWindowChanged(QQuickWindow* window) {
    if (!window) return;

    connect(window, &QQuickWindow::beforeRendering, this, &VisualizerItem::renderFrame, Qt::DirectConnection);

    if (renderTimer_) {
        renderTimer_->setInterval(1000 / fps_);
        connect(renderTimer_.get(), &QTimer::timeout, window, &QQuickWindow::update, Qt::DirectConnection);
        renderTimer_->start();
    }
}

void VisualizerItem::renderFrame() {
    if (!vizWindow_ || !initialized_) {
        initialize();
    }

    if (audioEngine_) {
        auto pcm = audioEngine_->currentPCM();
        if (!pcm.empty()) {
            vizWindow_->feedAudio(pcm.data(), static_cast<u32>(pcm.size() / 2), 2, 48000);
        }
    }
}

void VisualizerItem::initialize() {
    if (!vizWindow_) return;

    initialized_ = true;
}

void VisualizerItem::cleanup() {
    initialized_ = false;
}

} // namespace qml_bridge
