#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <string>
#include "QmlSingletonBridge.hpp"
#include "recorder/VideoRecorderCore.hpp"
#include "util/JThread.hpp"

namespace vc {
class LyricsSync;
class VideoRecorder;
class VisualizerWindow;
}

namespace qml_bridge {

class RecordingBridge : public QObject,
                        public QmlSingletonBridge<RecordingBridge> {

// The CRTP mixin constructs this singleton via its private
// constructor; grant only the exact instantiation access.
friend class QmlSingletonBridge<RecordingBridge>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY recordingStateChanged)
    Q_PROPERTY(QString recordingTime READ recordingTime NOTIFY statsChanged)
    Q_PROPERTY(int framesWritten READ framesWritten NOTIFY statsChanged)
    Q_PROPERTY(QString fileSize READ fileSize NOTIFY statsChanged)
    Q_PROPERTY(QString encodeFps READ encodeFps NOTIFY statsChanged)
    Q_PROPERTY(int bufferHealth READ bufferHealth NOTIFY statsChanged)
    // Video codec has no SettingsBridge equivalent; keep this recording-owned
    // control config-backed so the RecordingPanel can bind it without adding
    // another settings surface.
    Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY videoCodecChanged)

    // ── Karaoke burn-in (optional post-pass over a finished file) ────────────
    //
    // Five properties for one state machine, because the state has to be
    // *described* rather than merely blocked. An optional dependency that only
    // shows up as a greyed-out button is indistinguishable from a broken one, so
    // every one of these answers a question the panel has to put on screen: is a
    // pass running, what will it read, what will it write, and if not, why not.
    // The "why not" is one property rather than four booleans so the panel cannot
    // render a state that contradicts another -- see the AGENTS.md note on one
    // error string reaching several consumers. It also carries the
    // no-libavfilter reason verbatim, which is why there is no separate
    // "unavailable" property: on such a build it *is* the blocked reason, and a
    // second property that could disagree with it would be a second fact.
    //
    // All five describe the *default* job -- the last recording plus the sidecar
    // beside it. burnInSubtitles() resolves that same job through one shared
    // function, so what the panel promises and what the pass does cannot disagree.
    Q_PROPERTY(bool burnInRunning READ burnInRunning NOTIFY burnInStateChanged)
    Q_PROPERTY(QString burnInInputFile READ burnInInputFile NOTIFY burnInStateChanged)
    Q_PROPERTY(QString burnInSubtitleFile READ burnInSubtitleFile NOTIFY burnInStateChanged)
    Q_PROPERTY(QString burnInOutputFile READ burnInOutputFile NOTIFY burnInStateChanged)
    Q_PROPERTY(QString burnInBlockedReason READ burnInBlockedReason NOTIFY burnInStateChanged)

public:
    explicit RecordingBridge(QObject* parent = nullptr);
    static void setRecorder(vc::VideoRecorder* recorder);
    static void setVisualizer(vc::VisualizerWindow* visualizer);
    static void setLyricsSync(vc::LyricsSync* sync);

    bool isRecording() const;
    QString currentFile() const;
    QString recordingTime() const;
    int framesWritten() const;
    QString fileSize() const;
    QString encodeFps() const;
    int bufferHealth() const;
    QString videoCodec() const;
    void setVideoCodec(const QString& codec);

    /// Why this build cannot burn in at all, or empty when it can.
    ///
    /// Not a Q_PROPERTY: `burnInBlockedReason` already carries it verbatim on
    /// such a build, and a second property that could disagree with the first
    /// would be a second fact about one condition.
    QString burnInUnavailableReason() const;
    bool burnInRunning() const;
    QString burnInInputFile() const;
    QString burnInSubtitleFile() const;
    QString burnInOutputFile() const;
    /// Empty when the pass can run, otherwise the one sentence to put on screen.
    QString burnInBlockedReason() const;

public slots:
    Q_INVOKABLE void startRecording(const QString& outputPath = {});
    Q_INVOKABLE void stopRecording();
    /// Re-notify the six burn-in properties above without changing anything.
    ///
    /// The blocked reason is derived from the filesystem (does the sidecar
    /// exist? does the output already?), and QML cannot observe the filesystem.
    /// The panel calls this after writing the sidecar through LyricsBridge, so
    /// the button enables from the same event that created the file rather than
    /// on the next unrelated repaint.
    Q_INVOKABLE void refreshBurnInState();
    /// Burn the karaoke subtitles into a finished recording, writing a new file.
    ///
    /// `inputPath` empty means "the last recording", the same convention
    /// startRecording's `outputPath` uses, so the panel does not have to thread
    /// a path around: this bridge already holds the only candidate.
    ///
    /// Returns void and reports through `burnInFinished` / `recordingError`,
    /// which is what startRecording and stopRecording do. There is deliberately
    /// no return value: the pass is asynchronous (a full re-encode of a long
    /// recording takes minutes, so it cannot run on the GUI thread), so a
    /// synchronous bool would be reporting whether the job was *accepted*, not
    /// whether it worked.
    Q_INVOKABLE void burnInSubtitles(const QString& inputPath = {});

signals:
    void recordingStateChanged();
    void statsChanged();
    void videoCodecChanged();
    void recordingError(const QString& message);
    void burnInStateChanged();
    /// The burned copy is complete and written to `outputPath`.
    void burnInFinished(const QString& outputPath);

private:
    void connectRecorderSignals();
    void onStateChanged(vc::RecordingState state);
    void onStatsUpdated(const vc::RecordingStats& stats);
    void onError(const std::string& msg);

    /// The karaoke subtitle document for the recording about to start, or empty.
    ///
    /// See the comment at the call site in the .cpp for why this bridge, and not
    /// the recorder and not Application, is the layer that owns that decision.
    /// Kept out of the header's public surface deliberately: nothing in QML has
    /// any business choosing or inspecting a subtitle document.
    std::string karaokeDocument() const;

    /// The post-pass worker. An instance member rather than a static so the
    /// destructor can join it, which is what makes the raw `this` the worker
    /// captures safe: burnInSubtitles() takes no stop token, so a pass in
    /// flight at shutdown is waited out rather than left half-written on disk.
    /// A slow quit is recoverable; a truncated output file that looks like a
    /// render is not.
    vc::JThread burnInWorker_;

    /// Whether a pass is in flight. Static because it describes the *recorder's*
    /// post-pass rather than this QObject, and because the QmlSingletonBridge
    /// policy is NewInstancePerCall -- a per-instance flag would answer for one
    /// instance while the worker finished on another. Same reason
    /// s_recorder/s_visualizer/s_lyricsSync are static.
    static bool s_burnInRunning;

    static vc::VideoRecorder* s_recorder;
    static vc::VisualizerWindow* s_visualizer;
    static vc::LyricsSync* s_lyricsSync;
    vc::RecordingStats cachedStats_;
};

} // namespace qml_bridge
