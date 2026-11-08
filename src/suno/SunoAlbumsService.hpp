#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The Albums tab read: `POST /unified/feed` with
/// `{"feed_id":"library_albums","cursor":null,"page_size":12,
/// "request_metadata":{"term":"","library_scope":"all"}}`
/// (ENDPOINT-INVENTORY §4.12, T0383).
///
/// The body is pinned only by shape — `{feed:{feed_id,feed_title,
/// feed_direct_link,items:[{content_id,content_type:"album",content_item}],
/// presentation,item_count}}` — so every entry is kept as the server's own
/// `QJsonObject`, in server order. Mapping a schema the capture never
/// reproduced key-for-key would invent fields and silently drop the ones it
/// did not list; a caller reads the keys it needs. The feed title and feed_id
/// are kept as structured fields alongside the raw items.
///
/// A failure leaves the list empty with a reason rather than a stale answer.
class SunoAlbumsService final : public QObject
{
    Q_OBJECT

public:
    struct State {
        /// `feed.items[]` entries, each still carrying its `content_item`.
        QList<QJsonObject> albums;
        /// `feed.feed_title` from the last successful load.
        QString feedTitle;
        /// `feed.feed_id` from the last successful load.
        QString feedId;
        /// Empty when the last load succeeded.
        QString failureReason;
    };

    explicit SunoAlbumsService(SunoClient* client, QObject* parent = nullptr);

    /// The exact albums request body. Exposed so a test pins it.
    static QJsonObject requestBody();

    /// `feed.items[]` as raw entry objects. A missing feed or non-array
    /// items value is an empty list, not a failure.
    static QList<QJsonObject> parseAlbums(const QJsonObject& root);
    /// `feed.feed_title`, empty when absent.
    static QString parseFeedTitle(const QJsonObject& root);
    /// `feed.feed_id`, empty when absent.
    static QString parseFeedId(const QJsonObject& root);

    void loadAlbums();

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const QList<QJsonObject>& albums() const { return state_.albums; }
    [[nodiscard]] QString feedTitle() const { return state_.feedTitle; }
    [[nodiscard]] QString feedId() const { return state_.feedId; }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const { return loading_; }

signals:
    void sectionChanged();
    void failed(const QString& message);

private:
    void handleReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    bool loading_{false};
    quint64 generation_{0};
};

} // namespace vc::suno
