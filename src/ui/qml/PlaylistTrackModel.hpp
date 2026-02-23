#pragma once

#include <QAbstractListModel>
#include <QString>

#include <optional>
#include <vector>

#include "audio/Playlist.hpp"

namespace vc {
class AudioEngine;
}

namespace vc::ui::qml {

class PlaylistTrackModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        DurationMsRole,
        DurationTextRole,
        IsRemoteRole,
        IsCurrentRole,
    };

    explicit PlaylistTrackModel(AudioEngine* audioEngine, QObject* parent = nullptr);
    ~PlaylistTrackModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const;

    Q_INVOKABLE void jumpTo(int index);

signals:
    void currentIndexChanged();

private:
    struct TrackRow {
        QString title;
        QString artist;
        qint64 durationMs{0};
        QString durationText;
        bool isRemote{false};
        bool isCurrent{false};
    };

    void reload();
    static QString formatDuration(qint64 durationMs);

    AudioEngine* audioEngine_{nullptr};
    std::vector<TrackRow> rows_;

    std::optional<Signal<>::SlotId> playlistChangedConnection_;
    std::optional<Signal<usize>::SlotId> playlistCurrentConnection_;
};

} // namespace vc::ui::qml
