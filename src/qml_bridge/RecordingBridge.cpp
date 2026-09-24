#include "RecordingBridge.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include <QString>

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;
vc::VisualizerWindow* RecordingBridge::s_visualizer = nullptr;

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

void RecordingBridge::setVisualizer(vc::VisualizerWindow* visualizer) {
    s_visualizer = visualizer;
}

void RecordingBridge::connectRecorderSignals()
{
    auto* bridge = instance();
    if (!s_recorder || !bridge) {
        return;
    }

    s_recorder->stateChanged.connect([s = bridge](vc::RecordingState state) {
        QMetaObject::invokeMethod(s, [s, state] {
            if (s) {
                s->onStateChanged(state);
            }
        }, Qt::QueuedConnection);
    });
    s_recorder->statsUpdated.connect([s = bridge](const vc::RecordingStats& stats) {
        const auto snapshot = stats;
        QMetaObject::invokeMethod(s, [s, snapshot] {
            if (s) {
                s->onStatsUpdated(snapshot);
            }
        }, Qt::QueuedConnection);
    });
    s_recorder->error.connect([s = bridge](const std::string& msg) {
        const auto message = QString::fromStdString(msg);
        QMetaObject::invokeMethod(s, [s, message] {
            if (s) {
                s->onError(message.toStdString());
            }
        }, Qt::QueuedConnection);
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

QString RecordingBridge::videoCodec() const
{
    return QString::fromStdString(vc::Config::instance().recording().video.codec);
}

void RecordingBridge::setVideoCodec(const QString& codec)
{
    if (codec.isEmpty() || codec == videoCodec())
        return;

    vc::Config::instance().recording().video.codec = codec.toStdString();
    emit videoCodecChanged();
}

void RecordingBridge::startRecording(const QString& outputPath)
{
    if (!s_recorder) {
        emit recordingError(QStringLiteral("Recording backend is not available"));
        return;
    }

    auto settings = vc::EncoderSettings::fromConfig();
    if (!outputPath.isEmpty()) {
        const auto requestedPath = vc::fs::path(outputPath.toStdString());
        if (auto container = vc::EncoderSettings::containerFromPath(requestedPath)) {
            settings.container = *container;
        }
        settings.outputPath = vc::EncoderSettings::outputPathForContainer(
            requestedPath, settings.container);
    }

    if (s_visualizer) {
        s_visualizer->setRecordingSize(settings.video.width, settings.video.height);
    }

    const auto result = s_recorder->start(settings);
    if (!result) {
        LOG_ERROR("RecordingBridge: startRecording failed: {}", result.error().message);
        emit recordingError(QString::fromStdString(result.error().message));
        return;
    }

    // The encoder is ready first; only then enable renderer-side FBO/PBO
    // capture.  VisualizerWindow defers this request if its native window has
    // not been exposed yet, so one QML action still starts both halves.
    if (s_visualizer) {
        s_visualizer->startRecording();
    } else {
        LOG_WARN("RecordingBridge: recording started without a visualizer window");
    }
}

void RecordingBridge::stopRecording()
{
    // Stop capture before finalization so no new frame is submitted while the
    // encoder is draining and closing its queues.
    if (s_visualizer) {
        s_visualizer->stopRecording();
    }
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
