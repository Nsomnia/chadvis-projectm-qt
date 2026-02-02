#pragma once
// PlayerControls.hpp - Transport controls widget
// Play, pause, stop - the holy trinity
// Part of the Modern 1337 Chad GUI (uses custom widgets)

#include "audio/AudioEngine.hpp"
#include "ui/widgets/CyanSlider.hpp"
#include "ui/widgets/GlowButton.hpp"
#include "ui/widgets/ToggleSwitch.hpp"
#include "util/Types.hpp"

#include <QLabel>
#include <QWidget>

namespace vc {

class PlayerControls : public QWidget {
    Q_OBJECT

public:
    explicit PlayerControls(QWidget* parent = nullptr);

    void setAudioEngine(AudioEngine* engine);

signals:
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void nextClicked();
    void previousClicked();
    void shuffleToggled(bool enabled);
    void repeatToggled(RepeatMode mode);
    void seekRequested(Duration position);
    void volumeChanged(f32 volume);

public slots:
    void updatePlaybackState(PlaybackState state);
    void updatePosition(Duration position);
    void updateDuration(Duration duration);
    void updateTrackInfo(const MediaMetadata& meta);
    void setControlsEnabled(bool enabled);

private slots:
    void onPlayPauseClicked();
    void onSeekSliderPressed();
    void onSeekSliderReleased();
    void onSeekSliderMoved(int value);
    void onVolumeSliderChanged(int value);
    void onShuffleClicked();
    void onRepeatClicked();

private:
    void setupUI();
    void applyModernStyling();
    void updateSeekSlider();
    QString formatTime(Duration dur);

    AudioEngine* audioEngine_{nullptr};

    // Transport buttons (modern glow style)
    chadvis::GlowButton* prevButton_{nullptr};
    chadvis::GlowButton* playPauseButton_{nullptr};
    chadvis::GlowButton* stopButton_{nullptr};
    chadvis::GlowButton* nextButton_{nullptr};
    chadvis::GlowButton* shuffleButton_{nullptr};
    chadvis::GlowButton* repeatButton_{nullptr};

    // Seek bar (cyan accent style)
    chadvis::CyanSlider* seekSlider_{nullptr};
    QLabel* currentTimeLabel_{nullptr};
    QLabel* totalTimeLabel_{nullptr};
    bool seeking_{false};

    // Volume (cyan accent style)
    chadvis::CyanSlider* volumeSlider_{nullptr};
    chadvis::GlowButton* muteButton_{nullptr};
    f32 lastVolume_{1.0f};

    // Track info
    QLabel* titleLabel_{nullptr};
    QLabel* artistLabel_{nullptr};
    QLabel* albumArtLabel_{nullptr};

    Duration currentPosition_{0};
    Duration currentDuration_{0};
    PlaybackState currentState_{PlaybackState::Stopped};
    bool shuffle_{false};
    RepeatMode repeatMode_{RepeatMode::Off};
};

} // namespace vc
