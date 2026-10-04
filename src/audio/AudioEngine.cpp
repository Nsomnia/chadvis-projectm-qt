#include "AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "util/FileUtils.hpp"

// QAudioDevice is only forward-declared by qaudiooutput.h; QMediaDevices does
// not pull the definition in, and Result<QAudioDevice> needs it complete.
#include <QAudioDevice>
#include <QMediaDevices>

#include <QUrl>

#include <algorithm>

namespace vc {

namespace {

/// Resolves a configured device name to a real output device.
///
/// "" and "default" mean the system default. Any other name must match an
/// output device's description **exactly**: a partial match would silently
/// switch a user's output to a different device that happens to share a prefix,
/// and a name that matches nothing is a reported failure rather than a quiet
/// fall back. Two devices with identical descriptions resolve to the first,
/// because Qt exposes no other way to tell them apart from a name.
Result<QAudioDevice> resolveOutputDevice(std::string_view name) {
    if (name.empty() || name == AudioEngine::kDefaultDeviceName) {
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull()) {
            return Result<QAudioDevice>::err(
                "no audio output device is available on this system");
        }
        return Result<QAudioDevice>::ok(device);
    }

    const auto outputs = QMediaDevices::audioOutputs();
    for (const auto& candidate : outputs) {
        if (candidate.description().toStdString() == name) {
            return Result<QAudioDevice>::ok(candidate);
        }
    }

    std::string available;
    for (const auto& candidate : outputs) {
        if (!available.empty()) available += ", ";
        available += candidate.description().toStdString();
    }
    return Result<QAudioDevice>::err("configured audio device \"" + std::string(name) +
                                     "\" is not available; available devices: " +
                                     (available.empty() ? "none" : available));
}

} // namespace

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

Result<void> AudioEngine::init(std::optional<AudioConfig> config) {
    // See the header: the default argument reads the Config singleton because
    // Application::init() cannot pass it. This is the change that makes
    // audio.device / audio.bufferSize / audio.sampleRate -- and the three CLI
    // overrides -- mean anything at all.
    const AudioConfig requested = config.value_or(Config::instance().audio());

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

    setupDeviceWatchers();

    // Sized before applyAudioConfig(), which can fail on an unresolvable device
    // and return early. Without this the window would stay zero-length, every
    // callback would be refused as oversized, and a bad device name would take
    // playback down with it instead of merely being ignored.
    scratchBuffer_.resize(pcm::conversionWindow(requested.bufferSize));

    if (auto applied = applyAudioConfig(requested); !applied) {
        // Not fatal. A saved device name whose device has been unplugged is the
        // common case, and refusing to start the player would be a worse answer
        // than starting on the system default and saying so.
        LOG_WARN("AudioEngine: {}; continuing on the system default output",
                 applied.error().message);
        // Both QAudioOutputs are default-constructed on the system default, so
        // that is what audioConfig() must report: SettingsBridge reverts a
        // rejected device to this value, and a half-defaulted struct would make
        // the rate and buffer look unapplied too when the window was sized above.
        audioConfig_ = requested;
        audioConfig_.device = std::string(kDefaultDeviceName);
        refreshOutputStatus();
        emit errorSignal(applied.error().message);
    }

    // Playlist signals
    playlist_.currentChanged.connect([this](std::optional<usize> index) { onPlaylistCurrentChanged(index); });
    playlist_.changed.connect([this] { sessionSaveTimer_.start(); });

    loadLastPlaylist();

    LOG_INFO("Audio engine initialized with QAudioBufferOutput ({})",
             outputStatus_.toStdString());
    return Result<void>::ok();
}

Result<void> AudioEngine::applyAudioConfig(const AudioConfig& config) {
    if (!audioOutput_ || !nextAudioOutput_) {
        return Result<void>::err("audio engine is not initialised");
    }

    auto device = resolveOutputDevice(config.device);
    if (!device) {
        // Still refresh: a failure during init happens long before any QML is
        // connected, and the status line is where a caller attaching later will
        // read what actually happened. deviceDescription_ keeps whatever is
        // really open, which on the first init is nothing.
        refreshOutputStatus();
        return Result<void>::err(device.error());
    }
    const auto& sink = device.value();

    // audio.bufferSize is a sample count, and Qt 6 gives no device-level
    // equivalent, so it sizes the engine's per-callback conversion window.
    // Clamped rather than obeyed, because a window below one platform callback
    // would drop audio; the clamp is reported in the status line so the UI never
    // claims a value the engine is not using.
    const usize window = pcm::conversionWindow(config.bufferSize);
    scratchBuffer_.resize(window);

    // Assigned before setDevice(): setDevice emits deviceChanged synchronously,
    // which lands in onOutputDeviceChanged and rebuilds the status. With this
    // order that intermediate emission is already consistent with the new config
    // rather than describing the new device with the old sample rate.
    deviceDescription_ = sink.description();
    deviceMinRate_ = sink.minimumSampleRate();
    deviceMaxRate_ = sink.maximumSampleRate();
    observedSinkRate_ = 0;
    audioConfig_ = config;
    // Counters are per-configuration, not per-process. A new device or window is
    // the user answering a previous complaint, so the next fault must be able to
    // be "the first" again -- both so the log re-states it and so a consumer that
    // cleared the message has it handed back.
    oversizedBufferCount_ = 0;
    formatErrorCount_ = 0;
    conversionFaulted_ = false;

    audioOutput_->setDevice(sink);
    nextAudioOutput_->setDevice(sink);

    // Qt reports the sink's rate range as int while the config carries u32, and
    // comparing the two directly converts the int to unsigned -- so a device
    // reporting a negative minimum would become a huge bound and the check
    // would silently pass. Clamp both to a non-negative u32 first, then compare.
    const auto sinkMinRate = static_cast<u32>(std::max(deviceMinRate_, 0));
    const auto sinkMaxRate = static_cast<u32>(std::max(deviceMaxRate_, 0));

    if (config.sampleRate != 0 && sinkMaxRate > 0 &&
        (config.sampleRate < sinkMinRate || config.sampleRate > sinkMaxRate)) {
        // Warned, not refused: QMediaPlayer negotiates the sink format with the
        // source, so an out-of-range preference is resampled rather than fatal.
        // Silence about it is what made the setting look broken.
        LOG_WARN("AudioEngine: requested {} Hz but \"{}\" reports {}-{} Hz",
                 config.sampleRate, deviceDescription_.toStdString(), deviceMinRate_,
                 deviceMaxRate_);
    }

    refreshOutputStatus();
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

void AudioEngine::setupDeviceWatchers() {
    // Connected once, here, and deliberately not from setupConnections(): that is
    // re-run by swapPlayers() for the next player, and re-connecting the outputs
    // there would double every device report.
    connect(audioOutput_.get(), &QAudioOutput::deviceChanged,
            this, &AudioEngine::onOutputDeviceChanged);
    connect(nextAudioOutput_.get(), &QAudioOutput::deviceChanged,
            this, &AudioEngine::onOutputDeviceChanged);

    // Qt 6.11 has no QAudioOutput::errorChanged -- QAudioOutput was reduced to a
    // device+volume holder and the error signal went with start()/stop(). What
    // remains is the two signals that do exist and do fire on a real device
    // change: the output's own deviceChanged (a platform handover, an unplug the
    // backend absorbs) and QMediaDevices::audioOutputsChanged (hot-plug). Both
    // reach the UI through errorSignal()/audioOutputStatusChanged().
    mediaDevices_ = std::make_unique<QMediaDevices>();
    connect(mediaDevices_.get(), &QMediaDevices::audioOutputsChanged,
            this, &AudioEngine::onAudioOutputsChanged);
}

void AudioEngine::onOutputDeviceChanged() {
    const QAudioDevice device = audioOutput_ ? audioOutput_->device() : QAudioDevice{};
    if (device.isNull()) {
        LOG_ERROR("AudioEngine: output device was cleared by the platform");
        emit errorSignal("the audio output device was removed or could not be opened");
        deviceDescription_.clear();
        deviceMinRate_ = 0;
        deviceMaxRate_ = 0;
    } else {
        LOG_INFO("AudioEngine: output moved to \"{}\"", device.description().toStdString());
        deviceDescription_ = device.description();
        deviceMinRate_ = device.minimumSampleRate();
        deviceMaxRate_ = device.maximumSampleRate();
    }
    observedSinkRate_ = 0;
    refreshOutputStatus();
}

void AudioEngine::onAudioOutputsChanged() {
    LOG_DEBUG("AudioEngine: audio output list changed");
    reapplyAfterDeviceListChange();
}

void AudioEngine::reapplyAfterDeviceListChange() {
    // Hot-plug. If the configured device is gone, say so and move to the system
    // default. The persisted name is deliberately left alone: rewriting it would
    // make the setting silently disagree with what the user chose, and the
    // SettingsBridge already reports the mismatch.
    auto resolved = resolveOutputDevice(audioConfig_.device);
    if (!resolved) {
        LOG_WARN("AudioEngine: {}", resolved.error().message);
        emit errorSignal(resolved.error().message);
        auto fallback = resolveOutputDevice(kDefaultDeviceName);
        if (fallback) {
            audioOutput_->setDevice(fallback.value());
            nextAudioOutput_->setDevice(fallback.value());
            deviceDescription_ = fallback.value().description();
            deviceMinRate_ = fallback.value().minimumSampleRate();
            deviceMaxRate_ = fallback.value().maximumSampleRate();
            LOG_INFO("AudioEngine: using system default output \"{}\"",
                     deviceDescription_.toStdString());
        }
    }
    observedSinkRate_ = 0;
    refreshOutputStatus();
}

void AudioEngine::refreshOutputStatus() {
    QString status = QStringLiteral("Output: %1").arg(
        deviceDescription_.isEmpty() ? QStringLiteral("none") : deviceDescription_);
    status += QStringLiteral(" · window: %1 samples").arg(interleavedWindow());
    if (audioConfig_.sampleRate != 0) {
        status += QStringLiteral(" · requested: %1 Hz").arg(audioConfig_.sampleRate);
    }
    if (deviceMaxRate_ > 0) {
        status += QStringLiteral(" (device %1-%2 Hz)").arg(deviceMinRate_).arg(deviceMaxRate_);
    }
    status += observedSinkRate_ > 0
                  ? QStringLiteral(" · sink: %1 Hz").arg(observedSinkRate_)
                  : QStringLiteral(" · sink: not yet observed");

    if (status == outputStatus_) {
        return;
    }
    outputStatus_ = status;
    emit audioOutputStatusChanged(outputStatus_);
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
    const usize channels = static_cast<usize>(format.channelCount());
    const usize totalSamples = static_cast<usize>(buffer.sampleCount());
    if (channels == 0 || totalSamples == 0) return;

    // The sink format is negotiated by the platform with the source, so the rate
    // is observed rather than requested. Recording the first one is what turns
    // audio.sampleRate from a setting with no observable effect into a setting
    // whose outcome the user can be shown.
    const auto sinkRate = static_cast<int>(format.sampleRate());
    if (sinkRate != observedSinkRate_) {
        observedSinkRate_ = sinkRate;
        refreshOutputStatus();
    }

    // Read the buffer as raw bytes and let vc::pcm decide the width. The old code
    // indexed scratchBuffer_ with a width implied by an if/else-if chain with no
    // else, so an Int32 sink pushed the previous buffer's floats into both
    // consumer queues -- stale audio, no log, no signal, no UI.
    const std::span<const std::byte> source(buffer.constData<std::byte>(),
                                            static_cast<usize>(buffer.byteCount()));
    const std::span<f32> destination(scratchBuffer_.data(), scratchBuffer_.size());

    auto converted = pcm::convertInterleaved(source, destination, format.sampleFormat());
    if (!converted) {
        const auto& error = converted.error();
        switch (error.reason) {
        case pcm::ConversionError::DestinationTooSmall:
        case pcm::ConversionError::TooManySamples:
            ++oversizedBufferCount_;
            reportConversionFailure(error, oversizedBufferCount_, "oversized");
            break;
        default:
            ++formatErrorCount_;
            reportConversionFailure(error, formatErrorCount_, "undecodable format");
            break;
        }
        return;
    }

    if (conversionFaulted_) {
        // One empty string is the documented recovery signal; without it a fault
        // would be the last thing the user ever sees about their output.
        conversionFaulted_ = false;
        emit errorSignal(std::string());
    }

    // The queue view is measured from the byte count the conversion actually
    // consumed, not from QAudioBuffer::sampleCount(). For a buffer whose byte
    // count and declared width disagree, the two differ, and taking the larger
    // one would hand the queues scratch samples this callback never wrote.
    const AudioChunk chunk{
        .samples = std::span<const f32>(scratchBuffer_.data(),
                                        pcm::interleavedSampleCount(source.size(),
                                                                    format.sampleFormat())),
        .channels = static_cast<u32>(channels),
        .sampleRate = static_cast<u32>(format.sampleRate()),
    };
    audioQueue_.pushAll(chunk);
    // Deliberately no PCM signal here. The old `pcmReceived` had zero receivers
    // repo-wide and copied the whole scratch buffer per audio callback for
    // nobody; consumers that want PCM read it from the AudioQueue, which is
    // already the single copy.
}

void AudioEngine::reportConversionFailure(const pcm::FormatError& error, u64 count,
                                          const char* kind) {
    conversionFaulted_ = true;
    if (count != 1 && count % kConversionErrorReportInterval != 0) {
        return;
    }
    LOG_ERROR("AudioEngine: {} audio callback dropped ({}/{}) -- {}", kind, count,
              interleavedWindow(), error.describe());
    emit errorSignal(error.describe());
}

} // namespace vc
