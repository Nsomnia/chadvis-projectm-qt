#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The Listen-for-Credits reads: `GET /preferences/{summary,tasks,rewards,
/// stats,profile,leaderboard}` (ENDPOINT-INVENTORY §5.15, T0365).
///
/// Read side only. `POST /preferences/submit` is captcha-gated and
/// `POST /preferences/tasks/release-buffer` returns a claimed task; neither
/// is touched here, so this service never claims, submits, or releases a
/// task.
///
/// Each body is pinned only by shape (see the per-route contracts in
/// ENDPOINT-INVENTORY §5.15), so every section keeps the server's own
/// `QJsonObject` root verbatim. Mapping a schema the capture never
/// reproduced key-for-key would invent fields and silently drop the ones it
/// did not list; a caller reads the keys it needs. The leaderboard also
/// exposes its `entries[]` as raw entry objects in server order.
///
/// The six sections are independent: one failing never empties another, and
/// a failure leaves its own section empty with a reason rather than a
/// stale answer.
class SunoPreferencesService final : public QObject
{
    Q_OBJECT

public:
    struct State {
        /// `GET /preferences/summary` root, verbatim.
        QJsonObject summary;
        /// `GET /preferences/tasks` root, verbatim.
        QJsonObject tasks;
        /// `GET /preferences/rewards` root, verbatim.
        QJsonObject rewards;
        /// `GET /preferences/stats` root, verbatim.
        QJsonObject stats;
        /// `GET /preferences/profile` root, verbatim (`{}` until filled).
        QJsonObject profile;
        /// `GET /preferences/leaderboard` root, verbatim.
        QJsonObject leaderboard;
        /// `leaderboard.entries[]` as raw entry objects, in server order.
        QList<QJsonObject> leaderboardEntries;
        /// Empty when the last load of every section succeeded.
        QString failureReason;
    };

    explicit SunoPreferencesService(SunoClient* client, QObject* parent = nullptr);

    /// Each parser keeps the captured root verbatim: an unknown future
    /// field is preserved, never mapped onto an invented type. Identity
    /// parsers, except leaderboard entries.
    static QJsonObject parseSummary(const QJsonObject& root);
    static QJsonObject parseTasks(const QJsonObject& root);
    static QJsonObject parseRewards(const QJsonObject& root);
    static QJsonObject parseStats(const QJsonObject& root);
    static QJsonObject parseProfile(const QJsonObject& root);
    static QJsonObject parseLeaderboard(const QJsonObject& root);
    /// `entries[]` as raw entry objects. A missing or non-array value is
    /// an empty list, not a failure.
    static QList<QJsonObject> parseLeaderboardEntries(const QJsonObject& root);

    void loadSummary();
    void loadTasks();
    void loadRewards();
    void loadStats();
    void loadProfile();
    void loadLeaderboard();

    [[nodiscard]] const State& state() const { return state_; }
    [[nodiscard]] const QJsonObject& summary() const { return state_.summary; }
    [[nodiscard]] const QJsonObject& tasks() const { return state_.tasks; }
    [[nodiscard]] const QJsonObject& rewards() const { return state_.rewards; }
    [[nodiscard]] const QJsonObject& stats() const { return state_.stats; }
    [[nodiscard]] const QJsonObject& profile() const { return state_.profile; }
    [[nodiscard]] const QJsonObject& leaderboard() const { return state_.leaderboard; }
    [[nodiscard]] const QList<QJsonObject>& leaderboardEntries() const
    {
        return state_.leaderboardEntries;
    }
    [[nodiscard]] QString failureReason() const { return state_.failureReason; }
    [[nodiscard]] bool isLoading() const
    {
        return summaryLoading_ || tasksLoading_ || rewardsLoading_ || statsLoading_
                || profileLoading_ || leaderboardLoading_;
    }
    [[nodiscard]] bool isLoadingSummary() const { return summaryLoading_; }
    [[nodiscard]] bool isLoadingTasks() const { return tasksLoading_; }
    [[nodiscard]] bool isLoadingRewards() const { return rewardsLoading_; }
    [[nodiscard]] bool isLoadingStats() const { return statsLoading_; }
    [[nodiscard]] bool isLoadingProfile() const { return profileLoading_; }
    [[nodiscard]] bool isLoadingLeaderboard() const { return leaderboardLoading_; }

signals:
    /// One section settled (loaded or failed), so a fast view appears
    /// without waiting for the other sections.
    void sectionChanged();
    void failed(const QString& message);

private:
    void handleSummaryReply(QNetworkReply* reply, quint64 generation);
    void handleTasksReply(QNetworkReply* reply, quint64 generation);
    void handleRewardsReply(QNetworkReply* reply, quint64 generation);
    void handleStatsReply(QNetworkReply* reply, quint64 generation);
    void handleProfileReply(QNetworkReply* reply, quint64 generation);
    void handleLeaderboardReply(QNetworkReply* reply, quint64 generation);
    void fail(const QString& message);

    SunoClient* client_;
    State state_;
    bool summaryLoading_{false};
    bool tasksLoading_{false};
    bool rewardsLoading_{false};
    bool statsLoading_{false};
    bool profileLoading_{false};
    bool leaderboardLoading_{false};
    quint64 summaryGeneration_{0};
    quint64 tasksGeneration_{0};
    quint64 rewardsGeneration_{0};
    quint64 statsGeneration_{0};
    quint64 profileGeneration_{0};
    quint64 leaderboardGeneration_{0};
};

} // namespace vc::suno
