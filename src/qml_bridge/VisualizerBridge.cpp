/**
 * @file VisualizerBridge.cpp
 * @brief Implementation of VisualizerBridge
 */

#include "VisualizerBridge.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include <QQmlEngine>

namespace qml_bridge {

PresetModel::PresetModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PresetModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return presets_.size();
}

QVariant PresetModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= presets_.size()) return {};

    int row = index.row();

    switch (role) {
        case NameRole:
            return presets_[row];
        case PathRole:
            return presets_[row];
        case IsCurrentRole:
            return row == currentIdx_;
        default:
            return {};
    }
}

QHash<int, QByteArray> PresetModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IsCurrentRole, "isCurrent"}
    };
}

void PresetModel::setPresets(const QStringList& presets)
{
    beginResetModel();
    presets_ = presets;
    endResetModel();
}

void PresetModel::setCurrentIndex(int idx)
{
    if (currentIdx_ == idx) return;

    int oldIdx = currentIdx_;
    currentIdx_ = idx;

    if (oldIdx >= 0 && oldIdx < presets_.size()) {
        emit dataChanged(index(oldIdx), index(oldIdx), {IsCurrentRole});
    }
    if (currentIdx_ >= 0 && currentIdx_ < presets_.size()) {
        emit dataChanged(index(currentIdx_), index(currentIdx_), {IsCurrentRole});
    }
}

VisualizerBridge::VisualizerBridge(QObject* parent)
    : QObject(parent)
    , presetModel_(new PresetModel(this))
{
}

VisualizerBridge* VisualizerBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    auto* bridge = new VisualizerBridge(qmlEngine);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    return bridge;
}

void VisualizerBridge::setVisualizerWindow(vc::VisualizerWindow* window)
{
if (window_ != window) {
window_ = window;
emit visualizerWindowChanged();
}
}

int VisualizerBridge::fps() const
{
    return fps_;
}

void VisualizerBridge::setFps(int fps)
{
    if (fps_ == fps) return;
    fps_ = qBound(10, fps, 120);

    if (window_) {
        window_->setRenderRate(fps_);
    }

    emit fpsChanged();
}

bool VisualizerBridge::fullscreen() const
{
    return fullscreen_;
}

void VisualizerBridge::setFullscreen(bool fullscreen)
{
    if (fullscreen_ == fullscreen) return;
    fullscreen_ = fullscreen;

    if (window_) {
        if (fullscreen_) {
            window_->showFullScreen();
        } else {
            window_->showNormal();
        }
    }

    emit fullscreenChanged();
}

bool VisualizerBridge::presetLocked() const
{
    return presetLocked_;
}

void VisualizerBridge::setPresetLocked(bool locked)
{
    if (presetLocked_ == locked) return;
    presetLocked_ = locked;

    if (window_) {
        window_->lockPreset(presetLocked_);
    }

    emit presetLockedChanged();
}

int VisualizerBridge::currentPresetIndex() const
{
    return currentPresetIndex_;
}

QString VisualizerBridge::currentPresetName() const
{
    if (currentPresetIndex_ >= 0 && currentPresetIndex_ < presetModel_->rowCount()) {
        return presetModel_->data(presetModel_->index(currentPresetIndex_), PresetModel::NameRole).toString();
    }
    return {};
}

QObject* VisualizerBridge::presetModel() const
{
return presetModel_;
}

QWindow* VisualizerBridge::visualizerWindow() const
{
return window_;
}

void VisualizerBridge::nextPreset()
{
    if (window_) {
        window_->nextPreset();
        emit currentPresetChanged();
    }
}

void VisualizerBridge::previousPreset()
{
    if (window_) {
        window_->previousPreset();
        emit currentPresetChanged();
    }
}

void VisualizerBridge::randomPreset()
{
    if (window_) {
        window_->randomPreset();
        emit currentPresetChanged();
    }
}

void VisualizerBridge::selectPreset(int index)
{
    if (!window_) return;
    
    auto& presets = window_->projectM().presets();
    auto& allPresets = presets.allPresets();
    if (index >= 0 && index < static_cast<int>(allPresets.size())) {
        const auto& preset = allPresets[index];
        window_->projectM().engine().loadPreset(preset.path.string(), false);
        currentPresetIndex_ = index;
        presetModel_->setCurrentIndex(index);
        emit currentPresetChanged();
    }
}

void VisualizerBridge::selectPresetByName(const QString& name)
{
    if (!window_) return;
    
    auto& presets = window_->projectM().presets();
    auto results = presets.search(name.toStdString());
    if (!results.empty()) {
        window_->projectM().engine().loadPreset(results[0]->path.string(), false);
        emit currentPresetChanged();
    }
}

void VisualizerBridge::searchPresets(const QString& query)
{
    Q_UNUSED(query)
}

} // namespace qml_bridge
