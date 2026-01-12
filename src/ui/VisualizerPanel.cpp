#include "VisualizerPanel.hpp"
#include "MarqueeLabel.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

namespace vc {

VisualizerPanel::VisualizerPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void VisualizerPanel::setOverlayEngine(OverlayEngine* engine) {
    if (visualizerWindow_) {
        visualizerWindow_->setOverlayEngine(engine);
    }
}

void VisualizerPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    visualizerWindow_ = new VisualizerWindow();
    visualizerWindow_->show();

    auto* visualizerContainer =
            QWidget::createWindowContainer(visualizerWindow_, this);
    visualizerContainer->setSizePolicy(QSizePolicy::Expanding,
                                       QSizePolicy::Expanding);
    visualizerContainer->setMinimumSize(320, 240);
    visualizerContainer->setFocusPolicy(Qt::StrongFocus);
    visualizerContainer->setAttribute(Qt::WA_OpaquePaintEvent);
    visualizerContainer->setAttribute(Qt::WA_NoSystemBackground);
    layout->addWidget(visualizerContainer, 1);

    auto* controlBar = new QWidget();
    controlBar->setMaximumHeight(40);
    auto* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 4, 8, 4);
    controlLayout->setSpacing(8);

    prevPresetButton_ = new QPushButton("◀");
    prevPresetButton_->setFixedSize(28, 28);
    prevPresetButton_->setToolTip("Previous preset");
    connect(prevPresetButton_, &QPushButton::clicked, this, [this] {
        visualizerWindow_->projectM().previousPreset();
    });
    controlLayout->addWidget(prevPresetButton_);

    presetLabel_ = new MarqueeLabel();
    presetLabel_->setAlignment(Qt::AlignCenter);
    presetLabel_->setStyleSheet("color: #00ff88; font-weight: bold;");
    presetLabel_->setText("No preset");
    controlLayout->addWidget(presetLabel_, 1);

    nextPresetButton_ = new QPushButton("▶");
    nextPresetButton_->setFixedSize(28, 28);
    nextPresetButton_->setToolTip("Next preset");
    connect(nextPresetButton_, &QPushButton::clicked, this, [this] {
        if (visualizerWindow_) {
            visualizerWindow_->projectM().nextPreset();
        }
    });
    controlLayout->addWidget(nextPresetButton_);

    lockButton_ = new QPushButton("🔓");
    lockButton_->setFixedSize(28, 28);
    lockButton_->setCheckable(true);
    lockButton_->setToolTip("Lock preset");
    connect(lockButton_, &QPushButton::clicked, this, [this](bool checked) {
        visualizerWindow_->projectM().lockPreset(checked);
        lockButton_->setText(checked ? "🔒" : "🔓");
        emit lockPresetToggled(checked);
    });
    controlLayout->addWidget(lockButton_);

    fullscreenButton_ = new QPushButton("⛶");
    fullscreenButton_->setFixedSize(28, 28);
    fullscreenButton_->setToolTip("Fullscreen");
    connect(fullscreenButton_, &QPushButton::clicked, this, [this] {
        visualizerWindow_->toggleFullscreen();
        emit fullscreenRequested();
    });
    controlLayout->addWidget(fullscreenButton_);

    fpsLabel_ = new QLabel("0 FPS");
    fpsLabel_->setFixedWidth(60);
    fpsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fpsLabel_->setStyleSheet("color: #888888; font-size: 11px;");
    controlLayout->addWidget(fpsLabel_);

    layout->addWidget(controlBar);

    connect(visualizerWindow_,
            &VisualizerWindow::fpsChanged,
            this,
            &VisualizerPanel::updateFPS);

    connect(visualizerWindow_,
            &VisualizerWindow::presetNameUpdated,
            this,
            [this](const QString& name) {
                updatePresetName(name);
            });
}

void VisualizerPanel::updatePresetName(const QString& name) {
    presetLabel_->setText(name);
    presetLabel_->setToolTip(name);
}

void VisualizerPanel::updateFPS(f32 fps) {
    fpsLabel_->setText(QString("%1 FPS").arg(static_cast<int>(fps)));
}

} // namespace vc
