#pragma once

#include "SunoModels.hpp"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// Read side of playlists: the signed-in account's playlist list and one
/// playlist's clips, paged.
///
/// The wire shape is captured, not guessed (ENDPOINT-INVENTORY §4.5, probes
/// 2026-10-26): `GET /playlist/{id}/?page=N` answers the playlist object at the
/// TOP LEVEL -- not `{playlist, playlist_clips}` -- and each `playlist_clips[]`
/// entry is `{clip, relative_index, created_at}`. Paging is a 1-based page
/// number with no `has_more`, so the end is an empty page (or the server's own
/// `num_total_results` being reached), never a flag.
///
/// Writes live in SunoLibraryMutations; this class only ever issues GETs.
class SunoPlaylistService final : public QObject
{
    Q_OBJECT

public:
    struct Summary {
        QString id;
        QString name;
        QString description;
        QString imageUrl;
        QString userDisplayName;
        QString userHandle;
        int numTotalResults{0};
        int songCount{0};
        qint64 totalDuration{0};
        bool isOwned{false};
        bool isPublic{false};
        bool isTrashed{false};
    };

    struct Page {
        Summary playlist;
        QList<SunoClip> clips;
        int currentPage{0};
        int totalResults{0};
    };

    explicit SunoPlaylistService(SunoClient* client, QObject* parent = nullptr);

    /// Tolerates the three envelopes seen or likely: a bare array, or an object
    /// carrying the array under `playlists` / `playlist_list` / `results`.
    static std::expected<QList<Summary>, QString> parseList(const QJsonValue& root);
    static std::expected<Page, QString> parsePage(const QJsonObject& root);

    /// The account's playlists. Re-reads stored credentials first, the same way
    /// SunoExploreService::refresh does, so a cold start still authenticates.
    void refresh();

    /// Open one playlist at `page` (1-based). Resets the accumulated clips.
    void openPlaylist(const QString& playlistId, int page = 1);

    /// Next page of the playlist currently open. No-op when the last page was
    /// empty or `num_total_results` is already covered.
    void loadMore();

    [[nodiscard]] const QList<Summary>& playlists() const { return playlists_; }
    [[nodiscard]] const Summary& currentPlaylist() const { return current_.playlist; }
    [[nodiscard]] const QList<SunoClip>& clips() const { return current_.clips; }
    [[nodiscard]] bool isLoading() const { return loading_; }
    [[nodiscard]] bool hasMore() const { return hasMoreAfter(clips().size(), current_.totalResults); }
    [[nodiscard]] int nextPage() const { return current_.currentPage + 1; }

    /// The paging arithmetic, separated from the accumulated state so it can be
    /// pinned: there is no `has_more` in this route, so the only end signals are
    /// an empty page (clipsSeen unchanged) and the server's own total.
    [[nodiscard]] static bool hasMoreAfter(int clipsSeen, int totalResults)
    {
        if (clipsSeen <= 0)
        {
            return false;
        }
        return totalResults <= 0 || clipsSeen < totalResults;
    }

signals:
    /// The list was replaced (not appended) -- a view should rebuild.
    void playlistsReset();
    void playlistsReady();
    /// The open playlist's first page arrived; `clips()` is authoritative again.
    void playlistReset();
    /// Another page was appended to `clips()`.
    void playlistPageReady();
    void loadingChanged();
    void failed(const QString& message);

private:
    void enqueueList();
    void enqueuePage(const QString& playlistId, int page, bool append);
    void handleListReply(QNetworkReply* reply, quint64 requestId);
    void handlePageReply(QNetworkReply* reply, bool append, quint64 requestId);
    void onCredentialsRestored();
    void setLoading(bool loading);
    void fail(const QString& message);

    SunoClient* client_;
    QList<Summary> playlists_;
    Page current_;
    bool credentialRefreshPending_{false};
    bool loading_{false};
    quint64 activeRequest_{0};
};

} // namespace vc::suno
