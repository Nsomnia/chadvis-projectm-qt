#include "VisualizerController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PresetBrowser.hpp"
#include "ui/VisualizerPanel.hpp"
#include "visualizer/ProjectMBridge.hpp"
#include "visualizer/VisualizerWindow.hpp"

namespace vc {

VisualizerController::VisualizerController(ProjectMBridge* bridge,
                                           MainWindow* window)
    : QObject(nullptr), bridge_(bridge), window_(window) {
}

void VisualizerController::setupUI(VisualizerPanel* panel,
                                   PresetBrowser* browser) {
    panel_ = panel;
    browser_ = browser;

    browser_->setPresetManager(&bridge_->presets());
}

void VisualizerController::connectSignals() {
    // Visualizer panel signals
    connect(panel_, &VisualizerPanel::lockPresetToggled, [this](bool locked) {
        bridge_->lockPreset(locked);
    });

    // bridge signals
    bridge_->presetChanged.connect([this](const std::string& name) {
        LOG_DEBUG("VisualizerController: Preset changed to {}", name);
    });

    bridge_->presetLoading.connect([this](bool loading) {
        // Handled in VisualizerWindow
    });
}

} // namespace vc
