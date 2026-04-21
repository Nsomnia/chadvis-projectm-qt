#pragma once
#include <QAbstractListModel>
#include <QtQml/qqml.h>
#include <QStringList>
#include <QUrl>

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

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        PathRole,
        IsCurrentRole
    };

    explicit PlaylistBridge(QObject* parent = nullptr);
    static QObject* create(QQmlEngine*, QJSEngine*);
    static void setPlaylist(vc::Playlist* playlist);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;

public slots:
    Q_INVOKABLE void addFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void playAt(int index);

signals:
    void currentIndexChanged();
    void countChanged();

private:
    static vc::Playlist* s_playlist;
    static PlaylistBridge* s_instance;
};

} // namespace qml_bridge
