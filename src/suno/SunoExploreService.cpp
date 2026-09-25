#include "SunoExploreService.hpp"

#include "ClipParser.hpp"
#include "SunoClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringList>

#include <optional>

namespace vc::suno {

namespace {

QString scalarString(const QJsonValue& value)
{
    if (value.isString())
    {
        return value.toString();
    }
    if (value.isDouble())
    {
        return QString::number(value.toDouble(), 'g', 15);
    }
    return {};
}

std::optional<SunoClip> parseClipEntry(const QJsonValue& value, int depth = 0)
{
    if (!value.isObject() || depth >= 4)
    {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    static const QStringList candidateKeys{
        QStringLiteral("item"),
        QStringLiteral("clip_schema"),
        QStringLiteral("clip"),
    };

    for (const QString& key : candidateKeys)
    {
        const QJsonValue candidate = object.value(key);
        if (!candidate.isObject())
        {
            continue;
        }
        if (auto nested = parseClipEntry(candidate, depth + 1))
        {
            return nested;
        }
    }

    auto direct = ClipParser::parseClip(object);
    if (direct)
    {
        return std::move(*direct);
    }
    return std::nullopt;
}

QString feedLabel(const QJsonObject& feed, const QString& id)
{
    static const QStringList labelKeys{
        QStringLiteral("title"),
        QStringLiteral("name"),
        QStringLiteral("label"),
        QStringLiteral("display_name"),
    };
    for (const QString& key : labelKeys)
    {
        const QString value = scalarString(feed.value(key));
        if (!value.isEmpty())
        {
            return value;
        }
    }
    return id.isEmpty() ? QStringLiteral("Explore") : id;
}

}

SunoExploreService::SunoExploreService(SunoClient* client, QObject* parent)
    : QObject(parent)
    , client_(client)
{
    if (client_)
    {
        connect(client_, &SunoClient::authStateChanged, this, [this]() {
            if (loading_ && client_->authState() != auth::AuthState::ActiveValid)
            {
                handleAuthenticationLost();
            }
        });
    }
}

QJsonObject SunoExploreService::requestBody(std::optional<QString> cursor)
{
    QJsonObject body;
    body[QStringLiteral("cursor")] =
        cursor.has_value() ? QJsonValue(*cursor) : QJsonValue::Null;
    return body;
}

std::expected<SunoExploreService::Page, QString>
SunoExploreService::parsePage(const QJsonObject& root)
{
    const QJsonValue feedsValue = root.value(QStringLiteral("feeds"));
    if (!feedsValue.isArray())
    {
        return std::unexpected(QStringLiteral("Explore response has no feeds array"));
    }

    Page page;
    const QJsonArray feedValues = feedsValue.toArray();
    page.feeds.reserve(feedValues.size());

    for (const QJsonValue& feedValue : feedValues)
    {
        if (!feedValue.isObject())
        {
            continue;
        }

        const QJsonObject feedObject = feedValue.toObject();
        Feed feed;
        feed.id = scalarString(feedObject.value(QStringLiteral("id")));
        if (feed.id.isEmpty())
        {
            feed.id = scalarString(feedObject.value(QStringLiteral("identifier")));
        }
        feed.label = feedLabel(feedObject, feed.id);

        const QJsonValue itemsValue = feedObject.value(QStringLiteral("items"));
        if (itemsValue.isArray())
        {
            const QJsonArray items = itemsValue.toArray();
            feed.clips.reserve(items.size());
            for (const QJsonValue& item : items)
            {
                if (auto clip = parseClipEntry(item))
                {
                    feed.clips.push_back(std::move(*clip));
                }
            }
        }
        page.feeds.push_back(std::move(feed));
    }

    const QJsonValue cursorValue = root.value(QStringLiteral("next_cursor"));
    if (cursorValue.isString())
    {
        page.nextCursor = cursorValue.toString();
    }
    return page;
}

void SunoExploreService::refresh()
{
    if (loading_)
    {
        return;
    }

    feeds_.clear();
    if (hasMore())
    {
        nextCursor_.clear();
        emit hasMoreChanged();
    }
    emit reset();
    setLoading(true);

    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    enqueueRequest(std::nullopt, false);
}

void SunoExploreService::loadMore()
{
    if (loading_ || !hasMore() || nextCursor_.isEmpty())
    {
        return;
    }
    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    setLoading(true);
    enqueueRequest(nextCursor_, true);
}

void SunoExploreService::enqueueRequest(std::optional<QString> cursor, bool append)
{
    const quint64 requestId = ++activeRequest_;
    QPointer<SunoExploreService> guard(this);
    const QByteArray payload =
        QJsonDocument(requestBody(std::move(cursor))).toJson(QJsonDocument::Compact);
    client_->enqueueAuthenticatedRequest(
        qstr(endpoints::UNIFIED_EXPLORE), "POST", payload,
        [guard, append, requestId](QNetworkReply* reply) {
            if (!guard)
            {
                reply->deleteLater();
                return;
            }
            guard->handleReply(reply, append, requestId);
        });
}

void SunoExploreService::handleReply(QNetworkReply* reply, bool append,
                                     quint64 requestId)
{
    if (requestId != activeRequest_)
    {
        reply->deleteLater();
        return;
    }

    const QNetworkReply::NetworkError networkError = reply->error();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkErrorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();
    activeRequest_ = 0;

    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300)
    {
        fail(networkErrorText.isEmpty()
                 ? QStringLiteral("Explore request failed")
                 : networkErrorText);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        fail(QStringLiteral("Explore returned invalid JSON"));
        return;
    }

    auto parsed = parsePage(document.object());
    if (!parsed)
    {
        fail(parsed.error());
        return;
    }

    if (!append)
    {
        feeds_.clear();
    }
    feeds_.append(std::move(parsed->feeds));
    const bool hadMore = hasMore();
    nextCursor_ = std::move(parsed->nextCursor);
    if (hadMore != hasMore())
    {
        emit hasMoreChanged();
    }
    emit pageReady();
    setLoading(false);
}

void SunoExploreService::handleAuthenticationLost()
{
    fail(QStringLiteral("Not authenticated"));
}

void SunoExploreService::setLoading(bool loading)
{
    if (loading_ == loading)
    {
        return;
    }
    loading_ = loading;
    emit loadingChanged();
}

void SunoExploreService::fail(const QString& message)
{
    activeRequest_ = 0;
    setLoading(false);
    emit failed(message);
}

}
