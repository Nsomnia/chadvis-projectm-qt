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
    // An id is the one field every clip shape has. Without this, a
    // `shortcut_schema` tile (name/image_url/redirect_url, no clip fields)
    // parses into a clip with an empty id and renders as an untitled card --
    // measured against the 2026-10-24 Explore response, whose first feed is six
    // of them.
    if (direct && !direct->id.empty())
    {
        return std::move(*direct);
    }
    return std::nullopt;
}

QString feedLabel(const QJsonObject& feed, const QString& id)
{
    // `feed_title` first: the live wire names it that way (2026-10-24), and the
    // older `title`/`name` spellings are still accepted because the 2026-09-24
    // fixture used them and both shapes are in the wild.
    static const QStringList labelKeys{
        QStringLiteral("feed_title"),
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

/// The Explore envelope has had three shapes, and every one of them is still in
/// the wild, so this accepts all three instead of the newest:
///
///   1. `items[]` are clip schemas, or wrappers holding one under `item`,
///      `clip` or `clip_schema` (the 2026-09-24 capture).
///   2. `items[]` are `{content_id, content_type, content_item}` and
///      `content_item` is the song schema (the 2026-10-24 wire).
///   3. `content_item` is *itself a feed* — `content_type: "generic_feed"`,
///      carrying `feed_id`, `feed_title` and its own `items` — whose entries are
///      the clips. That nested feed is published as its own section rather than
///      flattened, because its title is the only label the user can see.
///
/// A `content_type: "shortcut"` entry is a navigation tile (`shortcut_schema`:
/// name, image_url, redirect_url) and parses as no clip; it is dropped rather
/// than rendered as an untitled card.
///
/// This is what the previous parser could not read at all: it looked for
/// `id`/`title` on the feed (the wire says `feed_id`/`feed_title`) and for a clip
/// at the top of each item (the wire wraps it in `content_item`), so every feed
/// came back labelled "Explore" with zero clips while the API was answering
/// 165 KB of feeds -- an empty page with a healthy backend (T0333).
void appendFeed(const QJsonObject& feedObject, QList<SunoExploreService::Feed>& out,
                int depth)
{
    SunoExploreService::Feed feed;
    for (const QString& key : {QStringLiteral("feed_id"), QStringLiteral("id"),
                               QStringLiteral("identifier")})
    {
        feed.id = scalarString(feedObject.value(key));
        if (!feed.id.isEmpty()) break;
    }
    feed.label = feedLabel(feedObject, feed.id);

    int expandedChildren = 0;
    const QJsonArray items = feedObject.value(QStringLiteral("items")).toArray();
    feed.clips.reserve(items.size());
    for (const QJsonValue& item : items)
    {
        QJsonValue candidate = item;
        const QJsonObject wrapper = item.toObject();
        for (const QString& key : {QStringLiteral("content_item"), QStringLiteral("item"),
                                   QStringLiteral("clip_schema"), QStringLiteral("clip")})
        {
            const QJsonValue nested = wrapper.value(key);
            if (nested.isObject())
            {
                candidate = nested;
                break;
            }
        }

        // A navigation tile is not content. `shortcut_schema` carries an `id`
        // (the destination slug), so it would otherwise pass ClipParser's
        // tolerant parse and render as a clip card with no artist or duration.
        const QJsonObject candidateObject = candidate.toObject();
        if (wrapper.value(QStringLiteral("content_type")).toString() ==
                    QLatin1String("shortcut") ||
            candidateObject.value(QStringLiteral("entity_type")).toString() ==
                    QLatin1String("shortcut_schema"))
        {
            continue;
        }

        // A nested feed: publish it as its own section, bounded at two levels.
        const bool looksLikeFeed = candidateObject.value(QStringLiteral("items")).isArray() &&
                                   (candidateObject.contains(QStringLiteral("feed_id")) ||
                                    candidateObject.contains(QStringLiteral("feed_title")) ||
                                    candidateObject.contains(QStringLiteral("presentation")));
        if (looksLikeFeed && depth < 2)
        {
            appendFeed(candidateObject, out, depth + 1);
            ++expandedChildren;
            continue;
        }

        if (auto clip = parseClipEntry(candidate))
        {
            feed.clips.push_back(std::move(*clip));
        }
    }

    // A container feed whose only content was nested sections is not itself a
    // section: publishing it would render a title above an empty row.
    if (!feed.clips.isEmpty() || expandedChildren == 0)
    {
        out.push_back(std::move(feed));
    }
}

}

SunoExploreService::SunoExploreService(SunoClient* client, QObject* parent)
    : QObject(parent)
    , client_(client)
{
    if (client_)
    {
        connect(client_, &SunoClient::authStateChanged, this, [this]() {
            if (credentialRefreshPending_)
            {
                if (client_->authState() == auth::AuthState::ActiveValid ||
                    client_->authState() == auth::AuthState::Disconnected)
                {
                    onCredentialsRestored();
                }
            }
            else if (loading_ &&
                     client_->authState() != auth::AuthState::ActiveValid)
            {
                handleAuthenticationLost();
            }
        });
        connect(client_, &SunoClient::needsReauth, this, [this]() {
            if (credentialRefreshPending_)
            {
                onCredentialsRestored();
            }
        });
        connect(client_, &SunoClient::credentialRestoreCompleted,
                this, &SunoExploreService::onCredentialsRestored);
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
        appendFeed(feedValue.toObject(), page.feeds, 0);
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

    if (!client_)
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    credentialRefreshPending_ = true;
    client_->reloadStoredCredentials();
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

    // Bounded body, and read BEFORE the transport error is judged -- deliberately.
    // SunoClient arms every reply it issues with a readyRead-driven BodyReader, so
    // a body past the studio-API cap is aborted mid-transfer and reply->error() is
    // OperationCanceledError by the time we get here; reporting that would say
    // "Operation canceled" and nothing else. readTrackedBody() carries the sticky
    // refusal out of the reader and names the limit.
    //
    // It is also the only body path that works at all now. This reply has already
    // been pumped, so reply->readAll() here would return an empty QByteArray that
    // is indistinguishable from a server that sent nothing, and the user would be
    // told "Explore returned invalid JSON".
    if (!client_)
    {
        reply->deleteLater();
        activeRequest_ = 0;
        fail(QStringLiteral("Explore has no SunoClient to read the response with"));
        return;
    }
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();
    activeRequest_ = 0;

    if (!body)
    {
        fail(body.error());
        return;
    }
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300)
    {
        fail(networkErrorText.isEmpty()
                 ? QStringLiteral("Explore request failed")
                 : networkErrorText);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
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

void SunoExploreService::onCredentialsRestored()
{
    if (!credentialRefreshPending_)
    {
        return;
    }
    credentialRefreshPending_ = false;
    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    enqueueRequest(std::nullopt, false);
}

void SunoExploreService::handleAuthenticationLost()
{
    credentialRefreshPending_ = false;
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
    credentialRefreshPending_ = false;
    activeRequest_ = 0;
    setLoading(false);
    emit failed(message);
}

}
