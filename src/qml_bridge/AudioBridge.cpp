/**
 * @file AudioBridge.cpp
 * @brief Implementation of AudioBridge QML singleton
 */

#include "AudioBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "audio/MediaMetadata.hpp"
#include "core/Application.hpp"
#include <QQmlEngine>

namespace qml_bridge {

AudioBridge::AudioBridge(QObject* parent)
    : QObject(parent)
{
    if (vc::Application::instance()) {
        s_engine = vc::Application::instance()->audioEngine();
    }

    if (s_engine) {
        connect(s_engine, &vc::AudioEngine::stateChanged,
                this, &AudioBridge::onEngineStateChanged, Qt::QueuedConnection);

        connect(s_engine, &vc::AudioEngine::positionChanged,
                this, &AudioBridge::onEnginePositionChanged, Qt::QueuedConnection);

        connect(s_engine, &vc::AudioEngine::durationChanged,
                this, &AudioBridge::onEngineDurationChanged, Qt::QueuedConnection);

        connect(s_engine, &vc::AudioEngine::trackChanged,
                this, &AudioBridge::onEngineTrackChanged, Qt::QueuedConnection);

        volume_ = s_engine->volume();
        position_ = s_engine->position().count();
        duration_ = s_engine->duration().count();
    }
}

int AudioBridge::playbackState() const
{
    return playbackState_;
}

qint64 AudioBridge::position() const
{
    return position_;
}

qint64 AudioBridge::duration() const
{
    return duration_;
}

qreal AudioBridge::volume() const
{
    return volume_;
}

bool AudioBridge::isPlaying() const
{
    return playbackState_ == Playing;
}

QVariantMap AudioBridge::currentTrack() const
{
    return currentTrack_;
}

void AudioBridge::play()
{
    if (s_engine) s_engine->play();
}

void AudioBridge::pause()
{
    if (s_engine) s_engine->pause();
}

void AudioBridge::stop()
{
    if (s_engine) s_engine->stop();
}

void AudioBridge::togglePlayPause()
{
    if (s_engine) s_engine->togglePlayPause();
}

void AudioBridge::seek(qint64 positionMs)
{
    if (s_engine) s_engine->seek(std::chrono::milliseconds(positionMs));
}

void AudioBridge::next()
{
    if (s_engine) s_engine->playlist().next();
}

void AudioBridge::previous()
{
    if (s_engine) s_engine->playlist().previous();
}

void AudioBridge::setVolume(qreal volume)
{
    if (qFuzzyCompare(volume_, volume)) return;

    volume_ = qBound(0.0, volume, 1.0);
    if (s_engine) s_engine->setVolume(static_cast<float>(volume_));
    emit volumeChanged();
}

bool AudioBridge::loadFile(const QString& filePath)
{
    if (!s_engine) return false;

    auto path = vc::fs::path(filePath.toStdString());
    if (!vc::fs::exists(path)) return false;

    s_engine->playlist().clear();
    s_engine->playlist().addFile(path);
    s_engine->play();
    return true;
}

void AudioBridge::addToPlaylist(const QString& filePath)
{
    if (!s_engine) return;

    auto path = vc::fs::path(filePath.toStdString());
    if (vc::fs::exists(path)) {
        s_engine->playlist().addFile(path);
    }
}

void AudioBridge::clearPlaylist()
{
    if (s_engine) {
        s_engine->playlist().clear();
    }
}

int AudioBridge::playlistCount() const
{
    return s_engine ? static_cast<int>(s_engine->playlist().items().size()) : 0;
}

QVariantMap AudioBridge::playlistItem(int index) const
{
    QVariantMap result;
    if (!s_engine || index < 0) return result;

    const auto& items = s_engine->playlist().items();
    if (static_cast<size_t>(index) < items.size()) {
        const auto& item = items[index];
        result["title"] = QString::fromStdString(item.metadata.displayTitle());
        result["artist"] = QString::fromStdString(item.metadata.displayArtist());
        result["album"] = QString::fromStdString(item.metadata.album);
        result["duration"] = static_cast<qint64>(item.metadata.duration.count());
        result["path"] = QString::fromStdString(item.path.string());
    }
    return result;
}

void AudioBridge::onEngineStateChanged(vc::PlaybackState state)
{
    int newState = Stopped;
    switch (state) {
        case vc::PlaybackState::Playing: newState = Playing; break;
        case vc::PlaybackState::Paused: newState = Paused; break;
        default: newState = Stopped; break;
    }

    if (playbackState_ != newState) {
        playbackState_ = newState;
        emit playbackStateChanged();
    }
}

void AudioBridge::onEnginePositionChanged(std::chrono::milliseconds pos)
{
    position_ = pos.count();
    emit positionChanged();
}

void AudioBridge::onEngineDurationChanged(std::chrono::milliseconds dur)
{
    duration_ = dur.count();
    emit durationChanged();
}

void AudioBridge::onEngineTrackChanged()
{
    currentTrack_.clear();

    if (s_engine) {
        if (auto* item = s_engine->playlist().currentItem()) {
            const auto& meta = item->metadata;
            currentTrack_["title"] = QString::fromStdString(meta.displayTitle());
            currentTrack_["artist"] = QString::fromStdString(meta.displayArtist());
            currentTrack_["album"] = QString::fromStdString(meta.album);
            currentTrack_["duration"] = static_cast<qint64>(meta.duration.count());
            currentTrack_["path"] = QString::fromStdString(item->path.string());
        }
    }

    emit trackChanged();
}

} // namespace qml_bridge
