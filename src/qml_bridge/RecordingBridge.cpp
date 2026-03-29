/**
 * @file RecordingBridge.cpp
 * @brief Implementation of RecordingBridge QML singleton
 */

#include "RecordingBridge.hpp"
#include "recorder/VideoRecorder.hpp"
#include "recorder/EncoderSettings.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;
RecordingBridge* RecordingBridge::s_instance = nullptr;

RecordingBridge::RecordingBridge(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

RecordingBridge* RecordingBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    auto* bridge = new RecordingBridge(qmlEngine);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    return bridge;
}

void RecordingBridge::setVideoRecorder(vc::VideoRecorder* recorder)
{
    s_recorder = recorder;
}

void RecordingBridge::connectSignals()
{
    if (!s_recorder || !s_instance) return;

    // Bridge vc::Signal → Qt signal for thread-safe QML updates
s_recorder->stateChanged.connect([instance = s_instance](vc::RecordingState state) {
QMetaObject::invokeMethod(instance, [instance, state] {
switch (state) {
case vc::RecordingState::Recording:
instance->isRecording_ = true;
instance->status_ = "Recording";
break;
case vc::RecordingState::Stopping:
instance->status_ = "Stopping...";
break;
case vc::RecordingState::Finalizing:
instance->isRecording_ = false;
instance->status_ = "Finalizing...";
break;
case vc::RecordingState::Stopped:
instance->isRecording_ = false;
instance->status_ = "Idle";
break;
case vc::RecordingState::Error:
instance->status_ = "Error";
break;
default:
break;
}
emit instance->recordingChanged();
emit instance->statusChanged();
});
});

    s_recorder->statsUpdated.connect([instance = s_instance](const vc::RecordingStats& stats) {
        QMetaObject::invokeMethod(instance, [instance, stats] {
            instance->duration_ = std::chrono::duration<double>(stats.elapsed).count();
            emit instance->durationChanged();
        });
    });

    s_recorder->error.connect([instance = s_instance](std::string msg) {
        QMetaObject::invokeMethod(instance, [instance, msg = QString::fromStdString(msg)] {
            emit instance->errorOccurred(msg);
        });
    });
}

bool RecordingBridge::isRecording() const
{
    return s_recorder ? s_recorder->isRecording() : false;
}

QString RecordingBridge::status() const
{
    if (!s_recorder) return "No Recorder";

    if (s_recorder->isRecording()) {
        return "Recording";
    }
    return "Idle";
}

qreal RecordingBridge::duration() const
{
    return duration_;
}

QString RecordingBridge::outputPath() const
{
    return outputPath_;
}

QStringList RecordingBridge::availableCodecs() const
{
    return {"libx264", "libx265", "libvpx", "nvenc", "vaapi"};
}

bool RecordingBridge::startRecording(const QString& outputPath)
{
if (!s_recorder) return false;

QString path = outputPath;
if (path.isEmpty()) {
auto settings = vc::EncoderSettings::fromConfig();
path = QString::fromStdString(settings.outputPath.string());
}

outputPath_ = path;

auto result = s_recorder->start(path.toStdString());
if (!result) {
emit errorOccurred(QString::fromStdString(result.error().message));
return false;
}

return true;
}

void RecordingBridge::stopRecording()
{
if (!s_recorder || !isRecording_) return;

s_recorder->stop();
}

void RecordingBridge::setCodec(const QString& codec)
{
    // Update encoder settings
    Q_UNUSED(codec)
}

void RecordingBridge::setQuality(int crf)
{
    Q_UNUSED(crf)
}

void RecordingBridge::onRecordingStarted()
{
    isRecording_ = true;
    status_ = "Recording";
    emit recordingChanged();
    emit statusChanged();
}

void RecordingBridge::onRecordingStopped()
{
    isRecording_ = false;
    status_ = "Idle";
    emit recordingChanged();
    emit statusChanged();
}

void RecordingBridge::onError(const QString& message)
{
    status_ = "Error";
    emit statusChanged();
    emit errorOccurred(message);
}

} // namespace qml_bridge
