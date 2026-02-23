#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "recorder/VideoRecorder.hpp"

namespace vc {
class VideoRecorder;
}

namespace vc::ui::qml {

class RecordingViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    RecordingViewModel(VideoRecorder* recorder,
                       std::function<void()> startRecording,
                       std::function<void()> stopRecording,
                       QObject* parent = nullptr);
    ~RecordingViewModel() override;

    bool recording() const;
    QString statusMessage() const { return statusMessage_; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();

signals:
    void recordingChanged();
    void statusMessageChanged();

private:
    void refreshState();

    VideoRecorder* recorder_{nullptr};
    std::function<void()> startRecording_;
    std::function<void()> stopRecording_;
    QString statusMessage_{"Recording idle"};

    std::optional<Signal<RecordingState>::SlotId> stateChangedConnection_;
};

} // namespace vc::ui::qml
