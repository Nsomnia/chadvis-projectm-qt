#include "VisualizerPanel.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

namespace vc {

VisualizerPanel::VisualizerPanel(QWidget* parent)
    : QWidget(parent)
{
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
    
    // Create VisualizerWindow (QWindow)
    visualizerWindow_ = new VisualizerWindow();
    
    // Embed it using createWindowContainer
    auto* visualizerContainer = QWidget::createWindowContainer(visualizerWindow_, this);
    visualizerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    visualizerContainer->setMinimumSize(320, 240);
    visualizerContainer->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(visualizerContainer, 1);
    
    // Control bar
    auto* controlBar = new QWidget();
    controlBar->setMaximumHeight(40);
    auto* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 4, 8, 4);
    controlLayout->setSpacing(8);
    
    prevPresetButton_ = new QPushButton("◀");
    prevPresetButton_->setFixedSize(28, 28);
    prevPresetButton_->setToolTip("Previous preset");
    connect(prevPresetButton_, &QPushButton::clicked, this, [this] {
        LOG_DEBUG("VisualizerPanel: Previous preset button clicked");
        if (!visualizerWindow_) {
            LOG_ERROR("VisualizerPanel: visualizerWindow_ is null!");
            return;
        }
        auto& pm = visualizerWindow_->projectM();
        LOG_DEBUG("VisualizerPanel: projectM() returned, checking state...");
        LOG_DEBUG("VisualizerPanel: isPresetLocked = {}", pm.isPresetLocked());
        LOG_DEBUG("VisualizerPanel: presets().count() = {}", pm.presets().count());
        LOG_DEBUG("VisualizerPanel: presets().currentIndex() = {}", pm.presets().currentIndex());
        pm.previousPreset();
        LOG_DEBUG("VisualizerPanel: previousPreset() called");
    });
    controlLayout->addWidget(prevPresetButton_);
    
    presetLabel_ = new QLabel("No preset");
    presetLabel_->setAlignment(Qt::AlignCenter);
    presetLabel_->setStyleSheet("color: #00ff88; font-weight: bold;");
    controlLayout->addWidget(presetLabel_, 1);
    
    nextPresetButton_ = new QPushButton("▶");
    nextPresetButton_->setFixedSize(28, 28);
    nextPresetButton_->setToolTip("Next preset");
    connect(nextPresetButton_, &QPushButton::clicked, this, [this] {
        LOG_DEBUG("VisualizerPanel: Next preset button clicked");
        if (!visualizerWindow_) {
            LOG_ERROR("VisualizerPanel: visualizerWindow_ is null!");
            return;
        }
        auto& pm = visualizerWindow_->projectM();
        LOG_DEBUG("VisualizerPanel: projectM() returned, checking state...");
        LOG_DEBUG("VisualizerPanel: isPresetLocked = {}", pm.isPresetLocked());
        LOG_DEBUG("VisualizerPanel: presets().count() = {}", pm.presets().count());
        LOG_DEBUG("VisualizerPanel: presets().currentIndex() = {}", pm.presets().currentIndex());
        pm.nextPreset();
        LOG_DEBUG("VisualizerPanel: nextPreset() called");
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
    
    // Connect visualizer signals
    connect(visualizerWindow_, &VisualizerWindow::fpsChanged, this, &VisualizerPanel::updateFPS);
    
    LOG_INFO("VisualizerPanel: Connecting to presetChanged signal");
    visualizerWindow_->projectM().presetChanged.connect([this](const std::string& name) {
        LOG_DEBUG("VisualizerPanel: Received ProjectMBridge::presetChanged signal for: {}", name);
        // Note: Preset not loaded yet, just tracking selection
    });
    
    connect(visualizerWindow_, &VisualizerWindow::presetNameUpdated, this, [this](const QString& name) {
        LOG_DEBUG("VisualizerPanel: Received VisualizerWindow::presetNameUpdated signal for: {}", name.toStdString());
        updatePresetName(name);
    });
    
    LOG_INFO("VisualizerPanel: Connected to preset signals");
}

void VisualizerPanel::updatePresetName(const QString& name) {
    QString display = name;
    if (display.length() > 50) {
        display = display.left(47) + "...";
    }
    presetLabel_->setText(display);
    presetLabel_->setToolTip(name);
}

void VisualizerPanel::updateFPS(f32 fps) {
    fpsLabel_->setText(QString("%1 FPS").arg(static_cast<int>(fps)));
}

} // namespace vc
