/**
 * @file PlaylistBridge.cpp
 * @brief Implementation of PlaylistBridge QML model
 */

#include "PlaylistBridge.hpp"
#include "audio/AudioEngine.hpp"
#include "audio/Playlist.hpp"
#include "audio/MediaMetadata.hpp"
#include <QQmlEngine>
#include <QUrl>
#include <QFile>
#include <QDir>

namespace qml_bridge {

PlaylistBridge::PlaylistBridge(QObject* parent)
    : QAbstractListModel(parent)
{
}

PlaylistBridge* PlaylistBridge::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
{
    Q_UNUSED(jsEngine)
    auto* bridge = new PlaylistBridge(qmlEngine);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    return bridge;
}

void PlaylistBridge::setAudioEngine(vc::AudioEngine* engine)
{
    if (engine_ == engine) return;

    beginResetModel();
    engine_ = engine;
    endResetModel();

    if (engine_) {
        engine_->trackChanged.connect([this] {
            QMetaObject::invokeMethod(this, &PlaylistBridge::onPlaylistChanged);
        });

        connect(this, &PlaylistBridge::countChanged, this, &PlaylistBridge::onPlaylistChanged);
    }
}

int PlaylistBridge::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !engine_) return 0;
    return static_cast<int>(engine_->playlist().items().size());
}

QVariant PlaylistBridge::data(const QModelIndex& index, int role) const
{
    if (!engine_ || !index.isValid()) return {};

    const auto& items = engine_->playlist().items();
    int row = index.row();

    if (row < 0 || row >= static_cast<int>(items.size())) return {};

    const auto& item = items[row];
    const auto& meta = item.metadata;
    size_t currentIdx = engine_->playlist().currentIndex();

    switch (role) {
        case TitleRole:
            return QString::fromStdString(meta.displayTitle());
        case ArtistRole:
            return QString::fromStdString(meta.displayArtist());
        case AlbumRole:
            return QString::fromStdString(meta.album);
        case DurationRole:
            return static_cast<qint64>(meta.duration.count());
        case DurationFormattedRole: {
            int totalSec = static_cast<int>(meta.duration.count() / 1000);
            int min = totalSec / 60;
            int sec = totalSec % 60;
            return QString("%1:%2").arg(min).arg(sec, 2, 10, QChar('0'));
        }
        case PathRole:
            return QUrl::fromLocalFile(QString::fromStdString(item.path.string()));
        case IsCurrentRole:
            return static_cast<size_t>(row) == currentIdx;
        default:
            return {};
    }
}

QHash<int, QByteArray> PlaylistBridge::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {DurationRole, "duration"},
        {DurationFormattedRole, "durationFormatted"},
        {PathRole, "path"},
        {IsCurrentRole, "isCurrent"}
    };
}

int PlaylistBridge::currentIndex() const
{
    if (!engine_) return -1;
    return static_cast<int>(engine_->playlist().currentIndex());
}

QStringList PlaylistBridge::supportedFormats() const
{
    return {"mp3", "flac", "ogg", "opus", "wav", "m4a", "aac"};
}

void PlaylistBridge::addFiles(const QList<QUrl>& urls)
{
    if (!engine_) return;

    for (const auto& url : urls) {
        if (url.isLocalFile()) {
            engine_->playlist().addFile(url.toLocalFile().toStdString());
        }
    }

    emit countChanged();
}

void PlaylistBridge::addFolder(const QUrl& folderUrl)
{
    if (!engine_ || !folderUrl.isLocalFile()) return;

    QString folder = folderUrl.toLocalFile();
    QDir dir(folder);

    auto formats = supportedFormats();
    QStringList filters;
    for (const auto& fmt : formats) {
        filters << QString("*.%1").arg(fmt);
    }

    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::Readable);

    for (const auto& file : dir.entryInfoList()) {
        engine_->playlist().addFile(file.absoluteFilePath().toStdString());
    }

    emit countChanged();
}

void PlaylistBridge::removeAt(int index)
{
    if (!engine_) return;

    engine_->playlist().removeAt(static_cast<size_t>(index));
    emit countChanged();
}

void PlaylistBridge::clear()
{
    if (!engine_) return;

    engine_->playlist().clear();
    emit countChanged();
}

void PlaylistBridge::playAt(int index)
{
    if (!engine_) return;

    engine_->playlist().playAt(static_cast<size_t>(index));
}

void PlaylistBridge::shuffle()
{
    if (!engine_) return;

    engine_->playlist().shuffle();
    emit countChanged();
}

QUrl PlaylistBridge::getUrl(int index) const
{
    if (!engine_) return {};

    const auto& items = engine_->playlist().items();
    if (index < 0 || index >= static_cast<int>(items.size())) return {};

    return QUrl::fromLocalFile(QString::fromStdString(items[index].path.string()));
}

void PlaylistBridge::onPlaylistChanged()
{
    beginResetModel();
    endResetModel();

    int newIdx = currentIndex();
    if (newIdx != currentIndex_) {
        currentIndex_ = newIdx;
        emit currentIndexChanged();
    }
}

void PlaylistBridge::onCurrentItemChanged(size_t index)
{
    int newIdx = static_cast<int>(index);
    if (newIdx != currentIndex_) {
        currentIndex_ = newIdx;
        emit currentIndexChanged();
    }
}

} // namespace qml_bridge
