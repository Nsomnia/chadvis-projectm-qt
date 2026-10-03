#include "RecordingBridge.hpp"
#include <QString>
#include <utility>
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"
#include "recorder/SubtitleBurnIn.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/FileUtils.hpp"
#include "visualizer/VisualizerWindow.hpp"

namespace qml_bridge {

vc::VideoRecorder* RecordingBridge::s_recorder = nullptr;
vc::VisualizerWindow* RecordingBridge::s_visualizer = nullptr;
vc::LyricsSync* RecordingBridge::s_lyricsSync = nullptr;
bool RecordingBridge::s_burnInRunning = false;

namespace {

/// One burn-in job: the three files, and why it may not run yet.
///
/// Resolved in one place so the properties QML reads and the call
/// burnInSubtitles() makes can never disagree about a filename or about whether
/// the pass is allowed to run.
struct BurnInJob {
    fs::path input;
    fs::path subtitle;
    fs::path output;
    QString blockedReason;

    [[nodiscard]] bool ready() const { return blockedReason.isEmpty(); }
};

/// Resolve the one job this bridge can offer: `inputPath`, or the last recording
/// when that is empty.
///
/// There is deliberately no way to choose the subtitle file or the output. The
/// whole design is that the document is the recording's own sidecar and the
/// output is a new derived name -- a caller who could pick both would be able to
/// ask the panel to promise one pair of files and then run another, which is the
/// exact disagreement this single resolver exists to prevent.
BurnInJob describeBurnIn(const QString& currentFilePath, const QString& inputPath = {}) {
    BurnInJob job;

    // Empty means "the last recording", the convention startRecording's
    // outputPath already uses and RecordingPanel states as "Auto-generated if
    // empty" (:132). The bridge holds the only candidate, so QML is never asked
    // to thread a path through to reach a file it just made.
    job.input = inputPath.isEmpty() ? fs::path(currentFilePath.toStdString())
                                    : fs::path(inputPath.toStdString());

    // Derived only from an input that exists, so the public getters never answer
    // ".ass" or "-burned" for a recording that was never made.
    //
    // The paths are resolved *before* the availability gate, and that is
    // deliberate: the .ass sidecar is worth writing on a build that cannot burn
    // it in -- it is the same document the MKV muxer takes as a soft track -- and
    // a user reading only "unsupported" learns nothing about which files are
    // involved. The gate still blocks the pass; it just does not hide the names.
    if (!job.input.empty()) {
        // "song-burned.mp4" rather than a counter: the name says what produced
        // it, which is the discipline burnInSubtitles() asks for when it refuses
        // to overwrite (:177-183). Deriving rather than prompting also means the
        // panel can name the file *before* the click, so nothing is ever
        // overwritten by surprise. A second pass has to be aimed elsewhere on
        // purpose.
        job.output = job.input;
        job.output.replace_extension();
        job.output += "-burned";
        job.output += job.input.extension();

        // The subtitle document is read back off disk rather than regenerated
        // from the live lyrics. That is what keeps the pass re-runnable after the
        // user has moved on to another track -- which is the entire reason a
        // post-pass exists rather than a mux-at-record-time -- and it means an
        // edit to the .ass is the thing that changes the next render, not a code
        // change. Beside the recording, by the repo's existing sidecar convention
        // (SunoDownloader derives .srt and .txt the same way:
        // SunoDownloader.cpp:452-460 and :489-492).
        job.subtitle = job.input.replace_extension(".ass");
    }

#if CHADVIS_HAS_AVFILTER
    if (const std::string unavailable = vc::burnInUnavailableReason(); !unavailable.empty()) {
        job.blockedReason = QString::fromStdString(unavailable);
        return job;
    }
#else
    job.blockedReason = kBurnInUnavailable;
    return job;
#endif

    if (job.input.empty()) {
        job.blockedReason =
                QStringLiteral("Record something first \u2014 burn-in runs on a finished file.");
        return job;
    }
    if (!fs::exists(job.input)) {
        job.blockedReason = QStringLiteral("No such recording: %1")
                                    .arg(QString::fromStdString(job.input.string()));
        return job;
    }

    // Sidecar first, output second: with both true, the missing file is the one
    // the user can actually do something about.
    if (!fs::exists(job.subtitle)) {
        job.blockedReason = QStringLiteral("No karaoke subtitles beside the recording: %1")
                                    .arg(QString::fromStdString(job.subtitle.string()));
        return job;
    }
    if (fs::exists(job.output)) {
        job.blockedReason = QStringLiteral("%1 already exists, and burn-in will not overwrite "
                                           "it \u2014 delete it first or rename the output.")
                                    .arg(QString::fromStdString(job.output.string()));
        return job;
    }
    return job;
}

} // namespace

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

// ── Karaoke burn-in: the described state ─────────────────────────────────────

// There is deliberately no `burnInSupported` getter: on a build without
// libavfilter the unsupported reason *is* the blocked reason, so a boolean would
// be a second, weaker answer to a question burnInBlockedReason already answers
// in full -- and a QML boolean nobody reads is exactly the dead surface TODO.md
// keeps finding in this bridge.
QString RecordingBridge::burnInUnavailableReason() const {
#if CHADVIS_HAS_AVFILTER
    return QString::fromStdString(vc::burnInUnavailableReason());
#else
    return kBurnInUnavailable;
#endif
}

bool RecordingBridge::burnInRunning() const { return s_burnInRunning; }

QString RecordingBridge::burnInInputFile() const {
    return QString::fromStdString(describeBurnIn(currentFile()).input.string());
}

QString RecordingBridge::burnInSubtitleFile() const {
    return QString::fromStdString(describeBurnIn(currentFile()).subtitle.string());
}

QString RecordingBridge::burnInOutputFile() const {
    return QString::fromStdString(describeBurnIn(currentFile()).output.string());
}

QString RecordingBridge::burnInBlockedReason() const {
    return describeBurnIn(currentFile()).blockedReason;
}

void RecordingBridge::refreshBurnInState() { emit burnInStateChanged(); }

// ── Karaoke burn-in: the pass ───────────────────────────────────────────────

void RecordingBridge::burnInSubtitles(const QString& inputPath) {
    const BurnInJob job = describeBurnIn(currentFile(), inputPath);

#if !CHADVIS_HAS_AVFILTER
    // Same gate describeBurnIn already reported, so the click cannot do
    // something the caption did not promise.
    Q_UNUSED(job)
    emit recordingError(kBurnInUnavailable);
#else
    if (!job.ready()) {
        emit recordingError(job.blockedReason);
        return;
    }
    if (s_burnInRunning) {
        emit recordingError(QStringLiteral("A burn-in is already running."));
        return;
    }

    vc::BurnInOptions options;
    options.inputVideo = job.input;
    options.subtitleFile = job.subtitle;
    options.outputVideo = job.output;
    // videoCodec is left empty on purpose. Empty means "re-encode with the
    // input's own codec", which is what a visual-only edit should do: forcing
    // the codec currently selected in the panel above would silently change the
    // codec of a file the user chose as a subtitle pass, and the container is
    // already fixed by the derived filename.
    //
    // crf is read from the same config the recording used, exactly as
    // videoCodec() reads its codec, so the pass does not introduce a quality
    // knob the rest of the panel does not already have -- and no new config key
    // is needed. fontsDir stays empty too: the filter's only font option is a
    // search directory, and which face renders comes from the document's
    // [V4+ Styles] row, so the honest workflow is "edit the .ass, run it again".
    options.crf = static_cast<int>(vc::Config::instance().recording().video.crf);

    s_burnInRunning = true;
    emit burnInStateChanged();

    // Off the GUI thread on purpose. A full re-encode of a long recording takes
    // minutes, and running it inline would freeze the window -- which is the
    // same "the UI looks broken" failure the in-progress state exists to avoid,
    // just arrived at from the other direction.
    //
    // BurnInOptions is captured by value and owns three paths, so nothing
    // borrowed crosses the thread boundary. The outcome is marshalled back with
    // the same queued QMetaObject::invokeMethod the recorder's own signals use
    // (see connectRecorderSignals, above), so every signal is emitted on the GUI
    // thread.
    burnInWorker_ = vc::JThread([this, options = std::move(options)]() {
        const auto result = vc::burnInSubtitles(options);
        const bool ok = result.isOk();
        const std::string message = ok ? std::string{} : result.error().message;
        const std::string output = options.outputVideo.string();

        QMetaObject::invokeMethod(
                this,
                [this, ok, message, output] {
                    s_burnInRunning = false;
                    if (ok) {
                        LOG_INFO("RecordingBridge: burned subtitles into {}", output);
                        emit burnInFinished(QString::fromStdString(output));
                    } else {
                        LOG_ERROR("RecordingBridge: burn-in failed: {}", message);
                        emit recordingError(QString::fromStdString(message));
                    }
                    emit burnInStateChanged();
                },
                Qt::QueuedConnection);
    });
#endif
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
