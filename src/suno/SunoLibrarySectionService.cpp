#include "SunoLibrarySectionService.hpp"

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

SunoLibrarySectionService::SunoLibrarySectionService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

QList<QJsonObject> SunoLibrarySectionService::parseTrashed(const QJsonObject& root)
{
    return objectsFromArray(root.value(QStringLiteral("clips")));
}

QList<QJsonObject> SunoLibrarySectionService::parseHistory(const QJsonObject& root)
{
    return objectsFromArray(root.value(QStringLiteral("history")));
}

void SunoLibrarySectionService::loadTrashed()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.records.clear();
    trashedLoading_ = true;
    ++trashedGeneration_;
    emit sectionChanged();

    const quint64 generation = trashedGeneration_;
    QPointer<SunoLibrarySectionService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::CLIPS_TRASHED_V2), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleTrashedReply(reply, generation);
            });
}

void SunoLibrarySectionService::loadHistory()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.history.clear();
    historyLoading_ = true;
    ++historyGeneration_;
    emit sectionChanged();

    const quint64 generation = historyGeneration_;
    QPointer<SunoLibrarySectionService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PROFILES_LISTEN_HISTORY), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleHistoryReply(reply, generation);
            });
}

void SunoLibrarySectionService::handleTrashedReply(QNetworkReply* reply, quint64 generation)
{
    // A reply from an abandoned load is dropped, not merged: the list is one
    // answer per request and a late one would overwrite the newer list.
    if (generation != trashedGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the trash list"));
    reply->deleteLater();

    trashedLoading_ = false;
    if (!root) {
        state_.records.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.records = parseTrashed(*root);
    emit sectionChanged();
}

void SunoLibrarySectionService::handleHistoryReply(QNetworkReply* reply, quint64 generation)
{
    if (generation != historyGeneration_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("the listen history"));
    reply->deleteLater();

    historyLoading_ = false;
    if (!root) {
        state_.history.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.history = parseHistory(*root);
    emit sectionChanged();
}

void SunoLibrarySectionService::fail(const QString& message)
{
    trashedLoading_ = false;
    historyLoading_ = false;
    state_ = {};
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
