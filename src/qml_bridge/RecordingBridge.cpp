#include "RecordingBridge.hpp"
#include "core/Logger.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include <QQmlEngine>
#include <QString>

namespace {

QString formatDuration(vc::Duration duration)
{
    const auto totalSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds % 3600) / 60;
    const auto seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar('0'));
}

QString formatBytes(vc::u64 bytes)
{
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;

    if (bytes >= static_cast<vc::u64>(gib)) {
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / gib, 0, 'f', 1);
    }
    if (bytes >= static_cast<vc::u64>(mib)) {
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / mib, 0, 'f', 1);
    }
    if (bytes >= static_cast<vc::u64>(kib)) {
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / kib, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;
RecordingBridge* RecordingBridge::s_instance = nullptr;

RecordingBridge::RecordingBridge(QObject* parent) : QObject(parent) {
    s_instance = this;
    if (s_recorder) {
        connectRecorderSignals();
        cachedStats_ = s_recorder->stats();
    }
}

QObject* RecordingBridge::create(QQmlEngine*, QJSEngine*) {
    return new RecordingBridge();
}

void RecordingBridge::setRecorder(vc::VideoRecorder* recorder) {
    s_recorder = recorder;
    if (s_instance) {
        s_instance->connectRecorderSignals();
        if (s_recorder) {
            s_instance->cachedStats_ = s_recorder->stats();
        }
    }
}

void RecordingBridge::connectRecorderSignals()
{
    if (!s_recorder || !s_instance) {
        return;
    }

    s_recorder->stateChanged.connect([s = s_instance](vc::RecordingState state) {
        if (s) {
            s->onStateChanged(state);
        }
    });
    s_recorder->statsUpdated.connect([s = s_instance](const vc::RecordingStats& stats) {
        if (s) {
            s->onStatsUpdated(stats);
        }
    });
    s_recorder->error.connect([s = s_instance](const std::string& msg) {
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

QString RecordingBridge::recordingTime() const { return formatDuration(cachedStats_.elapsed); }
int RecordingBridge::framesWritten() const { return static_cast<int>(cachedStats_.framesWritten); }
QString RecordingBridge::fileSize() const { return formatBytes(cachedStats_.bytesWritten); }
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
