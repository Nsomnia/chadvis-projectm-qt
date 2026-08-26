#include "VisualizerBridge.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include "visualizer/PresetManager.hpp"
#include "core/Config.hpp"

namespace qml_bridge {

vc::VisualizerWindow* VisualizerBridge::s_engine = nullptr;

VisualizerBridge::VisualizerBridge(QObject* parent) : QObject(parent) {
	setInstance(this);
}

void VisualizerBridge::setVisualizerEngine(vc::VisualizerWindow* engine) {
	s_engine = engine;
	if (auto* bridge = instance()) {
		emit bridge->visualizerWindowChanged();
	}
}

QWindow* VisualizerBridge::visualizerWindow() const {
	return s_engine; // VisualizerWindow inherits QWindow
}

bool VisualizerBridge::active() const {
	return s_engine != nullptr;
}

QString VisualizerBridge::currentPreset() const {
	if (!s_engine) return {};
	return QString::fromStdString(s_engine->projectM().currentPresetName());
}

int VisualizerBridge::fps() const {
	if (!s_engine) return 0;
	return static_cast<int>(s_engine->actualFps());
}

void VisualizerBridge::nextPreset() {
	if (s_engine) s_engine->nextPreset();
}

void VisualizerBridge::previousPreset() {
	if (s_engine) s_engine->previousPreset();
}

void VisualizerBridge::toggleActive() {
	// Toggle visualizer on/off — for now just a no-op placeholder
	// until pause/resume is implemented in VisualizerWindow
}

} // namespace qml_bridge
