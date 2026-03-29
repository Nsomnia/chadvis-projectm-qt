/**
 * @file PlaylistBridge.hpp
 * @brief QML bridge for Playlist model - exposes playlist data to QML
 *
 * Single-responsibility: QML interface for playlist management
 * Implements QAbstractListModel for ListView binding
 *
 * @version 1.0.0
 */

#pragma once

#include <QAbstractListModel>
#include <QtQml/qqml.h>
#include <QStringList>
#include <QUrl>

namespace vc {
class AudioEngine;
class Playlist;
}

namespace qml_bridge {

/**
 * @brief QML bridge exposing Playlist as a model
 *
 * Provides:
 * - List model for QML ListView/GridView
 * - Playlist manipulation methods
 * - Current track tracking
 *
 * Roles exposed to QML:
 * - TitleRole: track title
 * - ArtistRole: artist name
 * - DurationRole: duration in ms
 * - PathRole: file path
 * - IsCurrentRole: is currently playing
 */
class PlaylistBridge : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QStringList supportedFormats READ supportedFormats CONSTANT)
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        DurationRole,
        DurationFormattedRole,
        PathRole,
        IsCurrentRole
    };

    explicit PlaylistBridge(QObject* parent = nullptr);
    ~PlaylistBridge() override = default;

    static PlaylistBridge* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    void setAudioEngine(vc::AudioEngine* engine);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;
    QStringList supportedFormats() const;
    int repeatMode() const;
    void setRepeatMode(int mode);

public slots:
    Q_INVOKABLE void addFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void addFolder(const QUrl& folderUrl);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void playAt(int index);
    Q_INVOKABLE void shuffle();
    Q_INVOKABLE QUrl getUrl(int index) const;
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE QString getItemPath(int index) const;
    Q_INVOKABLE void cycleRepeatMode();

signals:
    void currentIndexChanged();
    void countChanged();
    void repeatModeChanged();

private slots:
    void onPlaylistChanged();
    void onCurrentItemChanged(size_t index);

private:
    vc::AudioEngine* engine_{nullptr};
    int cachedCount_{0};
    int currentIndex_{-1};
};

} // namespace qml_bridge
