#include "AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

#include <QUrl>
#include <QAudioDevice>
#include <QMediaDevices>

namespace vc {

AudioEngine::AudioEngine()
    : QObject(nullptr)
{
}

AudioEngine::~AudioEngine() {
    stop();
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
    player_->setAudioBufferOutput(bufferOutput_.get());
    
    // Connect signals
    connect(player_.get(), &QMediaPlayer::playbackStateChanged,
            this, &AudioEngine::onPlayerStateChanged);
    connect(player_.get(), &QMediaPlayer::positionChanged,
            this, &AudioEngine::onPositionChanged);
    connect(player_.get(), &QMediaPlayer::durationChanged,
            this, &AudioEngine::onDurationChanged);
    connect(player_.get(), &QMediaPlayer::errorOccurred,
            this, &AudioEngine::onErrorOccurred);
    connect(player_.get(), &QMediaPlayer::mediaStatusChanged,
            this, &AudioEngine::onMediaStatusChanged);
    
    connect(bufferOutput_.get(), &QAudioBufferOutput::audioBufferReceived,
            this, &AudioEngine::onAudioBufferReceived);
    
    // Connect playlist signals
    playlist_.currentChanged.connect([this](usize index) {
        onPlaylistCurrentChanged(index);
    });
    
    // Diagnostic timer to check if audio is being received
    connect(&bufferCheckTimer_, &QTimer::timeout, this, [this]() {
        if (state_ == PlaybackState::Playing && !bufferReceivedSinceLastCheck_) {
            LOG_WARN("AudioEngine: No audio buffers received in last 1000ms - QAudioBufferOutput may not be working");
        }
        bufferReceivedSinceLastCheck_ = false;
    });
    bufferCheckTimer_.start(1000);
    
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
    
    LOG_INFO("Calling player_->play(), source={}", player_->source().toString().toStdString());
    player_->play();
    LOG_INFO("player_->play() returned, state={}", static_cast<int>(player_->playbackState()));
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
             static_cast<int>(oldState), static_cast<int>(state_));
    stateChanged.emitSignal(state_);
}

void AudioEngine::onPositionChanged(qint64 position) {
    positionChanged.emitSignal(Duration(position));
}

void AudioEngine::onDurationChanged(qint64 duration) {
    durationChanged.emitSignal(Duration(duration));
}

void AudioEngine::onErrorOccurred(QMediaPlayer::Error err, const QString& errorString) {
    LOG_ERROR("Playback error: {}", errorString.toStdString());
    errorSignal.emitSignal(errorString.toStdString());
}

void AudioEngine::onMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::EndOfMedia && autoPlayNext_) {
        LOG_DEBUG("Track ended, playing next");
        if (!playlist_.next()) {
            stop();
        }
    }
}

void AudioEngine::onAudioBufferReceived(const QAudioBuffer& buffer) {
    bufferReceivedSinceLastCheck_ = true;
    LOG_DEBUG("AudioEngine::onAudioBufferReceived: buffer valid={}, frames={}", 
               buffer.isValid(), buffer.frameCount());
    processAudioBuffer(buffer);
}

void AudioEngine::onPlaylistCurrentChanged(usize index) {
    loadCurrentTrack();
    trackChanged.emitSignal();
    play();
}

void AudioEngine::loadCurrentTrack() {
    const auto* item = playlist_.currentItem();
    if (!item) return;
    
    LOG_INFO("Loading track: {}", item->path.filename().string());
    player_->setSource(QUrl::fromLocalFile(QString::fromStdString(item->path.string())));
}

void AudioEngine::processAudioBuffer(const QAudioBuffer& buffer) {
    // LOG_TRACE("AudioEngine::processAudioBuffer called"); // Too frequent for INFO
    if (!buffer.isValid()) return;
    
    const auto format = buffer.format();
    const auto sampleRate = format.sampleRate();
    const auto channels = format.channelCount();
    const auto frameCount = buffer.frameCount();
    const auto totalSamples = frameCount * channels;
    
    // Zero-allocation: reuse scratch buffer
    if (scratchBuffer_.size() < totalSamples) {
        scratchBuffer_.resize(totalSamples);
    }
    
    // Convert to float samples (in-place)
    if (format.sampleFormat() == QAudioFormat::Float) {
        const f32* data = buffer.constData<f32>();
        std::copy(data, data + totalSamples, scratchBuffer_.begin());
    } 
    else if (format.sampleFormat() == QAudioFormat::Int16) {
        const i16* data = buffer.constData<i16>();
        for (usize i = 0; i < totalSamples; ++i) {
            scratchBuffer_[i] = static_cast<f32>(data[i]) / 32768.0f;
        }
    }
    else if (format.sampleFormat() == QAudioFormat::Int32) {
        const i32* data = buffer.constData<i32>();
        for (usize i = 0; i < totalSamples; ++i) {
            scratchBuffer_[i] = static_cast<f32>(data[i]) / 2147483648.0f;
        }
    }
    
    // Analyze audio
    currentSpectrum_ = analyzer_.analyze(scratchBuffer_, sampleRate, channels);
    spectrumUpdated.emitSignal(currentSpectrum_);
    
    // Feed to projectM if handle is set
    if (projectMHandle_) {
        u32 frames = frameCount;
        projectm_pcm_add_float(projectMHandle_, scratchBuffer_.data(), frames, 
                               channels == 2 ? PROJECTM_STEREO : PROJECTM_MONO);
    }
    
    // Emit PCM data for visualizer
    pcmReceived.emitSignal(scratchBuffer_, frameCount, channels, sampleRate);
}


void AudioEngine::setProjectMHandle(projectm_handle handle) {
    projectMHandle_ = handle;
    LOG_INFO("AudioEngine: projectM handle set to {}", (void*)handle);
}
} // namespace vc

#include "moc_AudioEngine.cpp"


