#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantMap>
#include <QWindow>
#include "QmlSingletonBridge.hpp"

namespace vc {
class VisualizerWindow;
}

namespace qml_bridge {

/// Bridge between QML and the native VisualizerWindow.
/// Exposes the QWindow for embedding via WindowContainer, plus
/// preset navigation and status queries.
class VisualizerBridge : public QObject,
                         public QmlSingletonBridge<VisualizerBridge> {
Q_OBJECT
QML_ELEMENT
QML_SINGLETON

Q_PROPERTY(QWindow* visualizerWindow READ visualizerWindow NOTIFY visualizerWindowChanged)
Q_PROPERTY(bool active READ active NOTIFY activeChanged)
Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY presetChanged)
Q_PROPERTY(int fps READ fps NOTIFY statsChanged)

public:
explicit VisualizerBridge(QObject* parent = nullptr);
static void setVisualizerEngine(vc::VisualizerWindow* engine);

QWindow* visualizerWindow() const;
bool active() const;
QString currentPreset() const;
int fps() const;

public slots:
Q_INVOKABLE void nextPreset();
Q_INVOKABLE void previousPreset();
Q_INVOKABLE void toggleActive();

signals:
void visualizerWindowChanged();
void activeChanged();
void presetChanged();
void statsChanged();

private:
static vc::VisualizerWindow* s_engine;
};

} // namespace qml_bridge
