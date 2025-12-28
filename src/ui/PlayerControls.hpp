#pragma once
// PlayerControls.hpp - Transport controls widget
// Play, pause, stop - the holy trinity

#include "util/Types.hpp"
#include "audio/AudioEngine.hpp"

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

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
    void updateSeekSlider();
    QString formatTime(Duration dur);
    
    AudioEngine* audioEngine_{nullptr};
    
    // Buttons
    QPushButton* prevButton_{nullptr};
    QPushButton* playPauseButton_{nullptr};
    QPushButton* stopButton_{nullptr};
    QPushButton* nextButton_{nullptr};
    QPushButton* shuffleButton_{nullptr};
    QPushButton* repeatButton_{nullptr};
    
    // Seek bar
    QSlider* seekSlider_{nullptr};
    QLabel* currentTimeLabel_{nullptr};
    QLabel* totalTimeLabel_{nullptr};
    bool seeking_{false};
    
    // Volume
    QSlider* volumeSlider_{nullptr};
    QPushButton* muteButton_{nullptr};
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
