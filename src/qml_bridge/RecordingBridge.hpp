#pragma once
#include <QObject>
#include <QtQml/qqml.h>

namespace vc {
class VideoRecorder;
}

namespace qml_bridge {

class RecordingBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY recordingStateChanged)

public:
    explicit RecordingBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setRecorder(vc::VideoRecorder* recorder);

    bool isRecording() const;
    QString currentFile() const;

public slots:
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void recordingStateChanged();

private:
    static vc::VideoRecorder* s_recorder;
    static RecordingBridge* s_instance;
};

} // namespace qml_bridge
