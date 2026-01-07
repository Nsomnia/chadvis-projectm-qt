#pragma once
// VisualizerPanel.hpp - Container for visualizer with controls
// The frame around the pretty colors

#include "util/Types.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace vc {

class OverlayEngine;
class MarqueeLabel;

class VisualizerPanel : public QWidget {
    Q_OBJECT

public:
    explicit VisualizerPanel(QWidget* parent = nullptr);

    VisualizerWindow* visualizer() {
        return visualizerWindow_;
    }
    void setOverlayEngine(OverlayEngine* engine);

signals:
    void fullscreenRequested();
    void presetChangeRequested();
    void lockPresetToggled(bool locked);

public slots:
    void updatePresetName(const QString& name);
    void updateFPS(f32 fps);

private:
    void setupUI();

    VisualizerWindow* visualizerWindow_{nullptr};
    MarqueeLabel* presetLabel_{nullptr};
    QLabel* fpsLabel_{nullptr};
    QPushButton* fullscreenButton_{nullptr};
    QPushButton* lockButton_{nullptr};
    QPushButton* nextPresetButton_{nullptr};
    QPushButton* prevPresetButton_{nullptr};
};

} // namespace vc
