#include "VisualizerController.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PresetBrowser.hpp"
#include "ui/VisualizerPanel.hpp"
#include "visualizer/projectm/Bridge.hpp"
#include "visualizer/VisualizerWindow.hpp"

namespace vc {

VisualizerController::VisualizerController(pm::Bridge* bridge,
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
    connect(panel_, &VisualizerPanel::lockPresetToggled, [this](bool locked) {
        bridge_->lockPreset(locked);
    });

    bridge_->presetChanged.connect([this](const std::string& name) {
        LOG_DEBUG("VisualizerController: Preset changed to {}", name);
    });

    bridge_->presetLoading.connect([this](bool loading) {
    });
}

} // namespace vc
