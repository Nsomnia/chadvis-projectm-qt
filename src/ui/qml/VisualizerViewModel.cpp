#include "ui/qml/VisualizerViewModel.hpp"

#include "visualizer/VisualizerWindow.hpp"

namespace vc::ui::qml {

VisualizerViewModel::VisualizerViewModel(VisualizerWindow* visualizer, QObject* parent)
    : QObject(parent)
    , visualizer_(visualizer) {
    if (!visualizer_) {
        return;
    }

    connect(visualizer_, &VisualizerWindow::presetNameUpdated, this, [this](const QString& name) {
        if (presetName_ == name) {
            return;
        }

        presetName_ = name;
        emit presetNameChanged();
    });
}

void VisualizerViewModel::nextPreset() {
    if (visualizer_) {
        visualizer_->nextPreset();
    }
}

void VisualizerViewModel::previousPreset() {
    if (visualizer_) {
        visualizer_->previousPreset();
    }
}

void VisualizerViewModel::randomPreset() {
    if (visualizer_) {
        visualizer_->randomPreset();
    }
}

void VisualizerViewModel::togglePresetLock() {
    setPresetLocked(!presetLocked_);
}

void VisualizerViewModel::setPresetLocked(bool locked) {
    if (presetLocked_ == locked) {
        return;
    }

    presetLocked_ = locked;
    if (visualizer_) {
        visualizer_->lockPreset(locked);
    }

    emit presetLockedChanged();
}

} // namespace vc::ui::qml
