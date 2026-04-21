#include "VisualizerBridge.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::VisualizerWindow* VisualizerBridge::s_engine = nullptr;
VisualizerBridge* VisualizerBridge::s_instance = nullptr;

VisualizerBridge::VisualizerBridge(QObject* parent) : QObject(parent) {
    s_instance = this;
}

QObject* VisualizerBridge::create(QQmlEngine*, QJSEngine*) {
    return new VisualizerBridge();
}

void VisualizerBridge::setVisualizerEngine(vc::VisualizerWindow* engine) {
    s_engine = engine;
}

bool VisualizerBridge::active() const { return true; }
QString VisualizerBridge::currentPreset() const { return "Default"; }
int VisualizerBridge::fps() const { return 60; }

void VisualizerBridge::nextPreset() {}
void VisualizerBridge::previousPreset() {}
void VisualizerBridge::toggleActive() {}

} // namespace qml_bridge
