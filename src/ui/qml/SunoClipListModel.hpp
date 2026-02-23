#pragma once

#include <QAbstractListModel>
#include <QString>

#include <vector>

namespace vc::ui::qml {

class SunoClipListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        TagsRole,
        StatusRole,
        DurationRole,
        CreatedAtRole,
        LyricsPreviewRole,
        HasLyricsRole,
        ImageUrlRole,
    };

    struct ClipRow {
        QString id;
        QString title;
        QString artist;
        QString tags;
        QString status;
        QString duration;
        QString createdAt;
        QString lyricsPreview;
        bool hasLyrics{false};
        QString imageUrl;
    };

    explicit SunoClipListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(std::vector<ClipRow> rows);
    const ClipRow* clipAt(int row) const;

private:
    std::vector<ClipRow> rows_;
};

} // namespace vc::ui::qml
