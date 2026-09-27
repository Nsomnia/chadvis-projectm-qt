#include "AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QUrl>

namespace vc {

AudioEngine::AudioEngine(fs::path sessionPath)
    : QObject(nullptr), sessionPath_(std::move(sessionPath))
{
    // Debounced session-playlist persistence. Playlist::changed fires once per
    // mutation, so writing on every emission turned "drop 200 files on the
    // playlist" into 200 full M3U rewrites on the GUI thread. Coalescing to one
    // write per quiescent burst is the same pattern OverlayBridge uses for its
    // overlay JSON; the flush in the destructor below is what keeps a normal
    // quit from losing the session playlist.
    sessionSaveTimer_.setSingleShot(true);
    sessionSaveTimer_.setInterval(kSessionSaveDebounceMs);
    connect(&sessionSaveTimer_, &QTimer::timeout, this, [this] { saveLastPlaylist(); });
}

AudioEngine::~AudioEngine() {
    // Explicit flush so the last mutation is never lost on shutdown, matching
    // OverlayBridge's destructor.
    if (sessionSaveTimer_.isActive()) {
        sessionSaveTimer_.stop();
        saveLastPlaylist();
    }

    stop();
}

Result<void> AudioEngine::init() {
    scratchBuffer_.resize(kMaxScratchSamples);
    audioOutput_ = std::make_unique<QAudioOutput>();
    audioOutput_->setVolume(volume_);

    player_ = std::make_unique<QMediaPlayer>();
    player_->setAudioOutput(audioOutput_.get());

    bufferOutput_ = std::make_unique<QAudioBufferOutput>();
    player_->setAudioBufferOutput(bufferOutput_.get());

    setupConnections(player_.get(), bufferOutput_.get());

    // Preload player for gapless
    nextAudioOutput_ = std::make_unique<QAudioOutput>();
    nextAudioOutput_->setVolume(volume_);
    nextPlayer_ = std::make_unique<QMediaPlayer>();
    nextPlayer_->setAudioOutput(nextAudioOutput_.get());
    nextBufferOutput_ = std::make_unique<QAudioBufferOutput>();
    nextPlayer_->setAudioBufferOutput(nextBufferOutput_.get());

    // Playlist signals
    playlist_.currentChanged.connect([this](std::optional<usize> index) { onPlaylistCurrentChanged(index); });
    playlist_.changed.connect([this] { sessionSaveTimer_.start(); });

    loadLastPlaylist();

    LOG_INFO("Audio engine initialized with QAudioBufferOutput");
    return Result<void>::ok();
}

void AudioEngine::setupConnections(QMediaPlayer* player, QAudioBufferOutput* bufferOutput) {
    connect(player, &QMediaPlayer::playbackStateChanged, this, &AudioEngine::onPlayerStateChanged);
    connect(player, &QMediaPlayer::positionChanged, this, &AudioEngine::onPositionChanged);
    connect(player, &QMediaPlayer::durationChanged, this, &AudioEngine::onDurationChanged);
    connect(player, &QMediaPlayer::errorOccurred, this, &AudioEngine::onErrorOccurred);
    connect(player, &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onMediaStatusChanged);
    connect(bufferOutput, &QAudioBufferOutput::audioBufferReceived, this, &AudioEngine::onAudioBufferReceived);
}

void AudioEngine::play() {
    if (!playlist_.currentIndex() && !playlist_.empty()) {
        playlist_.startPlayback();
    }
    if (player_->source().isEmpty() && playlist_.currentItem()) {
        loadCurrentTrack();
    }
    if (!player_->source().isEmpty()) {
        player_->play();
    }
}

void AudioEngine::pause() { player_->pause(); }
void AudioEngine::stop() { player_->stop(); }

void AudioEngine::togglePlayPause() {
    if (state_ == PlaybackState::Playing) pause();
    else play();
}

void AudioEngine::seek(Duration position) { player_->setPosition(position.count()); }

void AudioEngine::setVolume(f32 volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (audioOutput_) audioOutput_->setVolume(volume_);
    if (nextAudioOutput_) nextAudioOutput_->setVolume(volume_);
}

Duration AudioEngine::position() const { return Duration(player_->position()); }
Duration AudioEngine::duration() const { return Duration(player_->duration()); }

void AudioEngine::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    if (sender() != player_.get()) return;
    switch (state) {
        case QMediaPlayer::StoppedState: state_ = PlaybackState::Stopped; break;
        case QMediaPlayer::PlayingState: state_ = PlaybackState::Playing; break;
        case QMediaPlayer::PausedState: state_ = PlaybackState::Paused; break;
    }
    emit stateChanged(state_);
}

void AudioEngine::onPositionChanged(qint64 position) {
    if (sender() == player_.get()) emit positionChanged(Duration(position));
}

void AudioEngine::onDurationChanged(qint64 duration) {
    if (sender() == player_.get()) emit durationChanged(Duration(duration));
}

void AudioEngine::onErrorOccurred(QMediaPlayer::Error err, const QString& errorString) {
    if (sender() == player_.get()) {
        LOG_ERROR("Playback error: {}", errorString.toStdString());
        emit errorSignal(errorString.toStdString());
    }
}

void AudioEngine::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (sender() != player_.get()) return;
    if (status == QMediaPlayer::EndOfMedia && autoPlayNext_) {
        // The queue advance below is the one selection change that must start
        // playback on its own. Arm it explicitly instead of inferring the
        // intent from the transport state, because Qt makes no promise about
        // whether playbackStateChanged(StoppedState) arrives before
        // mediaStatusChanged(EndOfMedia) - reading state_ here would make
        // gapless advance depend on that ordering.
        autoAdvance_ = true;
        if (!nextPlayer_->source().isEmpty()) {
            swapPlayers();
            playlist_.next(); 
        } else if (!playlist_.next()) {
            stop();
        }
    }
}

void AudioEngine::onAudioBufferReceived(const QAudioBuffer& buffer) {
    if (sender() == bufferOutput_.get()) processAudioBuffer(buffer);
}

void AudioEngine::onPlaylistCurrentChanged(std::optional<usize> index) {
    // This handler is bound to currentChanged, so *every* index change lands
    // here: a user skip, a queue edit that renumbers the selection, and the
    // automatic EndOfMedia advance. Only the last of those may start playback
    // by itself. Skipping a track while paused used to resume playback,
    // because the handler called play() unconditionally.
    //
    // The flag is consumed by the first emission after it is armed, because a
    // queue edit can emit more than one.
    const bool shouldResume = autoAdvance_ || state_ == PlaybackState::Playing;
    autoAdvance_ = false;

    if (!index) {
        // The selected track was removed, or the queue was cleared. This is the
        // case the old `Signal<usize>` could not express at all, which is why
        // the engine used to keep playing a deleted file with a stale
        // pre-buffered next track. There is nothing to play now, so say so.
        nextPlayer_->setSource(QUrl());
        stop();
        emit trackChanged();
        return;
    }

    loadCurrentTrack();
    emit trackChanged();
    if (shouldResume)
        play();
}

void AudioEngine::swapPlayers() {
    player_.swap(nextPlayer_);
    audioOutput_.swap(nextAudioOutput_);
    bufferOutput_.swap(nextBufferOutput_);
    // Reconnect new active player
    disconnect(nextPlayer_.get(), nullptr, this, nullptr);
    disconnect(nextBufferOutput_.get(), nullptr, this, nullptr);
    setupConnections(player_.get(), bufferOutput_.get());
}

void AudioEngine::loadCurrentTrack() {
    const auto item = playlist_.currentItem();
    if (!item) return;
    QUrl source = item->isRemote ? QUrl(QString::fromStdString(item->url)) : QUrl::fromLocalFile(QString::fromStdString(item->path.string()));
    if (player_->source() != source) player_->setSource(source);
    prepareNextTrack();
}

void AudioEngine::prepareNextTrack() {
    const auto nextItem = playlist_.itemAt(playlist_.currentIndex().value_or(0) + 1);
    if (!nextItem) {
        nextPlayer_->setSource(QUrl());
        return;
    }
    QUrl source = nextItem->isRemote ? QUrl(QString::fromStdString(nextItem->url)) : QUrl::fromLocalFile(QString::fromStdString(nextItem->path.string()));
    nextPlayer_->setSource(source);
}

fs::path AudioEngine::sessionFilePath() const {
    return sessionPath_.empty() ? file::configDir() / "last_session.m3u" : sessionPath_;
}

void AudioEngine::loadLastPlaylist() {
    const auto path = sessionFilePath();
    if (fs::exists(path)) {
        if (auto result = playlist_.loadM3U(path); !result) {
            LOG_WARN("AudioEngine: Failed to load last playlist: {}", result.error().message);
        }
    }
}

void AudioEngine::saveLastPlaylist() {
    const auto path = sessionFilePath();
    if (auto result = file::ensureDir(path.parent_path()); !result) {
        LOG_WARN("AudioEngine: Failed to create playlist directory: {}", result.error().message);
        return;
    }
    if (auto result = playlist_.saveM3U(path); !result) {
        LOG_WARN("AudioEngine: Failed to save last playlist: {}", result.error().message);
    }
}

void AudioEngine::processAudioBuffer(const QAudioBuffer& buffer) {
    if (!buffer.isValid()) return;
    const auto format = buffer.format();
    const usize frameCount = static_cast<usize>(buffer.frameCount());
    const usize channels = static_cast<usize>(format.channelCount());
    const usize totalSamples = frameCount * channels;

    if (totalSamples > scratchBuffer_.size()) {
        return;
    }

    if (format.sampleFormat() == QAudioFormat::Float) {
        std::copy(buffer.constData<f32>(), buffer.constData<f32>() + totalSamples, scratchBuffer_.begin());
    } else if (format.sampleFormat() == QAudioFormat::Int16) {
        const i16* data = buffer.constData<i16>();
        for (usize i = 0; i < totalSamples; ++i) scratchBuffer_[i] = static_cast<f32>(data[i]) / 32768.0f;
    }

    const AudioChunk chunk{
        .samples = std::span<const f32>(scratchBuffer_.data(), totalSamples),
        .channels = static_cast<u32>(channels),
        .sampleRate = static_cast<u32>(format.sampleRate()),
    };
    audioQueue_.pushAll(chunk);
    // Deliberately no PCM signal here. The old `pcmReceived` had zero receivers
    // repo-wide and copied the whole scratch buffer per audio callback for
    // nobody; consumers that want PCM read it from the AudioQueue, which is
    // already the single copy.
}

} // namespace vc
