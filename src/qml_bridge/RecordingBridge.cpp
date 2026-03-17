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
    // Signals will be connected when VideoRecorder supports them
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
    isRecording_ = true;
    status_ = "Recording";

    emit recordingChanged();
    emit statusChanged();

    return true;
}

void RecordingBridge::stopRecording()
{
    if (!s_recorder || !isRecording_) return;

    s_recorder->stop();
    isRecording_ = false;
    status_ = "Idle";

    emit recordingChanged();
    emit statusChanged();
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
