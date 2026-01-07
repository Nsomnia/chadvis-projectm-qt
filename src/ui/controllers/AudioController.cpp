#include "AudioController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Logger.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PlayerControls.hpp"
#include "ui/PlaylistView.hpp"
#include "ui/VisualizerPanel.hpp"
#include "visualizer/VisualizerWindow.hpp"

namespace vc {

AudioController::AudioController(AudioEngine* engine, MainWindow* window)
    : QObject(nullptr), engine_(engine), window_(window) {
}

void AudioController::setupUI(PlayerControls* controls, PlaylistView* view) {
    controls_ = controls;
    view_ = view;

    controls_->setAudioEngine(engine_);
    view_->setPlaylist(&engine_->playlist());
}

void AudioController::connectSignals() {
    // Player controls signals
    connect(controls_, &PlayerControls::playClicked, [this] {
        engine_->play();
    });
    connect(controls_, &PlayerControls::pauseClicked, [this] {
        engine_->pause();
    });
    connect(controls_, &PlayerControls::stopClicked, [this] {
        engine_->stop();
    });
    connect(controls_, &PlayerControls::nextClicked, [this] {
        engine_->playlist().next();
    });
    connect(controls_, &PlayerControls::previousClicked, [this] {
        engine_->playlist().previous();
    });
    connect(controls_,
            &PlayerControls::seekRequested,
            [this](Duration position) { engine_->seek(position); });
    connect(controls_, &PlayerControls::volumeChanged, [this](f32 volume) {
        engine_->setVolume(volume);
    });
    connect(controls_, &PlayerControls::shuffleToggled, [this](bool enabled) {
        engine_->playlist().setShuffle(enabled);
    });
    connect(controls_, &PlayerControls::repeatToggled, [this](RepeatMode mode) {
        engine_->playlist().setRepeatMode(mode);
    });

    // Playlist signals
    connect(view_, &PlaylistView::trackDoubleClicked, [this](usize index) {
        engine_->playlist().jumpTo(index);
    });
    connect(view_,
            &PlaylistView::filesDropped,
            [this](const QStringList& paths) {
                for (const auto& p : paths) {
                    window_->addToPlaylist(fs::path(p.toStdString()));
                }
            });

    // Audio engine signals
    engine_->trackChanged.connect([this] {
        QMetaObject::invokeMethod(window_, [this] {
            // Updated via MainWindow's updateWindowTitle and overlay sync
        });
    });

    engine_->playlist().changed.connect([this] {
        QMetaObject::invokeMethod(this, [this] {
            controls_->setControlsEnabled(!engine_->playlist().empty());
        });
    });

    // Initial state
    controls_->setControlsEnabled(!engine_->playlist().empty());

    engine_->pcmReceived.connect([this](const std::vector<f32>& pcm,
                                        u32 frames,
                                        u32 channels,
                                        u32 sampleRate) {
        if (!pcm.empty() && frames > 0) {
            window_->visualizerPanel()->visualizer()->feedAudio(
                    pcm.data(), frames, channels, sampleRate);
        }
    });
}

} // namespace vc
