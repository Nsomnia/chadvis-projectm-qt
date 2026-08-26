#include "OverlayBridge.hpp"
#include "core/Logger.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>
#include <QDir>

namespace qml_bridge {

OverlayBridge::OverlayBridge(QObject* parent)
    : QObject(parent)
{
    setInstance(this);
    loadOverlays();

    // Debounced auto-save: persist 2s after the last change (mirrors SettingsBridge)
    autoSaveTimer_.setSingleShot(true);
    autoSaveTimer_.setInterval(2000);
    connect(&autoSaveTimer_, &QTimer::timeout, this, [this]() { saveOverlays(); });
}

OverlayBridge::~OverlayBridge()
{
    // Explicit flush so pending edits are never lost on shutdown
    if (autoSaveTimer_.isActive()) {
        autoSaveTimer_.stop();
        saveOverlays();
    }
}

QVariantList OverlayBridge::overlays() const
{
    return overlays_;
}

void OverlayBridge::setOverlays(const QVariantList& overlays)
{
    if (overlays_ != overlays) {
        overlays_ = overlays;
        emit overlaysChanged();
        scheduleSave();
    }
}

void OverlayBridge::addOverlay(const QString& text)
{
    QVariantMap overlay;
    overlay["text"] = text;
    overlay["x"] = 0.5;
    overlay["y"] = 0.5;
    overlay["fontSize"] = 24;
    overlay["bold"] = false;
    overlay["color"] = "#FFFFFF";
    overlay["opacity"] = 1.0;
    overlay["animation"] = 0;
    
    overlays_.append(overlay);
    emit overlaysChanged();
    scheduleSave();
}

void OverlayBridge::removeOverlay(int index)
{
    if (index >= 0 && index < overlays_.size()) {
        overlays_.removeAt(index);
        emit overlaysChanged();
        scheduleSave();
    }
}

void OverlayBridge::updateOverlay(int index, const QVariantMap& data)
{
    if (index >= 0 && index < overlays_.size()) {
        overlays_[index] = data;
        emit overlaysChanged();
        scheduleSave();
    }
}

void OverlayBridge::scheduleSave()
{
    autoSaveTimer_.start(); // Restarts the timer if already running (debounce)
}

void OverlayBridge::saveOverlays()
{
    QString path = getSettingsPath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("OverlayBridge: Failed to open overlays file for writing: {}", path.toStdString());
        return;
    }

    QJsonArray root;
    for (const auto& var : overlays_) {
        root.append(QJsonObject::fromVariantMap(var.toMap()));
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
    LOG_INFO("OverlayBridge: Saved {} overlays to {}", overlays_.size(), path.toStdString());
}

void OverlayBridge::loadOverlays()
{
    QString path = getSettingsPath();
    if (!QFile::exists(path)) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR("OverlayBridge: Failed to open overlays file for reading: {}", path.toStdString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return;

    overlays_.clear();
    QJsonArray root = doc.array();
    for (const auto& val : root) {
        overlays_.append(val.toObject().toVariantMap());
    }

    emit overlaysChanged();
    LOG_INFO("OverlayBridge: Loaded {} overlays from {}", overlays_.size(), path.toStdString());
}

QString OverlayBridge::getSettingsPath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + "/overlays.json";
}

} // namespace qml_bridge
