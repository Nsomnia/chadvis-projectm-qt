#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The Trash and History tabs' reads: `GET /clips/trashed_v2` and
/// `GET /profiles/listen-history` (ENDPOINT-INVENTORY §4.12, T0383).
///
/// Both bodies are pinned only by shape — `{clips:[…]}` and
/// `{history:[{content:…}]}` — so every entry is kept as the server's own
/// `QJsonObject`. Mapping a schema the capture never reproduced key-for-key
/// would invent fields and silently drop the ones it did not list; a caller
/// reads the keys it needs.
///
/// The two sections are independent: one failing never empties the other, and a
/// failure leaves its own list empty with a reason rather than a stale answer.
class SunoLibrarySectionService final : public QObject
{
    Q_OBJECT

public:
    struct State {
        /// `clips[]` entries from the trash list.
        QList<QJsonObject> records;
        /// `history[]` entries, each still carrying its `content` object.
        QList<QJsonObject> history;
        /// Empty when the last load of every section succeeded.
        QString failureReason;
    };

    explicit SunoLibrarySectionService(SunoClient* client, QObject* parent = nullptr);

    /// `clips[]` as raw objects. A non-array is an empty list, not a failure:
    /// the tab renders empty either way, and the shape is the only contract.
    static QList<QJsonObject> parseTrashed(const QJsonObject& root);
    /// `history[]` as raw entry objects. The `content` object is left in place
    /// rather than hoisted, so no entry field is lost.
    static QList<QJsonObject> parseHistory(const QJsonObject& root);

    void loadTrashed();
    void loadHistory();

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const QList<QJsonObject>& records() const { return state_.records; }
    [[nodiscard]] const QList<QJsonObject>& history() const { return state_.history; }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const { return trashedLoading_ || historyLoading_; }

signals:
    /// One section settled (loaded or failed), so a fast tab appears without
    /// waiting for the other.
    void sectionChanged();
    void failed(const QString& message);

private:
    void handleTrashedReply(QNetworkReply* reply, quint64 generation);
    void handleHistoryReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    bool trashedLoading_{false};
    bool historyLoading_{false};
    quint64 trashedGeneration_{0};
    quint64 historyGeneration_{0};
};

} // namespace vc::suno
