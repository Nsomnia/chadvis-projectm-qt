#include "ui/qml/RecordingViewModel.hpp"

namespace vc::ui::qml {

RecordingViewModel::RecordingViewModel(VideoRecorder* recorder,
                                       std::function<void()> startRecording,
                                       std::function<void()> stopRecording,
                                       QObject* parent)
    : QObject(parent)
    , recorder_(recorder)
    , startRecording_(std::move(startRecording))
    , stopRecording_(std::move(stopRecording)) {
    if (!recorder_) {
        statusMessage_ = QStringLiteral("Recorder unavailable");
        return;
    }

    stateChangedConnection_ = recorder_->stateChanged.connect([this](RecordingState) {
        refreshState();
    });

    refreshState();
}

RecordingViewModel::~RecordingViewModel() {
    if (recorder_ && stateChangedConnection_) {
        recorder_->stateChanged.disconnect(*stateChangedConnection_);
    }
}

bool RecordingViewModel::recording() const {
    return recorder_ && recorder_->isRecording();
}

void RecordingViewModel::start() {
    if (recording()) {
        return;
    }

    if (startRecording_) {
        startRecording_();
        statusMessage_ = QStringLiteral("Starting recorder...");
        emit statusMessageChanged();
    }
}

void RecordingViewModel::stop() {
    if (!recording()) {
        return;
    }

    if (stopRecording_) {
        stopRecording_();
        statusMessage_ = QStringLiteral("Stopping recorder...");
        emit statusMessageChanged();
    }
}

void RecordingViewModel::toggle() {
    if (recording()) {
        stop();
    } else {
        start();
    }
}

void RecordingViewModel::refreshState() {
    emit recordingChanged();

    QString nextStatus = QStringLiteral("Recording idle");
    if (!recorder_) {
        nextStatus = QStringLiteral("Recorder unavailable");
    } else {
        switch (recorder_->state()) {
        case RecordingState::Stopped:
            nextStatus = QStringLiteral("Recording idle");
            break;
        case RecordingState::Starting:
            nextStatus = QStringLiteral("Recorder spinning up");
            break;
        case RecordingState::Recording:
            nextStatus = QStringLiteral("Recording active");
            break;
        case RecordingState::Stopping:
            nextStatus = QStringLiteral("Flushing recording buffers");
            break;
        case RecordingState::Error:
            nextStatus = QStringLiteral("Recorder error");
            break;
        }
    }

    if (statusMessage_ != nextStatus) {
        statusMessage_ = nextStatus;
        emit statusMessageChanged();
    }
}

} // namespace vc::ui::qml
