#include "ui/qml/SunoClipListModel.hpp"

namespace vc::ui::qml {

SunoClipListModel::SunoClipListModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int SunoClipListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(rows_.size());
}

QVariant SunoClipListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }

    const auto row = static_cast<std::size_t>(index.row());
    if (row >= rows_.size()) {
        return {};
    }

    const auto& item = rows_[row];
    switch (role) {
    case IdRole:
        return item.id;
    case TitleRole:
        return item.title;
    case ArtistRole:
        return item.artist;
    case TagsRole:
        return item.tags;
    case StatusRole:
        return item.status;
    case DurationRole:
        return item.duration;
    case CreatedAtRole:
        return item.createdAt;
    case LyricsPreviewRole:
        return item.lyricsPreview;
    case HasLyricsRole:
        return item.hasLyrics;
    case ImageUrlRole:
        return item.imageUrl;
    default:
        return {};
    }
}

QHash<int, QByteArray> SunoClipListModel::roleNames() const {
    return {
            {IdRole, "clipId"},
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {TagsRole, "tags"},
            {StatusRole, "status"},
            {DurationRole, "duration"},
            {CreatedAtRole, "createdAt"},
            {LyricsPreviewRole, "lyricsPreview"},
            {HasLyricsRole, "hasLyrics"},
            {ImageUrlRole, "imageUrl"},
    };
}

void SunoClipListModel::setRows(std::vector<ClipRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const SunoClipListModel::ClipRow* SunoClipListModel::clipAt(int row) const {
    if (row < 0) {
        return nullptr;
    }

    const auto idx = static_cast<std::size_t>(row);
    if (idx >= rows_.size()) {
        return nullptr;
    }

    return &rows_[idx];
}

} // namespace vc::ui::qml
