#include "ui/qml/PlaybackViewModel.hpp"

#include <algorithm>

#include <QMetaObject>
#include <QThread>

namespace vc::ui::qml {

PlaybackViewModel::PlaybackViewModel(AudioEngine* audioEngine, QObject* parent)
    : QObject(parent)
    , audioEngine_(audioEngine) {
    if (!audioEngine_) {
        return;
    }

    stateChangedConnection_ = audioEngine_->stateChanged.connect([this](PlaybackState) {
        invokeOnUiThread([this] { emit playingChanged(); });
    });

    positionChangedConnection_ = audioEngine_->positionChanged.connect([this](Duration position) {
        invokeOnUiThread([this, position] {
            positionMs_ = position.count();
            emit positionMsChanged();
        });
    });

    durationChangedConnection_ = audioEngine_->durationChanged.connect([this](Duration duration) {
        invokeOnUiThread([this, duration] {
            durationMs_ = duration.count();
            emit durationMsChanged();
        });
    });

    trackChangedConnection_ = audioEngine_->trackChanged.connect([this] {
        invokeOnUiThread([this] { refreshTrackInfo(); });
    });

    playlistChangedConnection_ = audioEngine_->playlist().changed.connect([this] {
        invokeOnUiThread([this] {
            refreshTrackInfo();
            refreshPlaybackModes();
        });
    });

    positionMs_ = audioEngine_->position().count();
    durationMs_ = audioEngine_->duration().count();
    refreshTrackInfo();
    refreshPlaybackModes();
}

PlaybackViewModel::~PlaybackViewModel() {
    if (!audioEngine_) {
        return;
    }

    if (stateChangedConnection_) {
        audioEngine_->stateChanged.disconnect(*stateChangedConnection_);
    }

    if (positionChangedConnection_) {
        audioEngine_->positionChanged.disconnect(*positionChangedConnection_);
    }

    if (durationChangedConnection_) {
        audioEngine_->durationChanged.disconnect(*durationChangedConnection_);
    }

    if (trackChangedConnection_) {
        audioEngine_->trackChanged.disconnect(*trackChangedConnection_);
    }

    if (playlistChangedConnection_) {
        audioEngine_->playlist().changed.disconnect(*playlistChangedConnection_);
    }
}

bool PlaybackViewModel::playing() const {
    return audioEngine_ && audioEngine_->state() == PlaybackState::Playing;
}

qint64 PlaybackViewModel::positionMs() const {
    return positionMs_;
}

qint64 PlaybackViewModel::durationMs() const {
    return durationMs_;
}

double PlaybackViewModel::volume() const {
    if (!audioEngine_) {
        return 0.0;
    }

    return static_cast<double>(audioEngine_->volume());
}

bool PlaybackViewModel::shuffleEnabled() const {
    if (!audioEngine_) {
        return false;
    }

    return audioEngine_->playlist().shuffle();
}

QString PlaybackViewModel::repeatModeLabel() const {
    if (!audioEngine_) {
        return QStringLiteral("Repeat Off");
    }

    return repeatModeToLabel(audioEngine_->playlist().repeatMode());
}

QString PlaybackViewModel::trackTitle() const {
    return trackTitle_;
}

QString PlaybackViewModel::trackArtist() const {
    return trackArtist_;
}

void PlaybackViewModel::playPause() {
    if (!audioEngine_) {
        return;
    }

    audioEngine_->togglePlayPause();
}

void PlaybackViewModel::play() {
    if (audioEngine_) {
        audioEngine_->play();
    }
}

void PlaybackViewModel::pause() {
    if (audioEngine_) {
        audioEngine_->pause();
    }
}

void PlaybackViewModel::stop() {
    if (audioEngine_) {
        audioEngine_->stop();
    }
}

void PlaybackViewModel::next() {
    if (!audioEngine_) {
        return;
    }

    audioEngine_->playlist().next();
}

void PlaybackViewModel::previous() {
    if (!audioEngine_) {
        return;
    }

    audioEngine_->playlist().previous();
}

void PlaybackViewModel::seek(qint64 positionMs) {
    if (!audioEngine_) {
        return;
    }

    audioEngine_->seek(Duration(positionMs));
}

void PlaybackViewModel::cycleRepeatMode() {
    if (!audioEngine_) {
        return;
    }

    audioEngine_->playlist().cycleRepeatMode();
    emit repeatModeLabelChanged();
}

void PlaybackViewModel::setVolume(double volume) {
    if (!audioEngine_) {
        return;
    }

    const auto clamped = std::clamp(volume, 0.0, 1.0);
    audioEngine_->setVolume(static_cast<f32>(clamped));
    emit volumeChanged();
}

void PlaybackViewModel::setShuffleEnabled(bool enabled) {
    if (!audioEngine_) {
        return;
    }

    if (audioEngine_->playlist().shuffle() == enabled) {
        return;
    }

    audioEngine_->playlist().setShuffle(enabled);
    emit shuffleEnabledChanged();
}

void PlaybackViewModel::refreshTrackInfo() {
    if (!audioEngine_) {
        return;
    }

    const auto* item = audioEngine_->playlist().currentItem();
    const QString previousTitle = trackTitle_;
    const QString previousArtist = trackArtist_;

    if (!item) {
        trackTitle_ = QStringLiteral("No track selected");
        trackArtist_ = QStringLiteral("Add local files or play from Suno");
    } else {
        if (!item->metadata.title.empty()) {
            trackTitle_ = QString::fromStdString(item->metadata.title);
        } else if (item->isRemote) {
            trackTitle_ = QStringLiteral("Remote stream");
        } else {
            trackTitle_ = QString::fromStdString(item->path.stem().string());
        }

        if (!item->metadata.artist.empty()) {
            trackArtist_ = QString::fromStdString(item->metadata.artist);
        } else if (item->isRemote) {
            trackArtist_ = QStringLiteral("Suno Remote Source");
        } else {
            trackArtist_ = QStringLiteral("Local Library");
        }
    }

    if (previousTitle != trackTitle_ || previousArtist != trackArtist_) {
        emit trackInfoChanged();
    }
}

void PlaybackViewModel::refreshPlaybackModes() {
    emit shuffleEnabledChanged();
    emit repeatModeLabelChanged();
}

void PlaybackViewModel::invokeOnUiThread(std::function<void()> fn) {
    if (!fn) {
        return;
    }

    if (QThread::currentThread() == thread()) {
        fn();
        return;
    }

    QMetaObject::invokeMethod(this, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
}

QString PlaybackViewModel::repeatModeToLabel(RepeatMode mode) {
    switch (mode) {
    case RepeatMode::Off:
        return QStringLiteral("Repeat Off");
    case RepeatMode::One:
        return QStringLiteral("Repeat One");
    case RepeatMode::All:
        return QStringLiteral("Repeat All");
    }

    return QStringLiteral("Repeat Off");
}

} // namespace vc::ui::qml
