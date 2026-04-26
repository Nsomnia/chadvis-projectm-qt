#include "PlaylistBridge.hpp"
#include "audio/Playlist.hpp"
#include <QAbstractItemModel>
#include <QQmlEngine>
#include <QString>
#include <cstdint>

namespace {

QString formatDurationMs(std::int64_t ms) {
    if (ms <= 0) {
        return QStringLiteral("0:00");
    }

    const auto totalSeconds = ms / 1000;
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds % 3600) / 60;
    const auto seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(static_cast<qlonglong>(hours))
            .arg(static_cast<int>(minutes), 2, 10, QLatin1Char('0'))
            .arg(static_cast<int>(seconds), 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(static_cast<int>(minutes))
        .arg(static_cast<int>(seconds), 2, 10, QLatin1Char('0'));
}

} // namespace

namespace qml_bridge {

vc::Playlist* PlaylistBridge::s_playlist = nullptr;
PlaylistBridge* PlaylistBridge::s_instance = nullptr;
vc::Playlist* PlaylistBridge::s_connectedPlaylist = nullptr;
std::optional<std::size_t> PlaylistBridge::s_changedConnection = std::nullopt;
std::optional<std::size_t> PlaylistBridge::s_currentChangedConnection = std::nullopt;
bool PlaylistBridge::s_suppressPlaylistNotifications = false;

PlaylistBridge::PlaylistBridge(QObject* parent) : QAbstractListModel(parent) {
    s_instance = this;
    connectPlaylistSignals();
}

QObject* PlaylistBridge::create(QQmlEngine*, QJSEngine*) {
    return new PlaylistBridge();
}

void PlaylistBridge::setPlaylist(vc::Playlist* playlist) {
    s_playlist = playlist;
    connectPlaylistSignals();
    if (s_instance) {
        s_instance->onPlaylistChanged();
    }
}

void PlaylistBridge::connectPlaylistSignals() {
    if (!s_instance) {
        return;
    }

    if (s_changedConnection && s_connectedPlaylist) {
        s_connectedPlaylist->changed.disconnect(*s_changedConnection);
    }
    if (s_currentChangedConnection && s_connectedPlaylist) {
        s_connectedPlaylist->currentChanged.disconnect(*s_currentChangedConnection);
    }

    s_changedConnection.reset();
    s_currentChangedConnection.reset();
    s_connectedPlaylist = s_playlist;

    if (!s_playlist) {
        return;
    }

    s_changedConnection = s_playlist->changed.connect([] {
        if (s_instance) {
            s_instance->onPlaylistChanged();
        }
    });

    s_currentChangedConnection = s_playlist->currentChanged.connect([](std::size_t index) {
        if (!s_instance || s_suppressPlaylistNotifications) {
            return;
        }

        s_instance->onPlaylistCurrentChanged(index);
    });
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
        case PathRole:
            return QString::fromStdString(item.isRemote ? item.url : item.path.string());
        case DurationFormattedRole:
            return formatDurationMs(item.metadata.duration.count());
        case IsCurrentRole: return s_playlist->currentIndex() == static_cast<size_t>(index.row());
    }
    return QVariant();
}

QHash<int, QByteArray> PlaylistBridge::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[TitleRole] = "title";
    roles[ArtistRole] = "artist";
    roles[PathRole] = "path";
    roles[DurationFormattedRole] = "durationFormatted";
    roles[IsCurrentRole] = "isCurrent";
    return roles;
}

int PlaylistBridge::currentIndex() const {
    return s_playlist ? static_cast<int>(s_playlist->currentIndex().value_or(-1)) : -1;
}

bool PlaylistBridge::shuffle() const {
    return s_playlist ? s_playlist->shuffle() : false;
}

int PlaylistBridge::repeatMode() const {
    if (!s_playlist) {
        return 0;
    }

    switch (s_playlist->repeatMode()) {
        case vc::RepeatMode::Off: return 0;
        case vc::RepeatMode::All: return 1;
        case vc::RepeatMode::One: return 2;
    }

    return 0;
}

void PlaylistBridge::addFiles(const QList<QUrl>& urls) {
    if (!s_playlist) return;
    if (urls.isEmpty()) return;

    std::vector<vc::fs::path> paths;
    paths.reserve(urls.size());
    for (const auto& url : urls) {
        const auto localFile = url.toLocalFile();
        if (localFile.isEmpty()) {
            continue;
        }

        const auto path = vc::fs::path(localFile.toStdString());
        if (vc::MetadataReader::canRead(path)) {
            paths.push_back(std::move(path));
        }
    }

    if (paths.empty()) {
        return;
    }

    s_suppressPlaylistNotifications = true;
    beginInsertRows(QModelIndex(), rowCount(), rowCount() + static_cast<int>(paths.size()) - 1);
    for (const auto& path : paths) s_playlist->addFile(path);
    endInsertRows();
    s_suppressPlaylistNotifications = false;
    emit countChanged();
    emit currentIndexChanged();
}

void PlaylistBridge::removeAt(int row) {
  if (!s_playlist || row < 0 || row >= rowCount()) return;
  s_suppressPlaylistNotifications = true;
  beginRemoveRows(QModelIndex(), row, row);
  s_playlist->removeAt(row);
  endRemoveRows();
  s_suppressPlaylistNotifications = false;
  emit countChanged();
  emit currentIndexChanged();
  if (rowCount() > 0) {
    emit dataChanged(QAbstractListModel::index(0, 0), QAbstractListModel::index(rowCount() - 1, 0), {IsCurrentRole});
  }
}

void PlaylistBridge::clear() {
    if (!s_playlist) return;
    s_suppressPlaylistNotifications = true;
    beginResetModel();
    s_playlist->clear();
    endResetModel();
    s_suppressPlaylistNotifications = false;
    emit countChanged();
    emit currentIndexChanged();
}

void PlaylistBridge::playAt(int index) {
    if (s_playlist) s_playlist->jumpTo(index);
}

void PlaylistBridge::toggleShuffle() {
  if (!s_playlist) {
    return;
  }

  setShuffle(!s_playlist->shuffle());
}

void PlaylistBridge::setShuffle(bool enabled) {
    if (!s_playlist) {
        return;
    }

    s_suppressPlaylistNotifications = true;
    s_playlist->setShuffle(enabled);
    s_suppressPlaylistNotifications = false;
    emit shuffleChanged();
}

void PlaylistBridge::cycleRepeatMode() {
    if (!s_playlist) {
        return;
    }

    s_suppressPlaylistNotifications = true;
    s_playlist->cycleRepeatMode();
    s_suppressPlaylistNotifications = false;
    emit repeatModeChanged();
}

void PlaylistBridge::moveItem(int from, int to) {
    if (!s_playlist) {
        return;
    }

    const auto size = rowCount();
    if (from < 0 || to < 0 || from >= size || to >= size || from == to) {
        return;
    }

    const auto destination = to > from ? to + 1 : to;

    s_suppressPlaylistNotifications = true;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
    s_playlist->move(static_cast<vc::usize>(from), static_cast<vc::usize>(to));
    endMoveRows();
    s_suppressPlaylistNotifications = false;

  emit countChanged();
  emit currentIndexChanged();
  if (rowCount() > 0) {
    emit dataChanged(QAbstractListModel::index(0, 0), QAbstractListModel::index(rowCount() - 1, 0), {IsCurrentRole});
  }
}

QString PlaylistBridge::getItemPath(int idx) const {
  if (!s_playlist || idx < 0 || idx >= rowCount()) {
        return {};
    }

    const auto* item = s_playlist->itemAt(static_cast<vc::usize>(idx));
    if (!item) {
        return {};
    }

    return QString::fromStdString(item->isRemote ? item->url : item->path.string());
}

void PlaylistBridge::onPlaylistChanged() {
    if (!s_instance || s_suppressPlaylistNotifications) {
        return;
    }

    beginResetModel();
    endResetModel();

    emit countChanged();
    emit currentIndexChanged();
    emit shuffleChanged();
    emit repeatModeChanged();
}

void PlaylistBridge::onPlaylistCurrentChanged(std::size_t) {
  if (!s_instance || s_suppressPlaylistNotifications) {
    return;
  }

  emit currentIndexChanged();
  if (rowCount() > 0) {
    emit dataChanged(QAbstractListModel::index(0, 0), QAbstractListModel::index(rowCount() - 1, 0), {IsCurrentRole});
  }
}

} // namespace qml_bridge
