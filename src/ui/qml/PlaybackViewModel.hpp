#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "audio/AudioEngine.hpp"

namespace vc {
class AudioEngine;
}

namespace vc::ui::qml {

class PlaybackViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffleEnabled READ shuffleEnabled WRITE setShuffleEnabled NOTIFY shuffleEnabledChanged)
    Q_PROPERTY(QString repeatModeLabel READ repeatModeLabel NOTIFY repeatModeLabelChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackInfoChanged)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY trackInfoChanged)

public:
    explicit PlaybackViewModel(AudioEngine* audioEngine, QObject* parent = nullptr);
    ~PlaybackViewModel() override;

    bool playing() const;
    qint64 positionMs() const;
    qint64 durationMs() const;
    double volume() const;
    bool shuffleEnabled() const;
    QString repeatModeLabel() const;
    QString trackTitle() const;
    QString trackArtist() const;

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void cycleRepeatMode();

public slots:
    void setVolume(double volume);
    void setShuffleEnabled(bool enabled);

signals:
    void playingChanged();
    void positionMsChanged();
    void durationMsChanged();
    void volumeChanged();
    void shuffleEnabledChanged();
    void repeatModeLabelChanged();
    void trackInfoChanged();

private:
    void refreshTrackInfo();
    void refreshPlaybackModes();
    void invokeOnUiThread(std::function<void()> fn);

    static QString repeatModeToLabel(RepeatMode mode);

    AudioEngine* audioEngine_{nullptr};

    qint64 positionMs_{0};
    qint64 durationMs_{0};
    QString trackTitle_{"No track"};
    QString trackArtist_{"Suno Remote + Local Library"};

    std::optional<Signal<PlaybackState>::SlotId> stateChangedConnection_;
    std::optional<Signal<Duration>::SlotId> positionChangedConnection_;
    std::optional<Signal<Duration>::SlotId> durationChangedConnection_;
    std::optional<Signal<>::SlotId> trackChangedConnection_;
    std::optional<Signal<>::SlotId> playlistChangedConnection_;
};

} // namespace vc::ui::qml
