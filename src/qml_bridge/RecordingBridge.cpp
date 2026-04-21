#include "RecordingBridge.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;
RecordingBridge* RecordingBridge::s_instance = nullptr;

RecordingBridge::RecordingBridge(QObject* parent) : QObject(parent) {
    s_instance = this;
}

QObject* RecordingBridge::create(QQmlEngine*, QJSEngine*) {
    return new RecordingBridge();
}

void RecordingBridge::setRecorder(vc::VideoRecorder* recorder) {
    s_recorder = recorder;
}

bool RecordingBridge::isRecording() const { return false; }
QString RecordingBridge::currentFile() const { return ""; }

void RecordingBridge::startRecording() {}
void RecordingBridge::stopRecording() {}

} // namespace qml_bridge
