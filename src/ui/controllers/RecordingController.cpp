#include "RecordingController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "recorder/VideoRecorder.hpp"
#include "ui/MainWindow.hpp"
#include "ui/RecordingControls.hpp"
#include "ui/VisualizerPanel.hpp"
#include "visualizer/VisualizerWindow.hpp"

#include <QTimer>

namespace vc {

RecordingController::RecordingController(VideoRecorder* recorder,
                                         MainWindow* window)
    : QObject(nullptr), recorder_(recorder), window_(window) {
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
            window_->onStopRecording();
        }

        if (CONFIG.recording().autoRecord) {
            LOG_INFO("Auto-record enabled, starting recording for new track.");
            QTimer::singleShot(500, this, [this] {
                if (window_->audioEngine()->isPlaying()) {
                    window_->onStartRecording("");
                }
            });
        }
    });

    // Auto-stop when audio stops completely
    window_->audioEngine()->stateChanged.connect([this](PlaybackState state) {
        if (state == PlaybackState::Stopped && recorder_->isRecording()) {
            LOG_INFO("Audio stopped, stopping recording.");
            window_->onStopRecording();
        }
    });
}

} // namespace vc
