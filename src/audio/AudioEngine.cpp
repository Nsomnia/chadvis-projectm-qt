#include "AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QUrl>

namespace vc {

AudioEngine::AudioEngine() : QObject(nullptr) {}

AudioEngine::~AudioEngine() {
    stop();
    stopAnalyzer_ = true;
    if (analyzerThread_.joinable()) {
        analyzerThread_.join();
    }
}

Result<void> AudioEngine::init() {
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
    playlist_.currentChanged.connect([this](usize index) { onPlaylistCurrentChanged(index); });
    playlist_.changed.connect([this] { saveLastPlaylist(); });

    loadLastPlaylist();

    stopAnalyzer_ = false;
    analyzerThread_ = std::jthread(&AudioEngine::analyzerWorker, this);

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
    if (!playlist_.currentItem() && !playlist_.empty()) {
        playlist_.jumpTo(0);
    }
    if (player_->source().isEmpty() && playlist_.currentItem()) {
        loadCurrentTrack();
    }
    if (!player_->source().isEmpty()) {
        player_->play();
    }
}

void AudioEngine::pause() { player_->pause(); }
void AudioEngine::stop() { player_->stop(); analyzer_.reset(); }

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

void AudioEngine::onPlaylistCurrentChanged(usize index) {
    loadCurrentTrack();
    emit trackChanged();
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
    const auto* item = playlist_.currentItem();
    if (!item) return;
    QUrl source = item->isRemote ? QUrl(QString::fromStdString(item->url)) : QUrl::fromLocalFile(QString::fromStdString(item->path.string()));
    if (player_->source() != source) player_->setSource(source);
    prepareNextTrack();
}

void AudioEngine::prepareNextTrack() {
    const auto* nextItem = playlist_.itemAt(playlist_.currentIndex().value_or(0) + 1);
    if (!nextItem) {
        nextPlayer_->setSource(QUrl());
        return;
    }
    QUrl source = nextItem->isRemote ? QUrl(QString::fromStdString(nextItem->url)) : QUrl::fromLocalFile(QString::fromStdString(nextItem->path.string()));
    nextPlayer_->setSource(source);
}

void AudioEngine::loadLastPlaylist() {
    auto path = file::configDir() / "last_session.m3u";
    if (fs::exists(path)) playlist_.loadM3U(path);
}

void AudioEngine::saveLastPlaylist() {
    auto path = file::configDir() / "last_session.m3u";
    file::ensureDir(path.parent_path());
    playlist_.saveM3U(path);
}

void AudioEngine::analyzerWorker() {
    std::vector<f32> localBuffer(2048);
    while (!stopAnalyzer_) {
        u32 popped = audioQueue_.popAnaBatch(localBuffer.data(), localBuffer.size() / 2);
        if (popped > 0) {
            currentSpectrum_ = analyzer_.analyze(std::span(localBuffer.data(), popped * 2), 48000, 2);
            emit spectrumUpdated(currentSpectrum_);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void AudioEngine::processAudioBuffer(const QAudioBuffer& buffer) {
    if (!buffer.isValid()) return;
    const auto format = buffer.format();
    const usize frameCount = static_cast<usize>(buffer.frameCount());
    const usize channels = static_cast<usize>(format.channelCount());
    const usize totalSamples = frameCount * channels;

    if (scratchBuffer_.size() < totalSamples) scratchBuffer_.resize(totalSamples);

    if (format.sampleFormat() == QAudioFormat::Float) {
        std::copy(buffer.constData<f32>(), buffer.constData<f32>() + totalSamples, scratchBuffer_.begin());
    } else if (format.sampleFormat() == QAudioFormat::Int16) {
        const i16* data = buffer.constData<i16>();
        for (usize i = 0; i < totalSamples; ++i) scratchBuffer_[i] = static_cast<f32>(data[i]) / 32768.0f;
    }

    audioQueue_.pushAll(scratchBuffer_.data(), static_cast<u32>(frameCount), static_cast<u32>(channels), static_cast<u32>(format.sampleRate()));
    emit pcmReceived(scratchBuffer_, static_cast<u32>(frameCount), static_cast<u32>(channels), static_cast<u32>(format.sampleRate()));
}

} // namespace vc
