#pragma once

#include "util/Types.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include "audio/AudioBridge.hpp"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace vc {

class OverlayEngine;
class MarqueeLabel;

using audio::AudioBridge;

class VisualizerPanel : public QWidget {
    Q_OBJECT

public:
    explicit VisualizerPanel(QWidget* parent = nullptr);

    VisualizerWindow* visualizer() {
        return visualizerWindow_;
    }
    void setOverlayEngine(OverlayEngine* engine);
    void setAudioBridge(AudioBridge* bridge);

signals:
    void fullscreenRequested();
    void presetChangeRequested();
    void lockPresetToggled(bool locked);

public slots:
    void updatePresetName(const QString& name);
    void updateFPS(f32 fps);
    void onAudioBridgePCM();  // New: Handle PCM from AudioBridge

private:
    void setupUI();

    VisualizerWindow* visualizerWindow_{nullptr};
    MarqueeLabel* presetLabel_{nullptr};
    QLabel* fpsLabel_{nullptr};
    QPushButton* fullscreenButton_{nullptr};
    QPushButton* lockButton_{nullptr};
    QPushButton* nextPresetButton_{nullptr};
    QPushButton* prevPresetButton_{nullptr};
    AudioBridge* audioBridge_{nullptr};
};

} // namespace vc
