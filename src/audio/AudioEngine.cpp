#include "AudioEngine.hpp"
#include "VisualizerAudioTap.hpp"
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

    // The visualizer tap is a fallback, not a second audio path. It exists
    // because QAudioBufferOutput is only implemented by Qt's FFmpeg media
    // backend, and a multimedia build with only the native backend (this
    // machine's Homebrew Qt ships libdarwinmediaplugin and no FFmpeg plugin)
    // plays audio while emitting zero buffers. The tick checks the observation,
    // not the platform: the first delivered buffer — if one ever comes — turns
    // the tap off for the rest of the session.
    vizTap_ = std::make_unique<VisualizerAudioTap>(audioQueue_);
    vizTapTimer_.setInterval(40);
    connect(&vizTapTimer_, &QTimer::timeout, this, &AudioEngine::onVizTapTick);

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
    observedSinkRate_.store(0, std::memory_order_relaxed);
    audioConfig_ = config;
    // Counters are per-configuration, not per-process. A new device or window is
    // the user answering a previous complaint, so the next fault must be able to
    // be "the first" again -- both so the log re-states it and so a consumer that
    // cleared the message has it handed back.
    oversizedBufferCount_.store(0, std::memory_order_relaxed);
    formatErrorCount_.store(0, std::memory_order_relaxed);
    conversionFaulted_.store(false, std::memory_order_relaxed);

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
    observedSinkRate_.store(0, std::memory_order_relaxed);
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
    observedSinkRate_.store(0, std::memory_order_relaxed);
    refreshOutputStatus();
}

void AudioEngine::postStatusRefresh() {
    // The observed rate is published as an atomic; the sentence describing it is
    // not. Building it on the audio callback thread would assign a QString there
    // while the GUI thread reads it, which is a torn multi-word read rather than
    // a stale value (T0154).
    QMetaObject::invokeMethod(this, [this] { refreshOutputStatus(); },
                              Qt::QueuedConnection);
}

void AudioEngine::postErrorSignal(std::string message) {
    // The queued functor is the only thing that allocates here, and the event is
    // posted to this object's thread -- so a fault found on the sink callback
    // thread reaches SettingsBridge on the GUI thread without a QML singleton
    // being written from an audio thread (T0155).
    QMetaObject::invokeMethod(this,
                              [this, message = std::move(message)] {
                                  emit errorSignal(message);
                              },
                              Qt::QueuedConnection);
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
    status += observedSinkRate_.load(std::memory_order_relaxed) > 0
                  ? QStringLiteral(" · sink: %1 Hz").arg(
                        observedSinkRate_.load(std::memory_order_relaxed))
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
    // The tap only has work while audio is moving, and only on a backend that
    // delivers no buffer output. Driving the timer from the transport rather
    // than from init() keeps a paused or idle app from waking 25 times a second.
    if (state_ == PlaybackState::Playing && !bufferOutputDelivered_) {
        if (!vizTapTimer_.isActive()) vizTapTimer_.start();
    } else {
        vizTapTimer_.stop();
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
    if (sender() != bufferOutput_.get()) return;

    if (!bufferOutputDelivered_) {
        bufferOutputDelivered_ = true;
        // The supported path works on this backend, so the fallback decoder has
        // no job left. Said once, because the alternative -- silently running
        // both producers into one SPSC queue -- is a data race with audio. The
        // timer stops here rather than idling at 25 wakeups a second for the
        // rest of the process.
        vizTapTimer_.stop();
        if (vizTap_ && vizTap_->isActive()) {
            vizTap_->clearSource();
            LOG_INFO("AudioEngine: the media backend delivers audio buffers; the visualizer "
                     "tap is off");
        }
    }
    processAudioBuffer(buffer);
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

void AudioEngine::onVizTapTick() {
    if (!vizTap_ || bufferOutputDelivered_) return;
    if (state_ != PlaybackState::Playing) return;
    vizTap_->pump(player_->position());
}

void AudioEngine::loadCurrentTrack() {
    const auto item = playlist_.currentItem();
    if (!item) return;
    QUrl source = item->isRemote ? QUrl(QString::fromStdString(item->url)) : QUrl::fromLocalFile(QString::fromStdString(item->path.string()));
    if (player_->source() != source) player_->setSource(source);
    if (vizTap_) {
        // Only a local file can be decoded here. A remote entry reaches the
        // queue through DownloadQueue, which hands playback a local path.
        if (item->isRemote) vizTap_->clearSource();
        else vizTap_->setSource(item->path);
    }
    prepareNextTrack();
}

void AudioEngine::prepareNextTrack() {
    // T0172. This used to derive `currentIndex() + 1` — the LINEAR successor —
    // while shuffle was active, whose traversal order is the permutation. On
    // EndOfMedia the player holding this pre-loaded source is swapped in as
    // the active one, so the wrong pre-buffer did not merely cost a warm-up:
    // it made the WRONG TRACK play. peekNextIndex() mirrors next() exactly and
    // answers nullopt at the one point it cannot know (the shuffle wrap under
    // Repeat::All regenerates the permutation), where clearing the pre-load
    // takes the non-swap advance path instead.
    const auto nextIndex = playlist_.peekNextIndex();
    const auto nextItem = nextIndex ? playlist_.itemAt(*nextIndex)
                                   : std::optional<PlaylistItem>{};
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
    if (observedSinkRate_.exchange(sinkRate, std::memory_order_relaxed) != sinkRate) {
        // This runs on the sink's callback thread, so the status sentence is
        // rebuilt there only via a queued invocation (T0154). The exchange also
        // means a repeated rate posts nothing, so the queued half fires once per
        // actual change.
        postStatusRefresh();
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

    if (conversionFaulted_.exchange(false, std::memory_order_relaxed)) {
        // One empty string is the documented recovery signal; without it a fault
        // would be the last thing the user ever sees about their output. Posted,
        // never emitted here: this is the sink callback thread (T0155).
        postErrorSignal({});
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
    conversionFaulted_.store(true, std::memory_order_relaxed);
    if (count != 1 && count % kConversionErrorReportInterval != 0) {
        return;
    }
    // This is the sink's real-time callback thread: FormatError::describe() and
    // LOG_ERROR are an allocation and a lock, so both belong to the queued half.
    // The payload is POD plus a string literal, so capturing it is a copy.
    QMetaObject::invokeMethod(this,
                              [this, error, count, kind] {
                                  const std::string detail = error.describe();
                                  LOG_ERROR("AudioEngine: {} audio callback dropped ({}/{}) -- {}",
                                            kind, count, interleavedWindow(), detail);
                                  emit errorSignal(detail);
                              },
                              Qt::QueuedConnection);
}

} // namespace vc
