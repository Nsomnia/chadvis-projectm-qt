#pragma once
#include <QAbstractListModel>
#include <QtQml/qqml.h>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

namespace vc {
class Playlist;
}

namespace qml_bridge {

class PlaylistBridge : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY repeatModeChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        PathRole,
        DurationFormattedRole,
        IsCurrentRole
    };

    explicit PlaylistBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setPlaylist(vc::Playlist* playlist);
    static void connectPlaylistSignals();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;
    bool shuffle() const;
    Q_INVOKABLE int repeatMode() const;

public slots:
    Q_INVOKABLE void addFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void playAt(int index);
  Q_INVOKABLE void toggleShuffle();
  Q_INVOKABLE void setShuffle(bool enabled);
    Q_INVOKABLE void cycleRepeatMode();
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE QString getItemPath(int idx) const;

signals:
    void currentIndexChanged();
    void countChanged();
    void shuffleChanged();
    void repeatModeChanged();

private slots:
    void onPlaylistChanged();
    void onPlaylistCurrentChanged(std::size_t index);

private:

    static vc::Playlist* s_playlist;
    static PlaylistBridge* s_instance;
    static vc::Playlist* s_connectedPlaylist;
    static std::optional<std::size_t> s_changedConnection;
    static std::optional<std::size_t> s_currentChangedConnection;
    static bool s_suppressPlaylistNotifications;
};

} // namespace qml_bridge
