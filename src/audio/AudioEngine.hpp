#pragma once
#include "AudioQueue.hpp"
#include "Playlist.hpp"
#include "PcmFormat.hpp"
#include "core/ConfigData.hpp"
#include "util/Result.hpp"
#include "util/Types.hpp"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QString>
#include <QTimer>
#include <atomic>
#include <memory>
#include <optional>

/// Held only for its audioOutputsChanged() signal; see setupDeviceWatchers().
/// Forward-declared in the global namespace, where Qt declares it.
class QMediaDevices;

namespace vc {

enum class PlaybackState { Stopped, Playing, Paused };

class AudioEngine : public QObject {
    Q_OBJECT

public:
    /// Sentinel device name meaning "let the platform choose". Part of the
    /// persisted contract, not a display label: AudioConfig::device defaults to
    /// it, and resolveOutputDevice() honours it alongside the empty string.
    static constexpr std::string_view kDefaultDeviceName = "default";

    explicit AudioEngine(fs::path sessionPath = {});
    ~AudioEngine() override;

    /// Builds the player/output graph and applies an audio configuration.
    ///
    /// @param config The configuration to apply. Passing `std::nullopt` reads
    ///        `Config::instance().audio()`. The default exists because
    ///        Application::init() calls `init()` with no argument, and until it
    ///        forwards CONFIG.audio() explicitly this fallback is the only route
    ///        by which the parsed `[audio]` table and the --audio-device /
    ///        --audio-buffer / --audio-rate overrides reach the sink at all.
    ///        They were parsed, serialised, and then dropped.
    ///
    /// Never fails on a bad device: a saved name whose device has since been
    /// unplugged must not stop the app from starting, so the failure is logged,
    /// emitted on errorSignal(), and reported in audioOutputStatus() while
    /// playback falls back to the system default.
    Result<void> init(std::optional<AudioConfig> config = std::nullopt);

    /// Re-applies a configuration to the live outputs and returns it as the
    /// engine's own state. Call this when a setting changes at runtime.
    ///
    /// Only the device is a hard requirement: Qt 6's QAudioOutput is a
    /// device+volume holder, with no output-period and no target-rate control
    /// (setBufferSize()/setSampleRate() were removed with the Qt 5 redesign),
    /// so `bufferSize` sizes the engine's conversion window and `sampleRate` is
    /// validated against the device's reported range and logged.
    Result<void> applyAudioConfig(const AudioConfig& config);

    /// The configuration currently applied to the outputs.
    [[nodiscard]] const AudioConfig& audioConfig() const { return audioConfig_; }

    /// Interleaved sample count the engine will convert per callback. Equals
    /// pcm::conversionWindow(audioConfig().bufferSize).
    [[nodiscard]] usize interleavedWindow() const { return scratchBuffer_.size(); }

    /// Callbacks dropped for exceeding interleavedWindow(). Was an
    /// unmonitored silent `return` before.
    [[nodiscard]] u64 oversizedBufferCount() const { return oversizedBufferCount_; }

    /// Callbacks dropped for an undecodable sample format. Used to be a silent
    /// pass-through of the previous buffer's samples.
    [[nodiscard]] u64 formatErrorCount() const { return formatErrorCount_; }

    /// One-line description of what the output is actually running: device,
    /// conversion window, requested rate against the device's range, and the
    /// rate the sink has been observed to deliver. The observed rate is the
    /// only honest answer to "what sample rate am I actually hearing", because
    /// the platform, not this config, decides it.
    [[nodiscard]] QString audioOutputStatus() const { return outputStatus_; }

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

    /// A human-readable failure, and the documented recovery signal: an EMPTY
    /// string means "the previous fault cleared", so a consumer can bind one
    /// status line to it and render nothing when it is empty. No QML consumer
    /// existed before, which is how an unplugged device produced silence with
    /// no log, no signal and no UI.
    void errorSignal(const std::string& error);

    /// Emitted whenever audioOutputStatus() changes, including on the first
    /// sink buffer that reveals the rate actually being delivered.
    void audioOutputStatusChanged(QString status);

private slots:
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onErrorOccurred(QMediaPlayer::Error error, const QString& errorString);
    void onAudioBufferReceived(const QAudioBuffer& buffer);
    void onPlaylistCurrentChanged(std::optional<usize> index);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onOutputDeviceChanged();
    void onAudioOutputsChanged();

private:
    /// Quiescent period after the last playlist mutation before the session M3U
    /// is written. Playlist::changed fires once per added file, so writing on
    /// every emission meant dropping 200 files onto the queue performed 200
    /// full rewrites of the whole file on the GUI thread.
    static constexpr int kSessionSaveDebounceMs = 500;

    /// A misbehaving sink delivers a callback every ~20 ms, so reporting every
    /// dropped buffer would write 50 error lines a second until the disk fills.
    /// The first failure and every kConversionErrorReportInterval-th after it
    /// are reported; the counters above stay exact regardless.
    static constexpr u64 kConversionErrorReportInterval = 100;

    void setupConnections(QMediaPlayer* player, QAudioBufferOutput* bufferOutput);
    void setupDeviceWatchers();
    void loadCurrentTrack();
    void prepareNextTrack();
    void swapPlayers();
    void processAudioBuffer(const QAudioBuffer& buffer);
    void loadLastPlaylist();

    /// Re-applies audioConfig() to both outputs after a device-list change,
    /// reporting a vanished configured device instead of keeping it silently.
    void reapplyAfterDeviceListChange();

    /// Rebuilds outputStatus_ from the observed state and emits
    /// audioOutputStatusChanged() if the text moved.
    void refreshOutputStatus();

    /// Logs and, subject to kConversionErrorReportInterval, emits a PCM
    /// conversion failure. @p count is the running total of that failure kind.
    void reportConversionFailure(const pcm::FormatError& error, u64 count, const char* kind);

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
    /// Live QMediaDevices instance, used only for its audioOutputsChanged()
    /// signal. Forward-declared so this header does not pull QMediaDevices in for
    /// every includer; ~AudioEngine() is out of line, which is where the
    /// unique_ptr deleter sees the complete type.
    std::unique_ptr<QMediaDevices> mediaDevices_;

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

    AudioConfig audioConfig_{};

    /// Description of the device the outputs were last pointed at, and the rate
    /// range it reported. Empty/0 until a device has been resolved.
    QString deviceDescription_;
    int deviceMinRate_{0};
    int deviceMaxRate_{0};
    /// Rate the sink has actually delivered, learned from the first buffer.
    /// Zero until one has arrived, which is what makes it an observation rather
    /// than a restatement of the requested value.
    int observedSinkRate_{0};

    QString outputStatus_;
    u64 oversizedBufferCount_{0};
    u64 formatErrorCount_{0};
    /// Set on a conversion failure, cleared by the next success that converts
    /// one. Gates the single empty-string recovery emission on errorSignal.
    bool conversionFaulted_{false};
};

} // namespace vc
