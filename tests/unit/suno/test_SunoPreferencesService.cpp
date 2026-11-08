#include <QtTest>

#include "suno/SunoClient.hpp"
#include "suno/SunoPreferencesService.hpp"
#include "FakeNetworkReply.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QPointer>

#include <cstring>
#include <vector>

using namespace vc::suno;
using fake_net::FakeReply;

namespace {


/// A JWT that decodes and carries a future `exp`, so setToken accepts it.
QString fakeBearer()
{
    const auto encode = [](const QJsonObject& object) {
        return QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    const QString header = encode({{QStringLiteral("alg"), QStringLiteral("NONE")},
                                   {QStringLiteral("typ"), QStringLiteral("JWT")}});
    const QString payload = encode({{QStringLiteral("exp"), static_cast<qint64>(4102444800)}});
    return header + QLatin1Char('.') + payload + QStringLiteral(".signature");
}

CredentialStoreWorker::Backend emptyBackend()
{
    return [](CredentialStoreWorker::Request) { return CredentialStoreWorker::Outcome{}; };
}

/// Fixture bodies per ENDPOINT-INVENTORY §5.15. Every fixture carries an
/// `unknown_future_field` on purpose: the parser must keep what it does not
/// know rather than map onto invented types.
QByteArray summaryBody()
{
    const QJsonObject nested{{QStringLiteral("nested"), QJsonArray{1, 2}}};
    const QJsonObject obj{{QStringLiteral("tasks_completed"), 42},
                          {QStringLiteral("credits_processed"), 120},
                          {QStringLiteral("credits_waiting"), 15},
                          {QStringLiteral("tasks_available"), 3},
                          {QStringLiteral("daily_submit_limit_reached"), false},
                          {QStringLiteral("daily_submit_cap"), 200},
                          {QStringLiteral("next_available_in_seconds"), 0},
                          {QStringLiteral("rater_profile_required"), false},
                          {QStringLiteral("rater_profile_completed"), true},
                          {QStringLiteral("unknown_future_field"), nested}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray tasksBody()
{
    const QJsonObject a{{QStringLiteral("key"), QStringLiteral("clip_a")},
                        {QStringLiteral("url"), QStringLiteral("https://example.invalid/clip_a.mp3")}};
    const QJsonObject b{{QStringLiteral("key"), QStringLiteral("clip_b")},
                        {QStringLiteral("url"), QStringLiteral("https://example.invalid/clip_b.mp3")}};
    const QJsonObject obj{{QStringLiteral("task_id"), QStringLiteral("task-123")},
                          {QStringLiteral("review_items"), QJsonArray{a, b}},
                          {QStringLiteral("project"), QStringLiteral("listen-and-rank")},
                          {QStringLiteral("project_category"), QStringLiteral("audio")},
                          {QStringLiteral("unknown_future_field"), true}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray rewardsBody()
{
    const QJsonObject claim{{QStringLiteral("reward_key"), QStringLiteral("streak-7")},
                            {QStringLiteral("credits"), 50}};
    const QJsonObject next{{QStringLiteral("reward_key"), QStringLiteral("streak-7")},
                           {QStringLiteral("credits"), 50},
                           {QStringLiteral("interval_days"), 7},
                           {QStringLiteral("days_remaining"), 4}};
    const QJsonObject obj{{QStringLiteral("current_streak_days"), 3},
                          {QStringLiteral("tasks_today"), 7},
                          {QStringLiteral("min_tasks_per_streak_day"), 5},
                          {QStringLiteral("claimable"), QJsonArray{claim}},
                          {QStringLiteral("next_reward"), next},
                          {QStringLiteral("unknown_future_field"), QStringLiteral("kept")}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray statsBody()
{
    const QJsonObject obj{{QStringLiteral("tasks_completed"), 42},
                          {QStringLiteral("credits_earned"), 135},
                          {QStringLiteral("listening_seconds"), 9000},
                          {QStringLiteral("projects_contributed"), 2},
                          {QStringLiteral("active_days"), 10},
                          {QStringLiteral("current_streak_days"), 3},
                          {QStringLiteral("longest_streak_days"), 6},
                          {QStringLiteral("unknown_future_field"), 99}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray profileBody()
{
    const QJsonObject obj{{QStringLiteral("creator_type"), QStringLiteral("listener")},
                          {QStringLiteral("musical_background"), QStringLiteral("hobbyist")},
                          {QStringLiteral("preferred_genres"),
                           QJsonArray{QStringLiteral("electronic")}},
                          {QStringLiteral("unknown_future_field"), QJsonArray{1}}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray leaderboardBody()
{
    const QJsonObject one{{QStringLiteral("rank"), 1},
                          {QStringLiteral("handle"), QStringLiteral("rater-one")},
                          {QStringLiteral("display_name"), QStringLiteral("Rater One")},
                          {QStringLiteral("avatar_image_url"),
                           QStringLiteral("https://example.invalid/one.png")}};
    const QJsonObject two{{QStringLiteral("rank"), 2},
                          {QStringLiteral("handle"), QStringLiteral("rater-two")},
                          {QStringLiteral("display_name"), QStringLiteral("Rater Two")},
                          {QStringLiteral("avatar_image_url"),
                           QStringLiteral("https://example.invalid/two.png")}};
    const QJsonObject obj{{QStringLiteral("window"), QStringLiteral("daily")},
                          {QStringLiteral("metric"), QStringLiteral("credits")},
                          {QStringLiteral("window_start"), QStringLiteral("2026-11-06T00:00:00Z")},
                          {QStringLiteral("window_end"), QStringLiteral("2026-11-07T00:00:00Z")},
                          {QStringLiteral("entries"), QJsonArray{one, two}},
                          {QStringLiteral("unknown_future_field"), QStringLiteral("kept")}};
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

FakeReply* findReply(const std::vector<QPointer<FakeReply>>& replies,
                         const QString& suffix, int occurrence = 0)
{
    int seen = 0;
    for (QPointer<FakeReply> reply : replies) {
        if (reply.isNull()) {
            continue;
        }
        if (!reply->url().path().endsWith(suffix)) {
            continue;
        }
        if (seen == occurrence) {
            return reply;
        }
        ++seen;
    }
    return nullptr;
}

} // namespace

class TestSunoPreferencesService : public QObject
{
    Q_OBJECT

private slots:
    void parsesEachEnvelope()
    {
        const auto summaryDoc = QJsonDocument::fromJson(summaryBody());
        QVERIFY(summaryDoc.isObject());
        const auto summary = SunoPreferencesService::parseSummary(summaryDoc.object());
        QCOMPARE(summary.value(QStringLiteral("tasks_completed")).toInt(), 42);
        QCOMPARE(summary.value(QStringLiteral("daily_submit_cap")).toInt(), 200);
        QVERIFY(summary.contains(QStringLiteral("unknown_future_field")));

        const auto tasksDoc = QJsonDocument::fromJson(tasksBody());
        QVERIFY(tasksDoc.isObject());
        const auto tasks = SunoPreferencesService::parseTasks(tasksDoc.object());
        QCOMPARE(tasks.value(QStringLiteral("task_id")).toString(), QStringLiteral("task-123"));
        QCOMPARE(tasks.value(QStringLiteral("review_items")).toArray().size(), 2);
        QVERIFY(tasks.contains(QStringLiteral("unknown_future_field")));

        const auto rewardsDoc = QJsonDocument::fromJson(rewardsBody());
        QVERIFY(rewardsDoc.isObject());
        const auto rewards = SunoPreferencesService::parseRewards(rewardsDoc.object());
        QCOMPARE(rewards.value(QStringLiteral("current_streak_days")).toInt(), 3);
        QCOMPARE(rewards.value(QStringLiteral("min_tasks_per_streak_day")).toInt(), 5);
        QCOMPARE(rewards.value(QStringLiteral("next_reward")).toObject()
                         .value(QStringLiteral("credits"))
                         .toInt(),
                 50);

        const auto statsDoc = QJsonDocument::fromJson(statsBody());
        QVERIFY(statsDoc.isObject());
        const auto stats = SunoPreferencesService::parseStats(statsDoc.object());
        QCOMPARE(stats.value(QStringLiteral("credits_earned")).toInt(), 135);
        QCOMPARE(stats.value(QStringLiteral("longest_streak_days")).toInt(), 6);

        const auto profileDoc = QJsonDocument::fromJson(profileBody());
        QVERIFY(profileDoc.isObject());
        const auto profile = SunoPreferencesService::parseProfile(profileDoc.object());
        QCOMPARE(profile.value(QStringLiteral("creator_type")).toString(),
                 QStringLiteral("listener"));

        const auto boardDoc = QJsonDocument::fromJson(leaderboardBody());
        QVERIFY(boardDoc.isObject());
        const auto board = SunoPreferencesService::parseLeaderboard(boardDoc.object());
        QCOMPARE(board.value(QStringLiteral("window")).toString(), QStringLiteral("daily"));
        const auto entries = SunoPreferencesService::parseLeaderboardEntries(boardDoc.object());
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].value(QStringLiteral("rank")).toInt(), 1);
        QCOMPARE(entries[0].value(QStringLiteral("handle")).toString(),
                 QStringLiteral("rater-one"));
    }

    void emptyProfileAndMissingEntriesAreNotFailures()
    {
        QVERIFY(SunoPreferencesService::parseProfile(QJsonObject{}).isEmpty());
        QVERIFY(SunoPreferencesService::parseLeaderboardEntries(QJsonObject{}).isEmpty());
        QVERIFY(SunoPreferencesService::parseLeaderboardEntries(
                        QJsonObject{{QStringLiteral("entries"), QStringLiteral("nope")}})
                        .isEmpty());
    }

    void noClientReportsSignInAndStaysEmpty()
    {
        SunoPreferencesService service(nullptr);
        QSignalSpy failed(&service, &SunoPreferencesService::failed);

        service.loadSummary();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("Sign in to Suno first."));
        QCOMPARE(service.failureReason(), QStringLiteral("Sign in to Suno first."));
        QVERIFY(service.summary().isEmpty());
        QVERIFY(!service.isLoading());

        service.loadTasks();
        service.loadRewards();
        service.loadStats();
        service.loadProfile();
        service.loadLeaderboard();

        QCOMPARE(failed.count(), 6);
        QVERIFY(service.tasks().isEmpty());
        QVERIFY(service.rewards().isEmpty());
        QVERIFY(service.stats().isEmpty());
        QVERIFY(service.profile().isEmpty());
        QVERIFY(service.leaderboard().isEmpty());
        QVERIFY(service.leaderboardEntries().isEmpty());
        QVERIFY(!service.isLoading());
    }

    void aNonJsonBodyDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());
        QVERIFY(client.isAuthenticated());

        SunoPreferencesService service(&client);
        QSignalSpy failed(&service, &SunoPreferencesService::failed);

        service.loadSummary();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        QVERIFY(service.isLoading());
        QVERIFY(service.isLoadingSummary());
        FakeReply* reply = findReply(replies, QStringLiteral("/preferences/summary"));
        QVERIFY(reply != nullptr);

        reply->respond(QByteArrayLiteral("not json"), 200);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("unreadable")));
        QVERIFY(service.summary().isEmpty());
    }

    void anHttpErrorAfterRetryDegradesToEmptyWithAReason()
    {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPreferencesService service(&client);
        QSignalSpy failed(&service, &SunoPreferencesService::failed);

        service.loadTasks();
        QTRY_VERIFY_WITH_TIMEOUT(!replies.empty(), 15000);
        FakeReply* first = findReply(replies, QStringLiteral("/preferences/tasks"));
        QVERIFY(first != nullptr);
        first->respond(QByteArrayLiteral("{}"), 500);

        // 5xx is transient: SunoClient re-sends once after its backoff floor, so
        // only the second reply's status reaches the service. The first reply is
        // deleted before the retry, so it no longer counts as an occurrence.
        QTRY_VERIFY_WITH_TIMEOUT(replies.size() >= 2, 15000);
        FakeReply* second = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (second = findReply(replies, QStringLiteral("/preferences/tasks"))) != nullptr,
                15000);
        second->respond(QByteArrayLiteral("{}"), 500);

        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 15000);
        QVERIFY(!service.isLoading());
        QVERIFY(service.failureReason().contains(QStringLiteral("500")));
        QVERIFY(service.tasks().isEmpty());
    }

    // TODO(T0365): success path has not run yet -- the orchestrator build is the
    // first execution. Analysis (and any fixture correction) happens after it.
    void successfulLoadsFillEverySection()
    {
        std::vector<QPointer<FakeReply>> replies;
        auto factory = [&replies](const QNetworkRequest& request, const std::string& method,
                                  const QByteArray&) {
            auto* reply = new FakeReply(request, method);
            replies.push_back(reply);
            return reply;
        };
        SunoClient client(QStringLiteral("test-device"), nullptr, emptyBackend(), factory);
        client.setToken(fakeBearer().toStdString());

        SunoPreferencesService service(&client);
        QSignalSpy changed(&service, &SunoPreferencesService::sectionChanged);

        // Loaded one at a time, so queue serialisation cannot starve a
        // later request behind an earlier one.
        service.loadSummary();
        FakeReply* summary = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (summary = findReply(replies, QStringLiteral("/preferences/summary"))) != nullptr,
                15000);
        summary->respond(summaryBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.summary().value(QStringLiteral("tasks_completed")).toInt() == 42, 15000);

        service.loadTasks();
        FakeReply* tasks = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (tasks = findReply(replies, QStringLiteral("/preferences/tasks"))) != nullptr,
                15000);
        tasks->respond(tasksBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.tasks().value(QStringLiteral("task_id")).toString() == QStringLiteral("task-123"), 15000);

        service.loadRewards();
        FakeReply* rewards = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (rewards = findReply(replies, QStringLiteral("/preferences/rewards"))) != nullptr,
                15000);
        rewards->respond(rewardsBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.rewards().value(QStringLiteral("current_streak_days")).toInt() == 3, 15000);

        service.loadStats();
        FakeReply* stats = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (stats = findReply(replies, QStringLiteral("/preferences/stats"))) != nullptr,
                15000);
        stats->respond(statsBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.stats().value(QStringLiteral("credits_earned")).toInt() == 135, 15000);

        service.loadProfile();
        FakeReply* profile = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (profile = findReply(replies, QStringLiteral("/preferences/profile"))) != nullptr,
                15000);
        profile->respond(profileBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.profile().value(QStringLiteral("creator_type")).toString() == QStringLiteral("listener"), 15000);

        service.loadLeaderboard();
        FakeReply* board = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
                (board = findReply(replies, QStringLiteral("/preferences/leaderboard"))) != nullptr,
                15000);
        board->respond(leaderboardBody(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(service.leaderboardEntries().size() == 2, 15000);

        QCOMPARE(changed.count(), 12);
        QVERIFY(service.failureReason().isEmpty());
        QVERIFY(!service.isLoading());
    }
};

QTEST_MAIN(TestSunoPreferencesService)
#include "test_SunoPreferencesService.moc"
