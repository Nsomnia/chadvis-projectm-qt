#pragma once
// AudioEngine.hpp - Audio playback engine
// Qt Multimedia doing the heavy lifting

#include "util/Types.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "AudioAnalyzer.hpp"
#include "Playlist.hpp"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QTimer>
#include <memory>

namespace vc {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

class AudioEngine : public QObject {
    Q_OBJECT
    
public:
    AudioEngine();
    ~AudioEngine() override;
    
    Result<void> init();
    
    // Playback control
    void play();
    void pause();
    void stop();
    void togglePlayPause();
    
    void seek(Duration position);
    void setVolume(f32 volume);  // 0.0 - 1.0
    
    // State
    PlaybackState state() const { return state_; }
    Duration position() const;
    Duration duration() const;
    f32 volume() const { return volume_; }
    bool isPlaying() const { return state_ == PlaybackState::Playing; }
    
    // Playlist access
    Playlist& playlist() { return playlist_; }
    const Playlist& playlist() const { return playlist_; }
    
    // Audio analysis for visualizer
    const AudioSpectrum& currentSpectrum() const { return currentSpectrum_; }
    const std::vector<f32>& currentPCM() const { return analyzer_.pcmData(); }
    
    // Signals
    Signal<PlaybackState> stateChanged;
    Signal<Duration> positionChanged;
    Signal<Duration> durationChanged;
    Signal<const AudioSpectrum&> spectrumUpdated;
    Signal<> trackChanged;
    Signal<std::string> error;
    Signal<const std::vector<f32>&, u32, u32, u32> pcmReceived;  // data, frames, channels, sampleRate
    
private slots:
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onAudioBufferReceived(const QAudioBuffer& buffer);
    void onPlaylistCurrentChanged(usize index);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    
private:
    void loadCurrentTrack();
    void processAudioBuffer(const QAudioBuffer& buffer);
    
    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> audioOutput_;
    std::unique_ptr<QAudioBufferOutput> bufferOutput_;
    
    Playlist playlist_;
    AudioAnalyzer analyzer_;
    AudioSpectrum currentSpectrum_;
    
    PlaybackState state_{PlaybackState::Stopped};
    f32 volume_{1.0f};
    bool autoPlayNext_{true};
};

} // namespace vc
