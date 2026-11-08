#include "SunoSearchService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

namespace {

constexpr int routeIndex(SunoSearchService::Route route)
{
    return static_cast<int>(route);
}

} // namespace

SunoSearchService::SunoSearchService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

void SunoSearchService::suggest(const QString& term, int suggestionSize, int contentSize)
{
    if (term.isEmpty()) {
        fail(Route::Suggest, QStringLiteral("Search term is empty."));
        return;
    }
    state_.suggestionTerm = term;
    const QByteArray body = QJsonDocument(
                                    QJsonObject{{QStringLiteral("term"), term},
                                                {QStringLiteral("suggestion_size"), suggestionSize},
                                                {QStringLiteral("content_size"), contentSize}})
                                    .toJson(QJsonDocument::Compact);
    enqueue(Route::Suggest, qstr(endpoints::SEARCH_SUGGEST), body);
}

void SunoSearchService::search(const QString& term, const QString& completionMethod)
{
    if (term.isEmpty()) {
        fail(Route::OmniSearch, QStringLiteral("Search term is empty."));
        return;
    }
    state_.searchTerm = term;
    const QByteArray body = QJsonDocument(
                                    QJsonObject{{QStringLiteral("term"), term},
                                                {QStringLiteral("completion_method"), completionMethod}})
                                    .toJson(QJsonDocument::Compact);
    enqueue(Route::OmniSearch, qstr(endpoints::SEARCH_OMNISEARCH), body);
}

void SunoSearchService::enqueue(Route route, const QString& path, const QByteArray& body)
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(route, QStringLiteral("Sign in to Suno first."));
        return;
    }
    const int index = routeIndex(route);
    loading_[index] = true;
    const quint64 generation = ++generation_[index];
    emit changed();

    QPointer<SunoSearchService> guard(this);
    client_->enqueueAuthenticatedRequest(
            path, "POST", body, [guard, route, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, route, generation);
            });
}

void SunoSearchService::handleReply(QNetworkReply* reply, Route route, quint64 generation)
{
    const int index = routeIndex(route);
    // An answer from an abandoned fetch of the same route is dropped, not
    // merged: a late reply must not overwrite the newer fetch's section.
    // The other route carries its own generation, so it is unaffected.
    if (generation != generation_[index] || !client_) {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();

    if (!body) {
        fail(route, body.error());
        return;
    }
    if (status < 200 || status >= 300) {
        fail(route, QStringLiteral("Request failed (HTTP %1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(route, QStringLiteral("Unreadable response body."));
        return;
    }

    loading_[index] = false;
    state_.failureReason.clear();
    switch (route) {
    case Route::Suggest: {
        const QJsonValue feed = document.object().value(QStringLiteral("feed"));
        state_.suggestionFeed = feed.isObject() ? feed.toObject() : QJsonObject{};
        break;
    }
    case Route::OmniSearch: {
        const QJsonValue feed = document.object().value(QStringLiteral("feed"));
        state_.searchFeed = feed.isObject() ? feed.toObject() : QJsonObject{};
        state_.searchFeedId =
                state_.searchFeed.value(QStringLiteral("feed_id")).toString();
        state_.searchFeedTitle =
                state_.searchFeed.value(QStringLiteral("feed_title")).toString();
        const QJsonValue items = state_.searchFeed.value(QStringLiteral("items"));
        state_.searchItems = items.isArray() ? items.toArray() : QJsonArray{};
        break;
    }
    }
    emit changed();
}

void SunoSearchService::fail(Route route, const QString& message)
{
    const int index = routeIndex(route);
    loading_[index] = false;
    switch (route) {
    case Route::Suggest:
        state_.suggestionFeed = {};
        state_.suggestionTerm.clear();
        break;
    case Route::OmniSearch:
        state_.searchFeed = {};
        state_.searchFeedId.clear();
        state_.searchFeedTitle.clear();
        state_.searchItems = {};
        state_.searchTerm.clear();
        break;
    }
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
