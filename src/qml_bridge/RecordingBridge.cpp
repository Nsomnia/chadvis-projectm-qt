#include "RecordingBridge.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/VisualizerWindow.hpp"
#include <QString>

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;
vc::VisualizerWindow* RecordingBridge::s_visualizer = nullptr;
vc::LyricsSync* RecordingBridge::s_lyricsSync = nullptr;

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

void RecordingBridge::setLyricsSync(vc::LyricsSync* sync) {
    // A raw pointer, deliberately not a QPointer: the same lifetime the rest of
    // the bridge set uses, and the same contract -- Application owns the
    // LyricsSync (Application.cpp:429) and outlives every bridge. LyricsBridge
    // stores its own the same way.
    s_lyricsSync = sync;
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

    // Where the karaoke subtitle document comes from, and why it is here.
    //
    // This bridge is the only layer that holds both ends: it already owns the
    // recorder (setRecorder) and now the LyricsSync (setLyricsSync), and it is
    // the only production caller of VideoRecorder::start at all. So it is the
    // only place that can hand the recorder a document without either inventing
    // a second start path or making the recorder reach upward.
    //
    // The two alternatives were checked, not assumed:
    //
    //   * Application computing it. Not available. Application never calls
    //     VideoRecorder::start; the sole production caller is this function.
    //     Routing it through Application would mean moving the settings build,
    //     the container override and the visualizer hand-off out of here, i.e.
    //     relocating the whole start sequence to reach one extra argument.
    //   * The recorder calling LyricsSync itself. Rejected. The recorder's whole
    //     relationship to ASS is "here are some bytes": no header under
    //     src/recorder/ includes anything from src/lyrics/, and the one
    //     include-level edge that does exist is VideoRecorderFFmpeg.cpp's call
    //     into LyricsExport::splitAssStream, which is a deliberate one-function
    //     edge and is not widened here. This bridge depends on src/lyrics/
    //     already -- LyricsBridge.hpp includes lyrics/LyricsData.hpp -- so adding
    //     one more is the layer's existing convention, not a new one.
    //
    // Read on the GUI thread, which is where LyricsSync's only writer runs
    // (SunoController, and its own QTimer). LyricsSync is documented as
    // deliberately lock-free single-threaded state (LyricsSync.hpp:85-88), so
    // reading it from the thread that drives it is the only correct option, and
    // startRecording is a Q_INVOKABLE, i.e. always on that thread.
    //
    // `subtitle` is a std::string, so it owns its bytes for the whole of
    // VideoRecorder::start. The parameter is a view, and the view's only reader
    // is ffmpeg_.init, which runs synchronously on this thread and copies before
    // it returns.
    const std::string subtitle = karaokeDocument();

    const auto result = s_recorder->start(settings, subtitle);
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

std::string RecordingBridge::karaokeDocument() const
{
    // The null checks mirror LyricsBridge::assDocument's exactly, including the
    // reason: Application only creates the LyricsSync on the non-headless path
    // (Application.cpp:381,429), so a null here is a real state, not a
    // defensive fiction.
    if (!s_lyricsSync || !s_lyricsSync->hasLyrics()) {
        return {};
    }
    // Header-only documents are impossible from here -- hasLyrics() is
    // !lyrics_.empty() -- so the muxer always gets either cues or nothing, and
    // "nothing" is the empty string rather than a document it would have to
    // refuse.
    return vc::LyricsExport::toAssDocument(s_lyricsSync->getLyrics());
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
