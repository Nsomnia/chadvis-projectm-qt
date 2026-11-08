#include "SunoPreferencesService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <expected>

namespace vc::suno {

namespace {

/// Keep only object entries, in the server's order. A non-object entry is not
/// a record and has nothing a caller could read.
QList<QJsonObject> objectsFromArray(const QJsonValue& value)
{
    QList<QJsonObject> out;
    if (!value.isArray()) {
        return out;
    }
    const QJsonArray entries = value.toArray();
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (entry.isObject()) {
            out.append(entry.toObject());
        }
    }
    return out;
}

/// Status, tracked body, parsed object: the same read every section needs.
/// A body that did not arrive, a non-2xx, and unreadable JSON all become one
/// reason, so each caller has a single failure path.
std::expected<QJsonObject, QString> readObject(SunoClient& client, QNetworkReply* reply,
                                               const QString& label)
{
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client.readTrackedBody(reply);
    if (!body) {
        return std::unexpected(body.error());
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(
                QStringLiteral("Couldn't load %1 (HTTP %2).").arg(label).arg(status));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(
                QStringLiteral("Couldn't load %1: unreadable response body.").arg(label));
    }
    return document.object();
}

} // namespace

SunoPreferencesService::SunoPreferencesService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

QJsonObject SunoPreferencesService::parseSummary(const QJsonObject& root)
{
    return root;
}

QJsonObject SunoPreferencesService::parseTasks(const QJsonObject& root)
{
    return root;
}

QJsonObject SunoPreferencesService::parseRewards(const QJsonObject& root)
{
    return root;
}

QJsonObject SunoPreferencesService::parseStats(const QJsonObject& root)
{
    return root;
}

QJsonObject SunoPreferencesService::parseProfile(const QJsonObject& root)
{
    return root;
}

QJsonObject SunoPreferencesService::parseLeaderboard(const QJsonObject& root)
{
    return root;
}

QList<QJsonObject> SunoPreferencesService::parseLeaderboardEntries(const QJsonObject& root)
{
    return objectsFromArray(root.value(QStringLiteral("entries")));
}

void SunoPreferencesService::loadSummary()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.summary = {};
    summaryLoading_ = true;
    ++summaryGeneration_;
    emit sectionChanged();

    const quint64 generation = summaryGeneration_;
    QPointer<SunoPreferencesService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PREFERENCES_SUMMARY), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleSummaryReply(reply, generation);
            });
}

void SunoPreferencesService::loadTasks()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.tasks = {};
    tasksLoading_ = true;
    ++tasksGeneration_;
    emit sectionChanged();

    const quint64 generation = tasksGeneration_;
    QPointer<SunoPreferencesService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PREFERENCES_TASKS), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleTasksReply(reply, generation);
            });
}

void SunoPreferencesService::loadRewards()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.rewards = {};
    rewardsLoading_ = true;
    ++rewardsGeneration_;
    emit sectionChanged();

    const quint64 generation = rewardsGeneration_;
    QPointer<SunoPreferencesService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PREFERENCES_REWARDS), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleRewardsReply(reply, generation);
            });
}

void SunoPreferencesService::loadStats()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.stats = {};
    statsLoading_ = true;
    ++statsGeneration_;
    emit sectionChanged();

    const quint64 generation = statsGeneration_;
    QPointer<SunoPreferencesService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PREFERENCES_STATS), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleStatsReply(reply, generation);
            });
}

void SunoPreferencesService::loadProfile()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.profile = {};
    profileLoading_ = true;
    ++profileGeneration_;
    emit sectionChanged();

    const quint64 generation = profileGeneration_;
    QPointer<SunoPreferencesService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PREFERENCES_PROFILE), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleProfileReply(reply, generation);
            });
}

void SunoPreferencesService::loadLeaderboard()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.leaderboard = {};
    state_.leaderboardEntries.clear();
    leaderboardLoading_ = true;
    ++leaderboardGeneration_;
    emit sectionChanged();

    const quint64 generation = leaderboardGeneration_;
    QPointer<SunoPreferencesService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PREFERENCES_LEADERBOARD), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleLeaderboardReply(reply, generation);
            });
}

void SunoPreferencesService::handleSummaryReply(QNetworkReply* reply, quint64 generation)
{
    // A reply from an abandoned load is dropped, not merged: the section is
    // one answer per request and a late one would overwrite the newer one.
    if (generation != summaryGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the credits summary"));
    reply->deleteLater();

    summaryLoading_ = false;
    if (!root) {
        state_.summary = {};
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.summary = parseSummary(*root);
    emit sectionChanged();
}

void SunoPreferencesService::handleTasksReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != tasksGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the rating task"));
    reply->deleteLater();

    tasksLoading_ = false;
    if (!root) {
        state_.tasks = {};
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.tasks = parseTasks(*root);
    emit sectionChanged();
}

void SunoPreferencesService::handleRewardsReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != rewardsGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the rewards"));
    reply->deleteLater();

    rewardsLoading_ = false;
    if (!root) {
        state_.rewards = {};
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.rewards = parseRewards(*root);
    emit sectionChanged();
}

void SunoPreferencesService::handleStatsReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != statsGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the rater stats"));
    reply->deleteLater();

    statsLoading_ = false;
    if (!root) {
        state_.stats = {};
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.stats = parseStats(*root);
    emit sectionChanged();
}

void SunoPreferencesService::handleProfileReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != profileGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the rater profile"));
    reply->deleteLater();

    profileLoading_ = false;
    if (!root) {
        state_.profile = {};
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.profile = parseProfile(*root);
    emit sectionChanged();
}

void SunoPreferencesService::handleLeaderboardReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != leaderboardGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the leaderboard"));
    reply->deleteLater();

    leaderboardLoading_ = false;
    if (!root) {
        state_.leaderboard = {};
        state_.leaderboardEntries.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.leaderboard = parseLeaderboard(*root);
    state_.leaderboardEntries = parseLeaderboardEntries(*root);
    emit sectionChanged();
}

void SunoPreferencesService::fail(const QString& message)
{
    summaryLoading_ = false;
    tasksLoading_ = false;
    rewardsLoading_ = false;
    statsLoading_ = false;
    profileLoading_ = false;
    leaderboardLoading_ = false;
    state_ = {};
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
