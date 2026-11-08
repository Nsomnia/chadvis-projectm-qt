#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The Hooks tab reads: `GET /video/hooks/user_hooks`,
/// `GET /video/hooks/me/liked/v2`, and `GET /video/hooks/suggested_clips`
/// (ENDPOINT-INVENTORY §4.12, T0383; UI-SURFACE-INVENTORY Hooks/Liked Hooks).
///
/// No list key is pinned for any of the three bodies — `user_hooks` and
/// `me/liked/v2` were not in the captured HAR and `suggested_clips` was
/// observed without a recorded shape — so every entry is kept as the
/// server's own `QJsonObject`. Mapping a schema the capture never
/// reproduced key-for-key would invent fields and silently drop the ones
/// it did not list; a caller reads the keys it needs. The parsers accept
/// a top-level array or the first recognised list key, in server order.
///
/// The three sections are independent: one failing never empties the other,
/// and a failure leaves its own list empty with a reason rather than a
/// stale answer.
class SunoHooksService final : public QObject
{
    Q_OBJECT

public:
    struct State {
        /// Entries from `GET /video/hooks/user_hooks`.
        QList<QJsonObject> userHooks;
        /// Entries from `GET /video/hooks/me/liked/v2`.
        QList<QJsonObject> likedHooks;
        /// Entries from `GET /video/hooks/suggested_clips`.
        QList<QJsonObject> suggestedClips;
        /// Empty when the last load of every section succeeded.
        QString failureReason;
    };

    explicit SunoHooksService(SunoClient* client, QObject* parent = nullptr);

    /// List entries from `user_hooks`, raw and in server order. A missing
    /// or non-array value is an empty list, not a failure.
    static QList<QJsonObject> parseUserHooks(const QJsonObject& root);
    /// List entries from `me/liked/v2`, raw and in server order.
    static QList<QJsonObject> parseLiked(const QJsonObject& root);
    /// List entries from `suggested_clips`, raw and in server order.
    static QList<QJsonObject> parseSuggested(const QJsonObject& root);

    void loadUserHooks();
    void loadLiked();
    void loadSuggested();

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const QList<QJsonObject>& userHooks() const { return state_.userHooks; }
    [[nodiscard]] const QList<QJsonObject>& likedHooks() const { return state_.likedHooks; }
    [[nodiscard]] const QList<QJsonObject>& suggestedClips() const { return state_.suggestedClips; }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const
    {
        return userHooksLoading_ || likedLoading_ || suggestedLoading_;
    }

signals:
    /// One section settled (loaded or failed), so a fast tab appears without
    /// waiting for the other.
    void sectionChanged();
    void failed(const QString& message);

private:
    void handleUserHooksReply(QNetworkReply* reply, quint64 generation);
    void handleLikedReply(QNetworkReply* reply, quint64 generation);
    void handleSuggestedReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    bool userHooksLoading_{false};
    bool likedLoading_{false};
    bool suggestedLoading_{false};
    quint64 userHooksGeneration_{0};
    quint64 likedGeneration_{0};
    quint64 suggestedGeneration_{0};
};

} // namespace vc::suno
