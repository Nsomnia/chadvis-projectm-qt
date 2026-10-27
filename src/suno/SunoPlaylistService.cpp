#include "SunoPlaylistService.hpp"

#include "ClipParser.hpp"
#include "SunoClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace vc::suno {

namespace {

SunoPlaylistService::Summary parseSummary(const QJsonObject& object)
{
    SunoPlaylistService::Summary summary;
    summary.id = json::stringValue(object.value(QStringLiteral("id")));
    summary.name = json::stringValue(object.value(QStringLiteral("name")));
    summary.description = json::stringValue(object.value(QStringLiteral("description")));
    summary.imageUrl = json::stringValue(object.value(QStringLiteral("image_url")));
    summary.userDisplayName = json::stringValue(object.value(QStringLiteral("user_display_name")));
    summary.userHandle = json::stringValue(object.value(QStringLiteral("user_handle")));
    // Both counts ride on every captured playlist object; `num_total_results`
    // is the paging denominator, so it is read as an integer and never as a
    // string. A missing one leaves 0, which `hasMore()` treats as "unknown, keep
    // asking until a page comes back empty".
    summary.numTotalResults =
            static_cast<int>(json::integerValue(object.value(QStringLiteral("num_total_results"))).value_or(0));
    summary.songCount =
            static_cast<int>(json::integerValue(object.value(QStringLiteral("song_count"))).value_or(0));
    summary.totalDuration = json::integerValue(object.value(QStringLiteral("total_duration"))).value_or(0);
    summary.isOwned = json::booleanValue(object.value(QStringLiteral("is_owned")));
    summary.isPublic = json::booleanValue(object.value(QStringLiteral("is_public")));
    summary.isTrashed = json::booleanValue(object.value(QStringLiteral("is_trashed")));
    return summary;
}

} // namespace

SunoPlaylistService::SunoPlaylistService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client)
{
    if (!client_)
    {
        return;
    }
    // Same two-signal dance as SunoExploreService: a refresh that re-authenticates
    // continues the request that was waiting, and losing auth mid-flight fails the
    // pending one instead of hanging the spinner.
    connect(client_, &SunoClient::authStateChanged, this, [this]() {
        if (credentialRefreshPending_)
        {
            if (client_->authState() == auth::AuthState::ActiveValid ||
                client_->authState() == auth::AuthState::Disconnected)
            {
                onCredentialsRestored();
            }
        } else if (loading_ && client_->authState() != auth::AuthState::ActiveValid)
        {
            fail(QStringLiteral("Not authenticated"));
        }
    });
    connect(client_, &SunoClient::needsReauth, this, [this]() {
        if (credentialRefreshPending_)
        {
            onCredentialsRestored();
        }
    });
    connect(client_, &SunoClient::credentialRestoreCompleted,
            this, &SunoPlaylistService::onCredentialsRestored);
}

std::expected<QList<SunoPlaylistService::Summary>, QString>
SunoPlaylistService::parseList(const QJsonValue& root)
{
    QJsonArray entries;
    if (root.isArray())
    {
        entries = root.toArray();
    } else if (root.isObject())
    {
        // `playlist/me` was never body-captured; the library listing and the
        // bundle both name this family `playlists`, and the other two spellings
        // cost one line each to keep. Guessing one and refusing the rest would
        // turn a correct-but-renamed envelope into an empty page.
        static const QStringList envelopeKeys{
            QStringLiteral("playlists"),
            QStringLiteral("playlist_list"),
            QStringLiteral("results"),
        };
        const QJsonObject object = root.toObject();
        for (const QString& key : envelopeKeys)
        {
            if (object.value(key).isArray())
            {
                entries = object.value(key).toArray();
                break;
            }
        }
        if (entries.isEmpty() && !object.contains(QStringLiteral("playlists")))
        {
            return std::unexpected(QStringLiteral("Playlist response has no playlists array"));
        }
    } else
    {
        return std::unexpected(QStringLiteral("Playlist response is not an object"));
    }

    QList<Summary> summaries;
    summaries.reserve(entries.size());
    for (const QJsonValue& entry : entries)
    {
        if (!entry.isObject())
        {
            continue;
        }
        Summary summary = parseSummary(entry.toObject());
        if (!summary.id.isEmpty())
        {
            summaries.append(std::move(summary));
        }
    }
    return summaries;
}

std::expected<SunoPlaylistService::Page, QString>
SunoPlaylistService::parsePage(const QJsonObject& root)
{
    // The playlist object is the body -- NOT `{playlist: {...}}`. The wrapper is
    // still accepted because the bundle described it that way, and refusing it
    // would make a shape the capture contradicts look like a broken playlist.
    const QJsonObject playlist =
            root.value(QStringLiteral("playlist")).isObject()
                    ? root.value(QStringLiteral("playlist")).toObject()
                    : root;

    Page page;
    page.playlist = parseSummary(playlist);
    page.currentPage =
            static_cast<int>(json::integerValue(playlist.value(QStringLiteral("current_page"))).value_or(0));
    page.totalResults = page.playlist.numTotalResults;

    const QJsonValue clipsValue = playlist.value(QStringLiteral("playlist_clips"));
    if (clipsValue.isArray())
    {
        const QJsonArray clipEntries = clipsValue.toArray();
        page.clips.reserve(clipEntries.size());
        for (const QJsonValue& entry : clipEntries)
        {
            if (!entry.isObject())
            {
                continue;
            }
            const QJsonObject wrap = entry.toObject();
            // `{clip, relative_index, created_at}`; a hydrated feed (`/unified/feed`)
            // puts the clip fields at the top of the entry instead, and both are
            // handled by trying the wrapper first.
            const QJsonObject clipObject = wrap.value(QStringLiteral("clip")).isObject()
                                                   ? wrap.value(QStringLiteral("clip")).toObject()
                                                   : wrap;
            if (auto clip = ClipParser::parseClip(clipObject); clip && !clip->id.empty())
            {
                page.clips.append(std::move(*clip));
            }
        }
    }

    if (page.playlist.id.isEmpty() && page.clips.isEmpty() && page.currentPage == 0)
    {
        return std::unexpected(QStringLiteral("Playlist response is not a playlist"));
    }
    return page;
}

void SunoPlaylistService::refresh()
{
    if (loading_)
    {
        return;
    }
    playlists_.clear();
    emit playlistsReset();
    setLoading(true);

    if (!client_)
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    credentialRefreshPending_ = true;
    client_->reloadStoredCredentials();
}

void SunoPlaylistService::openPlaylist(const QString& playlistId, int page)
{
    if (playlistId.isEmpty() || loading_)
    {
        return;
    }
    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    current_ = {};
    emit playlistReset();
    setLoading(true);
    enqueuePage(playlistId, page < 1 ? 1 : page, false);
}

void SunoPlaylistService::loadMore()
{
    if (loading_ || current_.playlist.id.isEmpty() || !hasMore())
    {
        return;
    }
    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Not authenticated"));
        return;
    }
    setLoading(true);
    enqueuePage(current_.playlist.id, nextPage(), true);
}

void SunoPlaylistService::enqueueList()
{
    const quint64 requestId = ++activeRequest_;
    QPointer<SunoPlaylistService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::PLAYLIST_ME), "GET", {},
            [guard, requestId](QNetworkReply* reply) {
                if (!guard)
                {
                    reply->deleteLater();
                    return;
                }
                guard->handleListReply(reply, requestId);
            });
}

void SunoPlaylistService::enqueuePage(const QString& playlistId, int page, bool append)
{
    const quint64 requestId = ++activeRequest_;
    QPointer<SunoPlaylistService> guard(this);
    // `{}` is this tree's placeholder spelling (see ALIGNED_LYRICS and
    // SunoAudioUploadService), not Qt's %1 -- arg() would leave it literal.
    const QString path = QString(qstr(endpoints::PLAYLIST_BY_ID))
                                 .replace(QStringLiteral("{}"), playlistId)
            + QStringLiteral("/?page=") + QString::number(page);
    client_->enqueueAuthenticatedRequest(
            path, "GET", {},
            [guard, append, requestId](QNetworkReply* reply) {
                if (!guard)
                {
                    reply->deleteLater();
                    return;
                }
                guard->handlePageReply(reply, append, requestId);
            });
}

void SunoPlaylistService::handleListReply(QNetworkReply* reply, quint64 requestId)
{
    if (requestId != activeRequest_)
    {
        reply->deleteLater();
        return;
    }

    const QNetworkReply::NetworkError networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkErrorText = reply->errorString();

    if (!client_)
    {
        reply->deleteLater();
        activeRequest_ = 0;
        fail(QStringLiteral("Playlists have no SunoClient to read the response with"));
        return;
    }
    // Read the body before judging the transport error, for the reason spelled
    // out in SunoExploreService::handleReply: the bounded reader turns an
    // oversized body into OperationCanceledError and its own refusal message is
    // the only useful one.
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
        fail(networkErrorText.isEmpty() ? QStringLiteral("Playlist request failed")
                                        : networkErrorText);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        fail(QStringLiteral("Playlists returned invalid JSON"));
        return;
    }

    auto parsed = parseList(document.isArray() ? QJsonValue(document.array())
                                               : QJsonValue(document.object()));
    if (!parsed)
    {
        fail(parsed.error());
        return;
    }
    playlists_ = std::move(*parsed);
    emit playlistsReady();
    setLoading(false);
}

void SunoPlaylistService::handlePageReply(QNetworkReply* reply, bool append, quint64 requestId)
{
    if (requestId != activeRequest_)
    {
        reply->deleteLater();
        return;
    }

    const QNetworkReply::NetworkError networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkErrorText = reply->errorString();

    if (!client_)
    {
        reply->deleteLater();
        activeRequest_ = 0;
        fail(QStringLiteral("Playlist has no SunoClient to read the response with"));
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
        fail(networkErrorText.isEmpty() ? QStringLiteral("Playlist request failed")
                                        : networkErrorText);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        fail(QStringLiteral("Playlist returned invalid JSON"));
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
        current_ = std::move(*parsed);
        emit playlistReset();
    } else
    {
        current_.clips.append(std::move(parsed->clips));
        current_.currentPage = parsed->currentPage;
        if (parsed->totalResults > 0)
        {
            current_.totalResults = parsed->totalResults;
        }
        if (current_.playlist.id.isEmpty())
        {
            current_.playlist = parsed->playlist;
        }
        emit playlistPageReady();
    }
    setLoading(false);
}

void SunoPlaylistService::onCredentialsRestored()
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
    enqueueList();
}

void SunoPlaylistService::setLoading(bool loading)
{
    if (loading_ == loading)
    {
        return;
    }
    loading_ = loading;
    emit loadingChanged();
}

void SunoPlaylistService::fail(const QString& message)
{
    credentialRefreshPending_ = false;
    activeRequest_ = 0;
    setLoading(false);
    emit failed(message);
}

} // namespace vc::suno
