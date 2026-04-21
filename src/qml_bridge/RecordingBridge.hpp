/**
 * @file RecordingBridge.hpp
 * @brief QML bridge for VideoRecorder - exposes recording control to QML
 *
 * Single-responsibility: QML interface for video recording
 *
 * @version 1.0.0
 */

#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QStringList>

namespace vc {
class VideoRecorder;
}

namespace qml_bridge {

/**
 * @brief QML bridge exposing VideoRecorder to QML
 *
 * Usage in QML:
 * @code
 * RecordingBridge.startRecording("/path/to/output.mp4")
 * RecordingBridge.stopRecording()
 * recordingStatus: RecordingBridge.status
 * @endcode
 */
class RecordingBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(qreal duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY recordingChanged)
    Q_PROPERTY(QStringList availableCodecs READ availableCodecs CONSTANT)

public:
    enum RecordingStatus {
        Idle = 0,
        Recording = 1,
        Encoding = 2,
        Error = 3
    };
    Q_ENUM(RecordingStatus)

    explicit RecordingBridge(QObject* parent = nullptr);
    ~RecordingBridge() override = default;

    static RecordingBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    static void setVideoRecorder(vc::VideoRecorder* recorder);
    static void connectSignals();

    bool isRecording() const;
    QString status() const;
    qreal duration() const;
    QString outputPath() const;
    QStringList availableCodecs() const;

public slots:
    Q_INVOKABLE bool startRecording(const QString& outputPath = QString());
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void setCodec(const QString& codec);
    Q_INVOKABLE void setQuality(int crf);

signals:
    void recordingChanged();
    void statusChanged();
    void durationChanged();
    void outputPathChanged();
    void errorOccurred(const QString& message);

private slots:
    void onRecordingStarted();
    void onRecordingStopped();
    void onError(const QString& message);

private:
    static vc::VideoRecorder* s_recorder;
    static RecordingBridge* s_instance;

    bool isRecording_{false};
    QString status_{"Idle"};
    qreal duration_{0.0};
    QString outputPath_;
};

} // namespace qml_bridge
