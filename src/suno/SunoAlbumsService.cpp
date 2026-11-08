#include "SunoAlbumsService.hpp"

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

QJsonObject feedObject(const QJsonObject& root)
{
    const QJsonValue feedValue = root.value(QStringLiteral("feed"));
    return feedValue.isObject() ? feedValue.toObject() : QJsonObject{};
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
        return std::unexpected(QStringLiteral("Couldn't load %1 (HTTP %2).").arg(label).arg(status));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(QStringLiteral("Couldn't load %1: unreadable response body.").arg(label));
    }
    return document.object();
}

} // namespace

SunoAlbumsService::SunoAlbumsService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

QJsonObject SunoAlbumsService::requestBody()
{
    QJsonObject metadata;
    metadata.insert(QStringLiteral("term"), QStringLiteral(""));
    metadata.insert(QStringLiteral("library_scope"), QStringLiteral("all"));

    QJsonObject body;
    body.insert(QStringLiteral("feed_id"), QStringLiteral("library_albums"));
    body.insert(QStringLiteral("cursor"), QJsonValue::Null);
    body.insert(QStringLiteral("page_size"), 12);
    body.insert(QStringLiteral("request_metadata"), metadata);
    return body;
}

QList<QJsonObject> SunoAlbumsService::parseAlbums(const QJsonObject& root)
{
    return objectsFromArray(feedObject(root).value(QStringLiteral("items")));
}

QString SunoAlbumsService::parseFeedTitle(const QJsonObject& root)
{
    return feedObject(root).value(QStringLiteral("feed_title")).toString();
}

QString SunoAlbumsService::parseFeedId(const QJsonObject& root)
{
    return feedObject(root).value(QStringLiteral("feed_id")).toString();
}

void SunoAlbumsService::loadAlbums()
{
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    state_.albums.clear();
    state_.feedTitle.clear();
    state_.feedId.clear();
    state_.failureReason.clear();
    loading_ = true;
    ++generation_;
    emit sectionChanged();

    const quint64 generation = generation_;
    QPointer<SunoAlbumsService> guard(this);
    const QByteArray payload = QJsonDocument(requestBody()).toJson(QJsonDocument::Compact);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::UNIFIED_FEED), "POST", payload,
            [guard, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, generation);
            });
}

void SunoAlbumsService::handleReply(QNetworkReply* reply, quint64 generation)
{
    // A reply from an abandoned load is dropped, not merged: the list is one
    // answer per request and a late one would overwrite the newer list.
    if (generation != generation_ || !client_) {
        reply->deleteLater();
        return;
    }
    const auto root = readObject(*client_, reply, QStringLiteral("your albums"));
    reply->deleteLater();

    loading_ = false;
    if (!root) {
        state_.albums.clear();
        state_.feedTitle.clear();
        state_.feedId.clear();
        state_.failureReason = root.error();
        emit sectionChanged();
        emit failed(root.error());
        return;
    }
    state_.albums = parseAlbums(*root);
    state_.feedTitle = parseFeedTitle(*root);
    state_.feedId = parseFeedId(*root);
    emit sectionChanged();
}

void SunoAlbumsService::fail(const QString& message)
{
    loading_ = false;
    state_ = {};
    state_.failureReason = message;
    emit failed(message);
}

} // namespace vc::suno
