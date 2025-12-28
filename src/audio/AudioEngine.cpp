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
    
    LOG_INFO("Audio engine initialized");
    return Result<void>::ok();
}

void AudioEngine::play() {
    LOG_INFO("AudioEngine::play() CALLED");
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
    switch (state) {
        case QMediaPlayer::StoppedState:
            state_ = PlaybackState::Stopped;
            break;
        case QMediaPlayer::PlayingState:
            state_ = PlaybackState::Playing;
            break;
        case QMediaPlayer::PausedState:
            state_ = PlaybackState::Paused;
            break;
    }
    
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
    error.emitSignal(errorString.toStdString());
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
    if (!buffer.isValid()) return;
    
    const auto format = buffer.format();
    const auto sampleRate = format.sampleRate();
    const auto channels = format.channelCount();
    
    // Convert to float samples
    std::vector<f32> samples;
    
    if (format.sampleFormat() == QAudioFormat::Float) {
        const f32* data = buffer.constData<f32>();
        samples.assign(data, data + buffer.frameCount() * channels);
    } 
    else if (format.sampleFormat() == QAudioFormat::Int16) {
        const i16* data = buffer.constData<i16>();
        samples.resize(buffer.frameCount() * channels);
        for (usize i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<f32>(data[i]) / 32768.0f;
        }
    }
    else if (format.sampleFormat() == QAudioFormat::Int32) {
        const i32* data = buffer.constData<i32>();
        samples.resize(buffer.frameCount() * channels);
        for (usize i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<f32>(data[i]) / 2147483648.0f;
        }
    }
    
    // Analyze audio
    currentSpectrum_ = analyzer_.analyze(samples, sampleRate, channels);
    spectrumUpdated.emitSignal(currentSpectrum_);
    
    // Emit PCM data for visualizer
    LOG_DEBUG("AudioEngine: Emitting {} samples ({} frames) to pcmReceived signal", samples.size(), samples.size() / 2);
    pcmReceived.emitSignal(samples, samples.size() / 2, 2, sampleRate);
}

} // namespace vc

#include "moc_AudioEngine.cpp"

