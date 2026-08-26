/**
 * @file OverlayBridge.hpp
 * @brief QML bridge for managing text overlays
 *
 * @version 1.1.0 - 2026-08-25
 */

#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include "QmlSingletonBridge.hpp"

namespace qml_bridge {

/// Ownership note: create() parents this bridge to the QML engine with explicit
/// CppOwnership (preserved historical behavior) via SingletonPolicy::CppOwnership.
class OverlayBridge : public QObject,
                      public QmlSingletonBridge<OverlayBridge, SingletonPolicy::CppOwnership> {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList overlays READ overlays WRITE setOverlays NOTIFY overlaysChanged)

public:
    explicit OverlayBridge(QObject* parent = nullptr);
    ~OverlayBridge() override;

    QVariantList overlays() const;
    void setOverlays(const QVariantList& overlays);

    Q_INVOKABLE void addOverlay(const QString& text);
    Q_INVOKABLE void removeOverlay(int index);
    Q_INVOKABLE void updateOverlay(int index, const QVariantMap& data);

signals:
    void overlaysChanged();

private:
    void scheduleSave();
    void saveOverlays();
    void loadOverlays();
    QString getSettingsPath() const;

    QVariantList overlays_;
    QTimer autoSaveTimer_;
};

} // namespace qml_bridge
