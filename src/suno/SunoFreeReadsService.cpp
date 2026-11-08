#include "SunoFreeReadsService.hpp"

#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

namespace {

constexpr int routeIndex(SunoFreeReadsService::Route route)
{
    return static_cast<int>(route);
}

} // namespace

SunoFreeReadsService::SunoFreeReadsService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

std::expected<QList<SunoFreeReadsService::CoverArtSuggestion>, QString>
SunoFreeReadsService::parseCoverArtSuggestions(const QJsonObject& root)
{
    const QJsonValue raw = root.value(QStringLiteral("items"));
    if (!raw.isArray()) {
        return QList<CoverArtSuggestion>{};
    }
    QList<CoverArtSuggestion> out;
    const QJsonArray entries = raw.toArray();
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject object = entry.toObject();
        CoverArtSuggestion suggestion;
        suggestion.slug = object.value(QStringLiteral("slug")).toString();
        suggestion.label = object.value(QStringLiteral("label")).toObject();
        suggestion.prompt = object.value(QStringLiteral("prompt")).toObject();
        out.append(std::move(suggestion));
    }
    return out;
}

void SunoFreeReadsService::fetchClip(const QString& clipId)
{
    const QString path =
            QString(qstr(endpoints::CLIP_BY_ID)).replace(QStringLiteral("{}"), clipId);
    state_.clipId = clipId;
    enqueue(Route::Clip, path, "GET", {});
}

void SunoFreeReadsService::fetchPinnedClips(const QString& projectId)
{
    const QString path = QString(qstr(endpoints::PROJECT_PINNED_CLIPS_BY_ID))
                                 .replace(QStringLiteral("{}"), projectId);
    state_.pinnedProjectId = projectId;
    enqueue(Route::PinnedClips, path, "GET", {});
}

void SunoFreeReadsService::fetchCoverArtSuggestions()
{
    enqueue(Route::CoverArtSuggestions, qstr(endpoints::CMS_COVER_ART_SUGGESTIONS), "GET", {});
}

void SunoFreeReadsService::fetchRealtimeToken(const QStringList& channelPatterns)
{
    QJsonArray capabilities;
    for (const QString& pattern : channelPatterns) {
        capabilities.append(QJsonObject{{QStringLiteral("channel_pattern"), pattern},
                                        {QStringLiteral("subscribe"), true}});
    }
    const QByteArray body =
            QJsonDocument(QJsonObject{{QStringLiteral("capabilities"), capabilities}})
                    .toJson(QJsonDocument::Compact);
    enqueue(Route::RealtimeToken, qstr(endpoints::REALTIME_TOKEN), "POST", body);
}

void SunoFreeReadsService::fetchChallengeProgress()
{
    enqueue(Route::ChallengeProgress, qstr(endpoints::CHALLENGE_PROGRESS), "GET", {});
}

void SunoFreeReadsService::enqueue(Route route, const QString& path, const QString& method,
                                   const QByteArray& data)
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(route, QStringLiteral("Sign in to Suno first."));
        return;
    }
    const int index = routeIndex(route);
    loading_[index] = true;
    const quint64 generation = ++generation_[index];
    emit changed();

    QPointer<SunoFreeReadsService> guard(this);
    client_->enqueueAuthenticatedRequest(path, method.toStdString(), data,
                                         [guard, route, generation](QNetworkReply* reply) {
                                             if (!guard) {
                                                 reply->deleteLater();
                                                 return;
                                             }
                                             guard->handleReply(reply, route, generation);
                                         });
}

void SunoFreeReadsService::handleReply(QNetworkReply* reply, Route route, quint64 generation)
{
    const int index = routeIndex(route);
    // An answer from an abandoned fetch of the same route is dropped, not
    // merged: a late reply must not overwrite the newer fetch's section.
    // Other routes carry their own generation, so they are unaffected.
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
    if (parseError.error != QJsonParseError::NoError) {
        fail(route, QStringLiteral("Unreadable response body."));
        return;
    }

    loading_[index] = false;
    state_.failureReason.clear();
    switch (route) {
    case Route::Clip:
        if (!document.isObject()) {
            fail(route, QStringLiteral("Unreadable response body."));
            return;
        }
        state_.clip = document.object();
        break;
    case Route::PinnedClips:
        state_.pinnedClips = document.isArray() ? QJsonValue(document.array())
                                                : QJsonValue(document.object());
        break;
    case Route::CoverArtSuggestions: {
        if (!document.isObject()) {
            fail(route, QStringLiteral("Unreadable response body."));
            return;
        }
        auto parsed = parseCoverArtSuggestions(document.object());
        if (!parsed) {
            fail(route, parsed.error());
            return;
        }
        state_.coverArtSuggestions = std::move(*parsed);
        break;
    }
    case Route::RealtimeToken:
        if (!document.isObject()) {
            fail(route, QStringLiteral("Unreadable response body."));
            return;
        }
        state_.realtimeToken = document.object();
        break;
    case Route::ChallengeProgress:
        if (!document.isObject()) {
            fail(route, QStringLiteral("Unreadable response body."));
            return;
        }
        state_.challengeProgress = document.object();
        break;
    }
    emit changed();
}

void SunoFreeReadsService::fail(Route route, const QString& message)
{
    const int index = routeIndex(route);
    loading_[index] = false;
    switch (route) {
    case Route::Clip:
        state_.clip = {};
        break;
    case Route::PinnedClips:
        state_.pinnedClips = {};
        break;
    case Route::CoverArtSuggestions:
        state_.coverArtSuggestions = {};
        break;
    case Route::RealtimeToken:
        state_.realtimeToken = {};
        break;
    case Route::ChallengeProgress:
        state_.challengeProgress = {};
        break;
    }
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
