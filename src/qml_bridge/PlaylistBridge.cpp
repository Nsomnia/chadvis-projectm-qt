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
		connect(engine_, &vc::AudioEngine::trackChanged,
			this, [this] {
				QMetaObject::invokeMethod(this, &PlaylistBridge::onPlaylistChanged);
			}, Qt::QueuedConnection);

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
    auto currentIdxOpt = engine_->playlist().currentIndex();
    size_t currentIdx = currentIdxOpt.value_or(SIZE_MAX);

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
    auto opt = engine_->playlist().currentIndex();
    return opt.has_value() ? static_cast<int>(*opt) : -1;
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

    engine_->playlist().jumpTo(static_cast<size_t>(index));
}

void PlaylistBridge::shuffle()
{
    if (!engine_) return;

    engine_->playlist().setShuffle(!engine_->playlist().shuffle());
    emit countChanged();
}

QUrl PlaylistBridge::getUrl(int index) const
{
    if (!engine_) return {};

    const auto& items = engine_->playlist().items();
    if (index < 0 || index >= static_cast<int>(items.size())) return {};

    return QUrl::fromLocalFile(QString::fromStdString(items[index].path.string()));
}

void PlaylistBridge::moveItem(int from, int to)
{
    if (!engine_) return;

    const auto& items = engine_->playlist().items();
    if (from < 0 || from >= static_cast<int>(items.size())) return;
    if (to < 0 || to >= static_cast<int>(items.size())) return;

    engine_->playlist().move(static_cast<size_t>(from), static_cast<size_t>(to));
    emit countChanged();
}

QString PlaylistBridge::getItemPath(int index) const
{
    if (!engine_) return {};

    const auto& items = engine_->playlist().items();
    if (index < 0 || index >= static_cast<int>(items.size())) return {};

    return QString::fromStdString(items[index].path.string());
}

void PlaylistBridge::cycleRepeatMode()
{
    if (!engine_) return;

    engine_->playlist().cycleRepeatMode();
    emit repeatModeChanged();
}

int PlaylistBridge::repeatMode() const
{
    if (!engine_) return 0;
    return static_cast<int>(engine_->playlist().repeatMode());
}

void PlaylistBridge::setRepeatMode(int mode)
{
    if (!engine_) return;
    engine_->playlist().setRepeatMode(static_cast<vc::RepeatMode>(mode));
    emit repeatModeChanged();
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
