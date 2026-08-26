#include "RecordingBridge.hpp"
#include "core/Logger.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/FileUtils.hpp"
#include <QString>

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;

RecordingBridge::RecordingBridge(QObject* parent) : QObject(parent) {
    setInstance(this);
    if (s_recorder) {
        connectRecorderSignals();
        cachedStats_ = s_recorder->stats();
    }
}

void RecordingBridge::setRecorder(vc::VideoRecorder* recorder) {
    s_recorder = recorder;
    if (auto* bridge = instance()) {
        bridge->connectRecorderSignals();
        if (s_recorder) {
            bridge->cachedStats_ = s_recorder->stats();
        }
    }
}

void RecordingBridge::connectRecorderSignals()
{
    auto* bridge = instance();
    if (!s_recorder || !bridge) {
        return;
    }

    s_recorder->stateChanged.connect([s = bridge](vc::RecordingState state) {
        if (s) {
            s->onStateChanged(state);
        }
    });
    s_recorder->statsUpdated.connect([s = bridge](const vc::RecordingStats& stats) {
        if (s) {
            s->onStatsUpdated(stats);
        }
    });
    s_recorder->error.connect([s = bridge](const std::string& msg) {
        if (s) {
            s->onError(msg);
        }
    });
}

bool RecordingBridge::isRecording() const { return s_recorder ? s_recorder->isRecording() : false; }
QString RecordingBridge::currentFile() const
{
    if (s_recorder) {
        return QString::fromStdString(s_recorder->stats().currentFile);
    }
    return QString::fromStdString(cachedStats_.currentFile);
}

QString RecordingBridge::recordingTime() const { return vc::file::formatDurationQString(cachedStats_.elapsed.count()); }
int RecordingBridge::framesWritten() const { return static_cast<int>(cachedStats_.framesWritten); }
QString RecordingBridge::fileSize() const { return vc::file::humanSizeQString(cachedStats_.bytesWritten); }
QString RecordingBridge::encodeFps() const { return QString::number(cachedStats_.encodingFps, 'f', 1); }
int RecordingBridge::bufferHealth() const
{
    const auto totalFrames = cachedStats_.framesWritten + cachedStats_.framesDropped;
    if (!s_recorder || !s_recorder->isRecording() || totalFrames == 0) {
        return 100;
    }
    return static_cast<int>((100 * cachedStats_.framesWritten) / totalFrames);
}

void RecordingBridge::startRecording(const QString& outputPath)
{
    if (!s_recorder) {
        emit recordingError(QStringLiteral("Recording backend is not available"));
        return;
    }

    if (!outputPath.isEmpty()) {
        const auto result = s_recorder->start(vc::fs::path(outputPath.toStdString()));
        if (!result) {
            LOG_ERROR("RecordingBridge: startRecording failed: {}", result.error().message);
            emit recordingError(QString::fromStdString(result.error().message));
        }
        return;
    }

    auto settings = vc::EncoderSettings::fromConfig();
    const auto result = s_recorder->start(settings);
    if (!result) {
        LOG_ERROR("RecordingBridge: startRecording failed: {}", result.error().message);
        emit recordingError(QString::fromStdString(result.error().message));
    }
}

void RecordingBridge::stopRecording()
{
    if (!s_recorder) {
        return;
    }

    const auto result = s_recorder->stop();
    if (!result) {
        LOG_ERROR("RecordingBridge: stopRecording failed: {}", result.error().message);
        emit recordingError(QString::fromStdString(result.error().message));
    }
}

void RecordingBridge::onStateChanged(vc::RecordingState state)
{
    if (s_recorder) {
        cachedStats_ = s_recorder->stats();
    }
    Q_UNUSED(state)
    emit recordingStateChanged();
    emit statsChanged();
}

void RecordingBridge::onStatsUpdated(const vc::RecordingStats& stats)
{
    cachedStats_ = stats;
    emit statsChanged();
    emit recordingStateChanged();
}

void RecordingBridge::onError(const std::string& msg)
{
    emit recordingError(QString::fromStdString(msg));
}

} // namespace qml_bridge
