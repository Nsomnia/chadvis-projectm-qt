#pragma once
// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Audio playback engine with lock-free queue integration

#include <projectM-4/projectM.h>
#include "AudioAnalyzer.hpp"
#include "AudioQueue.hpp"
#include "Playlist.hpp"
#include "util/Result.hpp"
#include "util/Types.hpp"

#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QTimer>
#include <memory>
#include <thread>
#include <atomic>

namespace vc {

class FFmpegAudioSource;

enum class PlaybackState { Stopped, Playing, Paused };

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
    void setVolume(f32 volume); // 0.0 - 1.0

    // State
    PlaybackState state() const {
        return state_;
    }
    Duration position() const;
    Duration duration() const;
    f32 volume() const {
        return volume_;
    }
    bool isPlaying() const {
        return state_ == PlaybackState::Playing;
    }

    // Playlist access
    Playlist& playlist() {
        return playlist_;
    }
    const Playlist& playlist() const {
        return playlist_;
    }

    // Audio analysis for visualizer (lock-free access)
    AudioSpectrum currentSpectrum() const {
        return currentSpectrum_;
    }
    std::vector<f32> currentPCM() const {
        return analyzer_.pcmData();
    }

    // Lock-free audio queue access
    AudioQueue& audioQueue() { return audioQueue_; }
    const AudioQueue& audioQueue() const { return audioQueue_; }

signals:
	void stateChanged(PlaybackState state);
	void positionChanged(Duration position);
	void durationChanged(Duration duration);
	void spectrumUpdated(const AudioSpectrum& spectrum);
	void trackChanged();
	void errorSignal(const std::string& error);
	void pcmReceived(const std::vector<f32>& data, u32 frames, u32 channels, u32 sampleRate);

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
    void prepareNextTrack();
    void swapPlayers();
    void processAudioBuffer(const QAudioBuffer& buffer);
    void analyzerWorker();
    void onFFmpegPCM(const std::vector<f32>& pcm,
                     u32 frames,
                     u32 channels,
                     u32 sampleRate);

    void loadLastPlaylist();
    void saveLastPlaylist();

    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> audioOutput_;
    std::unique_ptr<QAudioBufferOutput> bufferOutput_;

    std::unique_ptr<QMediaPlayer> nextPlayer_;
    std::unique_ptr<QAudioOutput> nextAudioOutput_;
    std::unique_ptr<QAudioBufferOutput> nextBufferOutput_;

    std::jthread analyzerThread_;
    std::atomic<bool> stopAnalyzer_{false};

    Playlist playlist_;
    AudioAnalyzer analyzer_;
    AudioSpectrum currentSpectrum_;
    AudioQueue audioQueue_;

    PlaybackState state_{PlaybackState::Stopped};
    f32 volume_{1.0f};
    bool autoPlayNext_{true};

    // Zero-allocation scratch buffer for audio processing
    std::vector<f32> scratchBuffer_;

    // Diagnostic
    QTimer bufferCheckTimer_;
    bool bufferReceivedSinceLastCheck_{false};
};

} // namespace vc
