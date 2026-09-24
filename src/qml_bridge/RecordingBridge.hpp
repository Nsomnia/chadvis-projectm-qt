#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include "recorder/VideoRecorderCore.hpp"
#include "QmlSingletonBridge.hpp"

namespace vc {
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

public:
    explicit RecordingBridge(QObject* parent = nullptr);
    static void setRecorder(vc::VideoRecorder* recorder);
    static void setVisualizer(vc::VisualizerWindow* visualizer);

    bool isRecording() const;
    QString currentFile() const;
    QString recordingTime() const;
    int framesWritten() const;
    QString fileSize() const;
    QString encodeFps() const;
    int bufferHealth() const;
    QString videoCodec() const;
    void setVideoCodec(const QString& codec);

public slots:
    Q_INVOKABLE void startRecording(const QString& outputPath = {});
    Q_INVOKABLE void stopRecording();

signals:
    void recordingStateChanged();
    void statsChanged();
    void videoCodecChanged();
    void recordingError(const QString& message);

private:
    void connectRecorderSignals();
    void onStateChanged(vc::RecordingState state);
    void onStatsUpdated(const vc::RecordingStats& stats);
    void onError(const std::string& msg);

    static vc::VideoRecorder* s_recorder;
    static vc::VisualizerWindow* s_visualizer;
    vc::RecordingStats cachedStats_;
};

} // namespace qml_bridge
