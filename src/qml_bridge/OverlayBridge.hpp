/**
 * @file OverlayBridge.hpp
 * @brief QML bridge for managing text overlays
 *
 * @version 1.0.0 - 2026-04-20
 */

#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

namespace qml_bridge {

class OverlayBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList overlays READ overlays WRITE setOverlays NOTIFY overlaysChanged)

public:
    explicit OverlayBridge(QObject* parent = nullptr);
    ~OverlayBridge() override = default;

    static OverlayBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    QVariantList overlays() const;
    void setOverlays(const QVariantList& overlays);

    Q_INVOKABLE void addOverlay(const QString& text);
    Q_INVOKABLE void removeOverlay(int index);
    Q_INVOKABLE void updateOverlay(int index, const QVariantMap& data);

signals:
    void overlaysChanged();

private:
    void saveOverlays();
    void loadOverlays();
    QString getSettingsPath() const;

    static OverlayBridge* s_instance;
    QVariantList overlays_;
};

} // namespace qml_bridge
