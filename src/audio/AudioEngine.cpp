// Version: 1.1.0
// Last Edited: 2026-03-29 12:00:00
// Description: Audio playback engine implementation with lock-free queue

#include "AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QUrl>

namespace vc {

AudioEngine::AudioEngine() : QObject(nullptr) {
}

AudioEngine::~AudioEngine() {
    stop();
    stopAnalyzer_ = true;
    if (analyzerThread_.joinable()) {
        analyzerThread_.join();
    }
}

Result<void> AudioEngine::init() {
    // Create audio output
    audioOutput_ = std::make_unique<QAudioOutput>();
    audioOutput_->setVolume(volume_);

    // Create media player
    player_ = std::make_unique<QMediaPlayer>();
    player_->setAudioOutput(audioOutput_.get());

    // Create buffer output for visualization
    bufferOutput_ = std::make_unique<QAudioBufferOutput>();

    nextAudioOutput_ = std::make_unique<QAudioOutput>();
    nextAudioOutput_->setVolume(volume_);
    nextPlayer_ = std::make_unique<QMediaPlayer>();
    nextPlayer_->setAudioOutput(nextAudioOutput_.get());
    nextBufferOutput_ = std::make_unique<QAudioBufferOutput>();
    nextPlayer_->setAudioBufferOutput(nextBufferOutput_.get());

    player_->setAudioBufferOutput(bufferOutput_.get());

    // Connect signals
    // Connect nextPlayer signals
    connect(nextPlayer_.get(), &QMediaPlayer::playbackStateChanged, this, &AudioEngine::onPlayerStateChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::positionChanged, this, &AudioEngine::onPositionChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::durationChanged, this, &AudioEngine::onDurationChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::errorOccurred, this, &AudioEngine::onErrorOccurred);
    connect(nextPlayer_.get(), &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onMediaStatusChanged);
    connect(nextBufferOutput_.get(), &QAudioBufferOutput::audioBufferReceived, this, &AudioEngine::onAudioBufferReceived);

    connect(player_.get(),
            &QMediaPlayer::playbackStateChanged,
            this,
            &AudioEngine::onPlayerStateChanged);
    // Connect nextPlayer signals
    connect(nextPlayer_.get(), &QMediaPlayer::playbackStateChanged, this, &AudioEngine::onPlayerStateChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::positionChanged, this, &AudioEngine::onPositionChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::durationChanged, this, &AudioEngine::onDurationChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::errorOccurred, this, &AudioEngine::onErrorOccurred);
    connect(nextPlayer_.get(), &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onMediaStatusChanged);
    connect(nextBufferOutput_.get(), &QAudioBufferOutput::audioBufferReceived, this, &AudioEngine::onAudioBufferReceived);

    connect(player_.get(),
            &QMediaPlayer::positionChanged,
            this,
            &AudioEngine::onPositionChanged);
    // Connect nextPlayer signals
    connect(nextPlayer_.get(), &QMediaPlayer::playbackStateChanged, this, &AudioEngine::onPlayerStateChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::positionChanged, this, &AudioEngine::onPositionChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::durationChanged, this, &AudioEngine::onDurationChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::errorOccurred, this, &AudioEngine::onErrorOccurred);
    connect(nextPlayer_.get(), &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onMediaStatusChanged);
    connect(nextBufferOutput_.get(), &QAudioBufferOutput::audioBufferReceived, this, &AudioEngine::onAudioBufferReceived);

    connect(player_.get(),
            &QMediaPlayer::durationChanged,
            this,
            &AudioEngine::onDurationChanged);
    // Connect nextPlayer signals
    connect(nextPlayer_.get(), &QMediaPlayer::playbackStateChanged, this, &AudioEngine::onPlayerStateChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::positionChanged, this, &AudioEngine::onPositionChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::durationChanged, this, &AudioEngine::onDurationChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::errorOccurred, this, &AudioEngine::onErrorOccurred);
    connect(nextPlayer_.get(), &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onMediaStatusChanged);
    connect(nextBufferOutput_.get(), &QAudioBufferOutput::audioBufferReceived, this, &AudioEngine::onAudioBufferReceived);

    connect(player_.get(),
            &QMediaPlayer::errorOccurred,
            this,
            &AudioEngine::onErrorOccurred);
    // Connect nextPlayer signals
    connect(nextPlayer_.get(), &QMediaPlayer::playbackStateChanged, this, &AudioEngine::onPlayerStateChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::positionChanged, this, &AudioEngine::onPositionChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::durationChanged, this, &AudioEngine::onDurationChanged);
    connect(nextPlayer_.get(), &QMediaPlayer::errorOccurred, this, &AudioEngine::onErrorOccurred);
    connect(nextPlayer_.get(), &QMediaPlayer::mediaStatusChanged, this, &AudioEngine::onMediaStatusChanged);
    connect(nextBufferOutput_.get(), &QAudioBufferOutput::audioBufferReceived, this, &AudioEngine::onAudioBufferReceived);

    connect(player_.get(),
            &QMediaPlayer::mediaStatusChanged,
            this,
            &AudioEngine::onMediaStatusChanged);

    connect(bufferOutput_.get(),
            &QAudioBufferOutput::audioBufferReceived,
            this,
            &AudioEngine::onAudioBufferReceived);

    // Connect playlist signals
    playlist_.currentChanged.connect(
            [this](usize index) { onPlaylistCurrentChanged(index); });
    playlist_.changed.connect([this] { saveLastPlaylist(); });

    // Load last session playlist
    loadLastPlaylist();

    // Diagnostic timer to check if audio is being received
    connect(&bufferCheckTimer_, &QTimer::timeout, this, [this]() {
        if (state_ == PlaybackState::Playing &&
            !bufferReceivedSinceLastCheck_) {
            LOG_WARN(
                    "AudioEngine: No audio buffers received in last 1000ms - "
                    "QAudioBufferOutput may not be working");
        }
        bufferReceivedSinceLastCheck_ = false;
    });
    bufferCheckTimer_.start(1000);

    stopAnalyzer_ = false;
    analyzerThread_ = std::jthread(&AudioEngine::analyzerWorker, this);

    LOG_INFO("Audio engine initialized with QAudioBufferOutput");
    return Result<void>::ok();
}

void AudioEngine::play() {
    LOG_INFO("AudioEngine::play() CALLED");

    // Reset diagnostic flag for fresh start
    bufferReceivedSinceLastCheck_ = false;

    if (!playlist_.currentItem() && !playlist_.empty()) {
        playlist_.jumpTo(0);
        LOG_INFO("Jumped to first playlist item");
    }

    if (player_->source().isEmpty() && playlist_.currentItem()) {
        loadCurrentTrack();
        LOG_INFO("Loaded current track");
    }

    if (player_->source().isEmpty()) {
        LOG_WARN("AudioEngine: Cannot play, no source loaded");
        return;
    }

    LOG_INFO("Calling player_->play(), source={}",
             player_->source().toString().toStdString());
    player_->play();
    LOG_INFO("player_->play() returned, state={}",
             static_cast<int>(player_->playbackState()));
}

void AudioEngine::pause() {
    player_->pause();
}

void AudioEngine::stop() {
    player_->stop();
    analyzer_.reset();
}

void AudioEngine::togglePlayPause() {
    if (state_ == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void AudioEngine::seek(Duration position) {
    player_->setPosition(position.count());
}

void AudioEngine::setVolume(f32 volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (audioOutput_) {
        audioOutput_->setVolume(volume_);
    }
}

Duration AudioEngine::position() const {
    return Duration(player_->position());
}

Duration AudioEngine::duration() const {
    return Duration(player_->duration());
}

void AudioEngine::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    if (sender() != player_.get()) return;
    PlaybackState oldState = state_;
    switch (state) {
    case QMediaPlayer::StoppedState:
        state_ = PlaybackState::Stopped;
        break;
    case QMediaPlayer::PlayingState:
        state_ = PlaybackState::Playing;
        bufferReceivedSinceLastCheck_ = false;
        break;
    case QMediaPlayer::PausedState:
        state_ = PlaybackState::Paused;
        break;
    }

	LOG_INFO("AudioEngine: Player state changed from {} to {}",
		static_cast<int>(oldState),
		static_cast<int>(state_));
	emit stateChanged(state_);
}

void AudioEngine::onPositionChanged(qint64 position) {
    if (sender() != player_.get()) return;
	emit positionChanged(Duration(position));
}

void AudioEngine::onDurationChanged(qint64 duration) {
    if (sender() != player_.get()) return;
	emit durationChanged(Duration(duration));
}

void AudioEngine::onErrorOccurred(QMediaPlayer::Error err,
	const QString& errorString) {
    if (sender() != player_.get()) return;
	LOG_ERROR("Playback error: {}", errorString.toStdString());
	emit errorSignal(errorString.toStdString());
}

void AudioEngine::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (sender() != player_.get()) return;
    if (status == QMediaPlayer::EndOfMedia && autoPlayNext_) {
        LOG_DEBUG("Track ended, playing next");
        
        if (!nextPlayer_->source().isEmpty()) {
            LOG_INFO("Swapping to preloaded player for next track");
            swapPlayers();
            playlist_.next(); 
        } else {
            if (!playlist_.next()) {
                stop();
            }
        }
    }
}

void AudioEngine::onAudioBufferReceived(const QAudioBuffer& buffer) {
    if (sender() != bufferOutput_.get()) return;
    bufferReceivedSinceLastCheck_ = true;
    LOG_DEBUG("AudioEngine::onAudioBufferReceived: buffer valid={}, frames={}",
              buffer.isValid(),
              buffer.frameCount());
    processAudioBuffer(buffer);
}

void AudioEngine::onPlaylistCurrentChanged(usize index) {
	loadCurrentTrack();
	emit trackChanged();
	play();
}

void AudioEngine::prepareNextTrack() {
    const auto* nextItem = playlist_.itemAt(playlist_.currentIndex().value_or(0) + 1);
    if (!nextItem) return;

    if (nextItem->isRemote) {
        nextPlayer_->setSource(QUrl(QString::fromStdString(nextItem->url)));
    } else {
        nextPlayer_->setSource(QUrl::fromLocalFile(QString::fromStdString(nextItem->path.string())));
    }
}

void AudioEngine::swapPlayers() {
    player_.swap(nextPlayer_);
    audioOutput_.swap(nextAudioOutput_);
    bufferOutput_.swap(nextBufferOutput_);
}

void AudioEngine::loadCurrentTrack() {
    const auto* item = playlist_.currentItem();
    if (!item)
        return;

    QUrl newSource;
    if (item->isRemote) {
        newSource = QUrl(QString::fromStdString(item->url));
    } else {
        newSource = QUrl::fromLocalFile(QString::fromStdString(item->path.string()));
    }

    if (player_->source() != newSource) {
        LOG_INFO("Loading track: {}", item->path.filename().string());
        player_->setSource(newSource);
    }
    
    prepareNextTrack();
}

void AudioEngine::loadLastPlaylist() {
    auto path = file::configDir() / "last_session.m3u";
    if (fs::exists(path)) {
        LOG_INFO("Loading last session playlist...");
        playlist_.loadM3U(path);

        // Try to load last index if it exists in a separate file or config
        // For now, just loading the files is a huge improvement
    }
}

void AudioEngine::saveLastPlaylist() {
    auto path = file::configDir() / "last_session.m3u";
    file::ensureDir(path.parent_path());
    playlist_.saveM3U(path);
}

void AudioEngine::analyzerWorker() {
    std::vector<f32> localBuffer;
    localBuffer.resize(2048);
    
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
    if (!buffer.isValid())
        return;

    const auto format = buffer.format();
    const auto sampleRate = format.sampleRate();
    const auto channels = format.channelCount();
    const auto frameCount = static_cast<usize>(buffer.frameCount());
    const usize totalSamples = frameCount * static_cast<usize>(channels);

    // Zero-allocation: reuse scratch buffer
    if (scratchBuffer_.size() < totalSamples) {
        scratchBuffer_.resize(totalSamples);
    }

    // Convert to float samples (in-place)
    if (format.sampleFormat() == QAudioFormat::Float) {
        const f32* data = buffer.constData<f32>();
        std::copy(data, data + totalSamples, scratchBuffer_.begin());
    } else if (format.sampleFormat() == QAudioFormat::Int16) {
        const i16* data = buffer.constData<i16>();
        for (usize i = 0; i < totalSamples; ++i) {
            scratchBuffer_[i] = static_cast<f32>(data[i]) / 32768.0f;
        }
    } else if (format.sampleFormat() == QAudioFormat::Int32) {
        const i32* data = buffer.constData<i32>();
        for (usize i = 0; i < totalSamples; ++i) {
            scratchBuffer_[i] = static_cast<f32>(data[i]) / 2147483648.0f;
        }
    }

    // Push to lock-free audio queues (no mutex)
    audioQueue_.pushAll(scratchBuffer_.data(),
                         static_cast<u32>(frameCount),
                         static_cast<u32>(channels),
                         static_cast<u32>(sampleRate));

    // Emit PCM data for legacy consumers (backward compatibility)
    emit pcmReceived(scratchBuffer_,
                     static_cast<u32>(frameCount),
                     static_cast<u32>(channels),
                     static_cast<u32>(sampleRate));
}

} // namespace vc

#include "moc_AudioEngine.cpp"
