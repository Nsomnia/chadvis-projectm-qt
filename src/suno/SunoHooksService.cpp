#include "SunoHooksService.hpp"

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

/// No list key is pinned for the Hooks family (§4.12), so the first
/// recognised key wins. The fallback accepts any array-of-objects value so a
/// future rename degrades to the same list instead of an empty tab.
QList<QJsonObject> listFromRoot(const QJsonObject& root)
{
    static constexpr const char* keys[] = {"hooks",   "clips",    "items",
                                           "results", "data",     "suggested_clips",
                                           "user_hooks", "liked_hooks", "feed"};
    for (const char* key : keys) {
        const QJsonValue value = root.value(QString::fromLatin1(key));
        if (value.isArray()) {
            return objectsFromArray(value);
        }
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.value().isArray()) {
            return objectsFromArray(it.value());
        }
    }
    return {};
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
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(
                QStringLiteral("Couldn't load %1: unreadable response body.").arg(label));
    }
    if (document.isArray()) {
        QJsonObject root;
        root.insert(QStringLiteral("hooks"), document.array());
        return root;
    }
    if (!document.isObject()) {
        return std::unexpected(
                QStringLiteral("Couldn't load %1: unreadable response body.").arg(label));
    }
    return document.object();
}

} // namespace

SunoHooksService::SunoHooksService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

QList<QJsonObject> SunoHooksService::parseUserHooks(const QJsonObject& root)
{
    return listFromRoot(root);
}

QList<QJsonObject> SunoHooksService::parseLiked(const QJsonObject& root)
{
    return listFromRoot(root);
}

QList<QJsonObject> SunoHooksService::parseSuggested(const QJsonObject& root)
{
    return listFromRoot(root);
}

void SunoHooksService::loadUserHooks()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.userHooks.clear();
    userHooksLoading_ = true;
    ++userHooksGeneration_;
    emit sectionChanged();

    const quint64 generation = userHooksGeneration_;
    QPointer<SunoHooksService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::VIDEO_HOOKS_USER_HOOKS), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleUserHooksReply(reply, generation);
            });
}

void SunoHooksService::loadLiked()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.likedHooks.clear();
    likedLoading_ = true;
    ++likedGeneration_;
    emit sectionChanged();

    const quint64 generation = likedGeneration_;
    QPointer<SunoHooksService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::VIDEO_HOOKS_ME_LIKED), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleLikedReply(reply, generation);
            });
}

void SunoHooksService::loadSuggested()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.suggestedClips.clear();
    suggestedLoading_ = true;
    ++suggestedGeneration_;
    emit sectionChanged();

    const quint64 generation = suggestedGeneration_;
    QPointer<SunoHooksService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::VIDEO_HOOKS_SUGGESTED_CLIPS), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleSuggestedReply(reply, generation);
            });
}

void SunoHooksService::handleUserHooksReply(QNetworkReply* reply, quint64 generation)
{
    // A reply from an abandoned load is dropped, not merged: the list is one
    // answer per request and a late one would overwrite the newer list.
    if (generation != userHooksGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root =
            readObject(*client_, reply, QStringLiteral("your hooks"));
    reply->deleteLater();

    userHooksLoading_ = false;
    if (!root) {
        state_.userHooks.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.userHooks = parseUserHooks(*root);
    emit sectionChanged();
}

void SunoHooksService::handleLikedReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != likedGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root =
            readObject(*client_, reply, QStringLiteral("your liked hooks"));
    reply->deleteLater();

    likedLoading_ = false;
    if (!root) {
        state_.likedHooks.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.likedHooks = parseLiked(*root);
    emit sectionChanged();
}

void SunoHooksService::handleSuggestedReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != suggestedGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root =
            readObject(*client_, reply, QStringLiteral("suggested clips"));
    reply->deleteLater();

    suggestedLoading_ = false;
    if (!root) {
        state_.suggestedClips.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.suggestedClips = parseSuggested(*root);
    emit sectionChanged();
}

void SunoHooksService::fail(const QString& message)
{
    userHooksLoading_ = false;
    likedLoading_ = false;
    suggestedLoading_ = false;
    state_ = {};
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
