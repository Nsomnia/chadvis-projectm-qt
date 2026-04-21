#include "PlaylistBridge.hpp"
#include "audio/Playlist.hpp"
#include <QQmlEngine>

namespace qml_bridge {

vc::Playlist* PlaylistBridge::s_playlist = nullptr;
PlaylistBridge* PlaylistBridge::s_instance = nullptr;

PlaylistBridge::PlaylistBridge(QObject* parent) : QAbstractListModel(parent) {
    s_instance = this;
}

QObject* PlaylistBridge::create(QQmlEngine*, QJSEngine*) {
    return new PlaylistBridge();
}

void PlaylistBridge::setPlaylist(vc::Playlist* playlist) {
    s_playlist = playlist;
}

int PlaylistBridge::rowCount(const QModelIndex&) const {
    return s_playlist ? static_cast<int>(s_playlist->items().size()) : 0;
}

QVariant PlaylistBridge::data(const QModelIndex& index, int role) const {
    if (!s_playlist || !index.isValid()) return QVariant();
    const auto& items = s_playlist->items();
    if (index.row() >= static_cast<int>(items.size())) return QVariant();
    
    const auto& item = items[index.row()];
    switch (role) {
        case TitleRole: return QString::fromStdString(item.metadata.displayTitle());
        case ArtistRole: return QString::fromStdString(item.metadata.displayArtist());
        case PathRole: return QString::fromStdString(item.path.string());
        case IsCurrentRole: return s_playlist->currentIndex() == static_cast<size_t>(index.row());
    }
    return QVariant();
}

QHash<int, QByteArray> PlaylistBridge::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[TitleRole] = "title";
    roles[ArtistRole] = "artist";
    roles[PathRole] = "path";
    roles[IsCurrentRole] = "isCurrent";
    return roles;
}

int PlaylistBridge::currentIndex() const {
    return s_playlist ? static_cast<int>(s_playlist->currentIndex().value_or(-1)) : -1;
}

void PlaylistBridge::addFiles(const QList<QUrl>& urls) {
    if (!s_playlist) return;
    beginInsertRows(QModelIndex(), rowCount(), rowCount() + urls.size() - 1);
    for (const auto& url : urls) s_playlist->addFile(vc::fs::path(url.toLocalFile().toStdString()));
    endInsertRows();
    emit countChanged();
}

void PlaylistBridge::removeAt(int index) {
    if (!s_playlist || index < 0 || index >= rowCount()) return;
    beginRemoveRows(QModelIndex(), index, index);
    s_playlist->removeAt(index);
    endRemoveRows();
    emit countChanged();
}

void PlaylistBridge::clear() {
    if (!s_playlist) return;
    beginResetModel();
    s_playlist->clear();
    endResetModel();
    emit countChanged();
}

void PlaylistBridge::playAt(int index) {
    if (s_playlist) s_playlist->jumpTo(index);
}

} // namespace qml_bridge
