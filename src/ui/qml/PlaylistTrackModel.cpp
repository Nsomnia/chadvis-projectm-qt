#include "ui/qml/PlaylistTrackModel.hpp"

#include <QMetaObject>
#include <QThread>

#include "audio/AudioEngine.hpp"

namespace vc::ui::qml {

namespace {

QString fallbackTitle(const PlaylistItem& item) {
    if (!item.metadata.title.empty()) {
        return QString::fromStdString(item.metadata.title);
    }

    if (item.isRemote) {
        return QString::fromStdString(item.url);
    }

    return QString::fromStdString(item.path.filename().string());
}

QString fallbackArtist(const PlaylistItem& item) {
    if (!item.metadata.artist.empty()) {
        return QString::fromStdString(item.metadata.artist);
    }

    if (item.isRemote) {
        return QStringLiteral("Remote Source");
    }

    return QStringLiteral("Local Library");
}

} // namespace

PlaylistTrackModel::PlaylistTrackModel(AudioEngine* audioEngine, QObject* parent)
    : QAbstractListModel(parent)
    , audioEngine_(audioEngine) {
    if (!audioEngine_) {
        return;
    }

    auto& playlist = audioEngine_->playlist();
    playlistChangedConnection_ = playlist.changed.connect([this] {
        QMetaObject::invokeMethod(this, [this] { reload(); }, Qt::QueuedConnection);
    });

    playlistCurrentConnection_ = playlist.currentChanged.connect([this](usize) {
        QMetaObject::invokeMethod(this, [this] { reload(); }, Qt::QueuedConnection);
    });

    reload();
}

PlaylistTrackModel::~PlaylistTrackModel() {
    if (!audioEngine_) {
        return;
    }

    auto& playlist = audioEngine_->playlist();
    if (playlistChangedConnection_) {
        playlist.changed.disconnect(*playlistChangedConnection_);
    }

    if (playlistCurrentConnection_) {
        playlist.currentChanged.disconnect(*playlistCurrentConnection_);
    }
}

int PlaylistTrackModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(rows_.size());
}

QVariant PlaylistTrackModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }

    const auto row = static_cast<usize>(index.row());
    if (row >= rows_.size()) {
        return {};
    }

    const auto& item = rows_[row];
    switch (role) {
    case TitleRole:
        return item.title;
    case ArtistRole:
        return item.artist;
    case DurationMsRole:
        return item.durationMs;
    case DurationTextRole:
        return item.durationText;
    case IsRemoteRole:
        return item.isRemote;
    case IsCurrentRole:
        return item.isCurrent;
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistTrackModel::roleNames() const {
    return {
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {DurationMsRole, "durationMs"},
            {DurationTextRole, "durationText"},
            {IsRemoteRole, "isRemote"},
            {IsCurrentRole, "isCurrent"},
    };
}

int PlaylistTrackModel::currentIndex() const {
    if (!audioEngine_) {
        return -1;
    }

    const auto index = audioEngine_->playlist().currentIndex();
    if (!index.has_value()) {
        return -1;
    }

    return static_cast<int>(*index);
}

void PlaylistTrackModel::jumpTo(int index) {
    if (!audioEngine_ || index < 0) {
        return;
    }

    if (audioEngine_->playlist().jumpTo(static_cast<usize>(index))) {
        audioEngine_->play();
    }
}

void PlaylistTrackModel::reload() {
    if (!audioEngine_) {
        return;
    }

    beginResetModel();

    rows_.clear();
    const auto& playlist = audioEngine_->playlist();
    const auto activeIndex = playlist.currentIndex();

    const auto& items = playlist.items();
    rows_.reserve(items.size());

    for (usize i = 0; i < items.size(); ++i) {
        const auto& src = items[i];

        TrackRow row;
        row.title = fallbackTitle(src);
        row.artist = fallbackArtist(src);
        row.durationMs = static_cast<qint64>(src.metadata.duration.count());
        row.durationText = formatDuration(row.durationMs);
        row.isRemote = src.isRemote;
        row.isCurrent = activeIndex.has_value() && *activeIndex == i;

        rows_.push_back(std::move(row));
    }

    endResetModel();

    emit currentIndexChanged();
}

QString PlaylistTrackModel::formatDuration(qint64 durationMs) {
    if (durationMs <= 0) {
        return QStringLiteral("--:--");
    }

    const auto totalSeconds = durationMs / 1000;
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;

    return QStringLiteral("%1:%2")
            .arg(minutes)
            .arg(seconds, 2, 10, QChar('0'));
}

} // namespace vc::ui::qml
