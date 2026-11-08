#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// Read-only bundle of five free, unmetered reads that need no credit spend
/// and mutate nothing:
///
///   * `GET /clip/{id}` — one clip, no feed envelope
///   * `GET /project/{id}/pinned-clips` — pinned clips for any project
///   * `GET /cms/cover-art-suggestions` — CMS cover-art prompt suggestions
///   * `POST /realtime/token` — the Ably credential for the realtime stream
///   * `GET /challenge/progress` — challenge / bonus progress
///
/// Evidence is partial for most of these (ENDPOINT-INVENTORY §4.10), so the
/// bodies whose shapes were not captured are kept as raw server objects
/// rather than parsed into invented fields. A failure empties only that
/// route's section and names the reason; the other sections are untouched.
class SunoFreeReadsService final : public QObject
{
    Q_OBJECT

public:
    enum class Route { Clip, PinnedClips, CoverArtSuggestions, RealtimeToken, ChallengeProgress };
    /// One `{items[]}` entry of `GET /cms/cover-art-suggestions`. `label` and
    /// `prompt` are objects on the wire; their inner fields were not captured,
    /// so they are kept raw.
    struct CoverArtSuggestion {
        QString slug;
        QJsonObject label;
        QJsonObject prompt;
    };

    struct State {
        /// Raw clip object from `GET /clip/{id}` ({clip_id} is a gen clip id).
        QJsonObject clip;
        QString clipId;
        /// Raw root value from `GET /project/{id}/pinned-clips`. The capture
        /// establishes the route, not the envelope, so the root (object or
        /// array) is preserved verbatim instead of inventing a wrapper key.
        QJsonValue pinnedClips;
        QString pinnedProjectId;
        QList<CoverArtSuggestion> coverArtSuggestions;
        /// Raw Ably token-request object from `POST /realtime/token`.
        QJsonObject realtimeToken;
        /// Raw root object from `GET /challenge/progress`.
        QJsonObject challengeProgress;
        /// Empty when the last settled route succeeded.
        QString failureReason;
    };

    explicit SunoFreeReadsService(SunoClient* client, QObject* parent = nullptr);

    static std::expected<QList<CoverArtSuggestion>, QString>
    parseCoverArtSuggestions(const QJsonObject& root);

    void fetchClip(const QString& clipId);
    void fetchPinnedClips(const QString& projectId);
    void fetchCoverArtSuggestions();
    /// `channelPatterns` are the `channel_pattern` values the caller subscribes
    /// to (the captured shape is `{"capabilities":[{"channel_pattern":…,
    /// "subscribe":true}]}`); the service never invents a user id.
    void fetchRealtimeToken(const QStringList& channelPatterns);
    void fetchChallengeProgress();

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] bool isLoading() const
    {
        for (const bool flag : loading_) {
            if (flag) return true;
        }
        return false;
    }

signals:
    void changed();
    void failed(const QString& message);

private:
    void enqueue(Route route, const QString& path, const QString& method, const QByteArray& data);
    void handleReply(QNetworkReply* reply, Route route, quint64 generation);
    void fail(Route route, const QString& message);

    SunoClient* client_;
    State state_;
    bool loading_[5]{};
    quint64 generation_[5]{};
};

} // namespace vc::suno
