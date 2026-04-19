/**
 * @file AudioBridge.hpp
 * @brief QML bridge for AudioEngine - exposes audio control to QML
 *
 * Single-responsibility: QML interface for audio playback
 * Uses QML_SINGLETON for global access without instantiation
 *
 * @version 1.0.0
 */

#pragma once

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantMap>
#include <chrono>

namespace vc {
class AudioEngine;
enum class PlaybackState;
}

namespace qml_bridge {

/**
 * @brief QML bridge exposing AudioEngine to QML
 *
 * Usage in QML:
 * @code
 * AudioBridge.play()
 * AudioBridge.pause()
 * AudioBridge.volume = 0.5
 * text: AudioBridge.currentTrack.title
 * @endcode
 */
class AudioBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(QVariantMap currentTrack READ currentTrack NOTIFY trackChanged)

public:
    enum PlaybackStateEnum { Stopped = 0, Playing = 1, Paused = 2 };
    Q_ENUM(PlaybackStateEnum)

    explicit AudioBridge(QObject* parent = nullptr);
    ~AudioBridge() override = default;

    int playbackState() const;
    qint64 position() const;
    qint64 duration() const;
    qreal volume() const;
    bool isPlaying() const;
    QVariantMap currentTrack() const;

public slots:
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void setVolume(qreal volume);
    Q_INVOKABLE bool loadFile(const QString& filePath);
    Q_INVOKABLE void addToPlaylist(const QString& filePath);
    Q_INVOKABLE void clearPlaylist();
    Q_INVOKABLE int playlistCount() const;
    Q_INVOKABLE QVariantMap playlistItem(int index) const;

signals:
    void playbackStateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void trackChanged();

private slots:
    void onEngineStateChanged(vc::PlaybackState state);
    void onEnginePositionChanged(std::chrono::milliseconds pos);
    void onEngineDurationChanged(std::chrono::milliseconds dur);
    void onEngineTrackChanged();

private:
    vc::AudioEngine* s_engine{nullptr};

    QVariantMap currentTrack_;
    qint64 position_{0};
    qint64 duration_{0};
    qreal volume_{1.0};
    int playbackState_{Stopped};
};

} // namespace qml_bridge
