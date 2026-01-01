#include "RecordingController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"
#include "recorder/VideoRecorder.hpp"
#include "ui/MainWindow.hpp"
#include "ui/RecordingControls.hpp"
#include "ui/VisualizerPanel.hpp"
#include "visualizer/VisualizerWindow.hpp"

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

    // Connect visualizer frames to recorder
    auto* visualizer = window_->visualizerPanel()->visualizer();
    connect(
            visualizer,
            &VisualizerWindow::frameCaptured,
            this,
            [this](std::vector<u8> data, u32 w, u32 h, i64 ts) {
                if (recorder_->isRecording()) {
                    recorder_->submitVideoFrame(std::move(data), w, h, ts);
                }
            },
            Qt::DirectConnection);

    // Connect audio samples to recorder
    window_->audioEngine()->pcmReceived.connect(
            [this](const std::vector<f32>& pcm,
                   u32 frames,
                   u32 channels,
                   u32 sampleRate) {
                if (recorder_->isRecording()) {
                    recorder_->submitAudioSamples(
                            pcm.data(), frames, channels, sampleRate);
                }
            });

    // Auto-stop recording on track change
    window_->audioEngine()->trackChanged.connect([this] {
        if (recorder_->isRecording()) {
            LOG_INFO("Track changed, stopping recording.");
            QMetaObject::invokeMethod(window_, &MainWindow::onStopRecording);
        }
    });
}

} // namespace vc
