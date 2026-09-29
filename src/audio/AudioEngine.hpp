#pragma once
#include "AudioQueue.hpp"
#include "Playlist.hpp"
#include "util/Result.hpp"
#include "util/Types.hpp"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QTimer>
#include <atomic>
#include <memory>
#include <optional>

namespace vc {

enum class PlaybackState { Stopped, Playing, Paused };

class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(fs::path sessionPath = {});
    ~AudioEngine() override;

    Result<void> init();

    void play();
    void pause();
    void stop();
    void togglePlayPause();
    void seek(Duration position);
    void setVolume(f32 volume);

    PlaybackState state() const { return state_; }
    Duration position() const;
    Duration duration() const;
    f32 volume() const { return volume_; }
    bool isPlaying() const { return state_ == PlaybackState::Playing; }

    Playlist& playlist() { return playlist_; }
    const Playlist& playlist() const { return playlist_; }

    AudioQueue& audioQueue() { return audioQueue_; }
    const AudioQueue& audioQueue() const { return audioQueue_; }

signals:
    void stateChanged(PlaybackState state);
    void positionChanged(Duration position);
    void durationChanged(Duration duration);
    void trackChanged();
    void errorSignal(const std::string& error);

private slots:
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onAudioBufferReceived(const QAudioBuffer& buffer);
    void onPlaylistCurrentChanged(std::optional<usize> index);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);

private:
    static constexpr usize kMaxScratchSamples = 16384;

    /// Quiescent period after the last playlist mutation before the session M3U
    /// is written. Playlist::changed fires once per added file, so writing on
    /// every emission meant dropping 200 files onto the queue performed 200
    /// full rewrites of the whole file on the GUI thread.
    static constexpr int kSessionSaveDebounceMs = 500;

    void setupConnections(QMediaPlayer* player, QAudioBufferOutput* bufferOutput);
    void loadCurrentTrack();
    void prepareNextTrack();
    void swapPlayers();
    void processAudioBuffer(const QAudioBuffer& buffer);
    void loadLastPlaylist();

    /// Writes the session M3U now. Only called by the debounce timer and by the
    /// destructor flush.
    void saveLastPlaylist();

    /// sessionPath_, or the default location when the engine was constructed
    /// without one.
    [[nodiscard]] fs::path sessionFilePath() const;

    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> audioOutput_;
    std::unique_ptr<QAudioBufferOutput> bufferOutput_;
    std::unique_ptr<QMediaPlayer> nextPlayer_;
    std::unique_ptr<QAudioOutput> nextAudioOutput_;
    std::unique_ptr<QAudioBufferOutput> nextBufferOutput_;


    Playlist playlist_;
    fs::path sessionPath_;
    AudioQueue audioQueue_;

    /// Debounces saveLastPlaylist(); restarted by every playlist change.
    QTimer sessionSaveTimer_;

    PlaybackState state_{PlaybackState::Stopped};
    f32 volume_{1.0f};
    bool autoPlayNext_{true};
    /// Armed by onMediaStatusChanged for the automatic EndOfMedia advance, so
    /// that one selection change resumes playback while a user skip or a queue
    /// edit preserves a paused transport. Consumed by the first
    /// onPlaylistCurrentChanged after it is set.
    bool autoAdvance_{false};
    std::vector<f32> scratchBuffer_;
};

} // namespace vc
