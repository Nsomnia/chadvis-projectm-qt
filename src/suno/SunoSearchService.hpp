#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// Read-only service for the Explore/search page's two routes
/// (ENDPOINT-INVENTORY §4.5, captured 2026-11-08, [T1]):
///
///   * `POST /unified/search/suggest` — keystroke suggestions.
///     Request `{term, suggestion_size, content_size}`; response `{feed}`.
///   * `POST /unified/search/omnisearch` — submit search.
///     Request `{term, completion_method}`; response
///     `{feed: {feed_id, feed_title, items}}`.
///
/// The capture records field names and types only: no observed
/// `suggestion_size`, `content_size`, or `completion_method` value exists to
/// default from, so all three are required caller-supplied parameters and
/// nothing is guessed. Debounce is the caller's job; this service sends
/// exactly one request per call and never coalesces keystrokes itself.
///
/// Only the three response keys above are parsed. The inner shape of
/// `feed.items` was not captured, so `items` stays a raw `QJsonArray`.
/// A failure empties only that route's section and names the reason; the
/// other route's section is untouched.
class SunoSearchService final : public QObject
{
    Q_OBJECT

public:
    enum class Route { Suggest, OmniSearch };

    struct State {
        /// Raw `feed` object from `POST /unified/search/suggest`.
        QJsonObject suggestionFeed;
        QString suggestionTerm;
        /// Raw `feed` object from `POST /unified/search/omnisearch`.
        QJsonObject searchFeed;
        QString searchFeedId;
        QString searchFeedTitle;
        /// Raw `feed.items` from omnisearch; inner item shape uncaptured.
        QJsonArray searchItems;
        QString searchTerm;
        /// Empty when the last settled route succeeded.
        QString failureReason;
    };

    explicit SunoSearchService(SunoClient* client, QObject* parent = nullptr);

    void suggest(const QString& term, int suggestionSize, int contentSize);
    void search(const QString& term, const QString& completionMethod);

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
    void enqueue(Route route, const QString& path, const QByteArray& body);
    void handleReply(QNetworkReply* reply, Route route, quint64 generation);
    void fail(Route route, const QString& message);

    SunoClient* client_;
    State state_;
    bool loading_[2]{};
    quint64 generation_[2]{};
};

} // namespace vc::suno
