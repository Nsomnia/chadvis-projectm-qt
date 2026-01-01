#include "RecordingController.hpp"
#include "core/Logger.hpp"
#include "recorder/VideoRecorder.hpp"
#include "ui/MainWindow.hpp"
#include "ui/RecordingControls.hpp"

namespace vc {

RecordingController::RecordingController(VideoRecorder* recorder,
                                         MainWindow* parent)
    : QObject(parent), recorder_(recorder), window_(parent) {
}

void RecordingController::setupUI(RecordingControls* controls) {
    controls_ = controls;
    controls_->setVideoRecorder(recorder_);
}

void RecordingController::connectSignals() {
    connect(controls_,
            &RecordingControls::startRecordingRequested,
            [this](const QString& path) { window_->onStartRecording(path); });

    connect(controls_, &RecordingControls::stopRecordingRequested, [this] {
        window_->onStopRecording();
    });
}

} // namespace vc
