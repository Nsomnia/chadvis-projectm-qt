#include "SunoGenerationService.hpp"

#include "ClipParser.hpp"
#include "SunoClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QUrl>

namespace vc::suno {

namespace {

constexpr int kPollIntervalMs = 4000;
// 90 polls at 4 s is six minutes. The one measured description-mode submit on
// the record completed in ~73-80 s; this is that with a wide margin, and the
// bound exists so a batch that never completes ends as a timeout instead of a
// spinner nobody can cancel.
constexpr int kMaxPolls = 90;

} // namespace

SunoGenerationService::SunoGenerationService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client)
{
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(kPollIntervalMs);
    connect(pollTimer_, &QTimer::timeout, this, [this] {
        if (pollsRemaining_ <= 0)
        {
            stopPolling();
            fail(QStringLiteral("The song is still rendering. Open suno.com to watch it."));
            return;
        }
        --pollsRemaining_;
        enqueuePoll();
    });
}

SunoGenerationService::Preflight SunoGenerationService::parsePreflight(const QJsonObject& root)
{
    Preflight preflight;
    // "Absent means no challenge" is the site's own default: `/c/check` is
    // called before every submit and the ordinary answer carries nothing to do.
    const QJsonValue required = root.value(QStringLiteral("required"));
    preflight.required = json::booleanValue(required, false);
    preflight.captchaVersion =
            static_cast<int>(json::integerValue(root.value(QStringLiteral("captcha_version")))
                                     .value_or(0));
    return preflight;
}

QJsonObject SunoGenerationService::buildSubmitBody(const Request& request,
                                                   const QString& transactionId)
{
    QJsonObject metadata;
    metadata[QStringLiteral("web_client_pathname")] = QStringLiteral("/create");
    metadata[QStringLiteral("is_max_mode")] = false;
    metadata[QStringLiteral("is_mumble")] = false;
    metadata[QStringLiteral("create_mode")] = QStringLiteral("custom");
    metadata[QStringLiteral("disable_volume_normalization")] = false;
    if (!transactionId.isEmpty())
    {
        metadata[QStringLiteral("create_session_token")] = transactionId;
    }

    QJsonObject body;
    body[QStringLiteral("task")] = QStringLiteral("generate");
    body[QStringLiteral("generation_type")] = QStringLiteral("TEXT");
    body[QStringLiteral("prompt")] = request.prompt;
    body[QStringLiteral("tags")] = request.tags;
    body[QStringLiteral("title")] = request.title;
    body[QStringLiteral("negative_tags")] = request.negativeTags;
    body[QStringLiteral("make_instrumental")] = request.instrumental;
    body[QStringLiteral("metadata")] = metadata;
    body[QStringLiteral("transaction_uuid")] =
            transactionId.isEmpty() ? QString() : transactionId;
    if (!request.model.isEmpty())
    {
        body[QStringLiteral("mv")] = request.model;
    }
    // Only when a challenge was solved. `token_provider` without a token would be
    // a claim about a challenge that was never run.
    if (!request.captchaToken.isEmpty())
    {
        body[QStringLiteral("token")] = request.captchaToken;
        body[QStringLiteral("token_provider")] = request.captchaVersion == 2 ? 2 : 1;
    }
    return body;
}

std::vector<std::string>
SunoGenerationService::parseSubmittedClipIds(const QJsonObject& root)
{
    std::vector<std::string> ids;
    const QJsonValue clips = root.value(QStringLiteral("clips"));
    if (clips.isArray())
    {
        const QJsonArray entries = clips.toArray();
        ids.reserve(static_cast<std::size_t>(entries.size()));
        for (const QJsonValue& entry : entries)
        {
            if (!entry.isObject())
            {
                continue;
            }
            const QString id = json::stringValue(entry.toObject().value(QStringLiteral("id")));
            if (!id.isEmpty())
            {
                ids.push_back(id.toStdString());
            }
        }
    }
    if (ids.empty())
    {
        const QString batchId = json::stringValue(root.value(QStringLiteral("id")));
        if (!batchId.isEmpty())
        {
            ids.push_back(batchId.toStdString());
        }
    }
    return ids;
}

void SunoGenerationService::checkCaptcha()
{
    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    error_.clear();
    preflightDone_ = false;
    setState(State::CheckingCaptcha);
    enqueuePreflight();
}

void SunoGenerationService::submit(const Request& request)
{
    if (!preflightDone_)
    {
        // Refused, not guessed: `required` decides whether the server will accept
        // a body without a token, and guessing costs the user a failed submit.
        fail(QStringLiteral("The generation preflight has not run yet."));
        return;
    }
    if (preflight_.required && request.captchaToken.isEmpty())
    {
        setState(State::NeedsCaptcha);
        emit captchaRequired(preflight_.captchaVersion);
        return;
    }
    if (!client_ || !client_->isAuthenticated())
    {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    error_.clear();
    clipIds_.clear();
    clips_.clear();
    setState(State::Submitting);
    enqueueSubmit(request);
}

void SunoGenerationService::enqueuePreflight()
{
    const quint64 id = ++requestId_;
    QPointer<SunoGenerationService> guard(this);
    const QByteArray payload = R"({"ctype":"generation"})";
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::CAPTCHA_CHECK), "POST", payload,
            [guard, id](QNetworkReply* reply) {
                if (!guard)
                {
                    reply->deleteLater();
                    return;
                }
                guard->handlePreflightReply(reply, id);
            });
}

void SunoGenerationService::enqueueSubmit(const Request& request)
{
    const quint64 id = ++requestId_;
    QPointer<SunoGenerationService> guard(this);
    const QString transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray payload = QJsonDocument(buildSubmitBody(request, transactionId))
                                       .toJson(QJsonDocument::Compact);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::GENERATE), "POST", payload,
            [guard, request, id](QNetworkReply* reply) {
                if (!guard)
                {
                    reply->deleteLater();
                    return;
                }
                guard->handleSubmitReply(reply, request, id);
            });
}

void SunoGenerationService::enqueuePoll()
{
    if (clipIds_.empty())
    {
        return;
    }
    const quint64 id = ++requestId_;
    QPointer<SunoGenerationService> guard(this);
    // Repeated `ids`, not a csv: the site's own read-back uses the repeated form
    // and a comma-joined value would be one unknown id to a server that splits
    // on `&`.
    QString path = qstr(endpoints::CLIP_GET_SONGS_BY_IDS);
    for (const std::string& clipId : clipIds_)
    {
        path += (path.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?"))
                + QStringLiteral("ids=") + QUrl::toPercentEncoding(QString::fromStdString(clipId));
    }
    client_->enqueueAuthenticatedRequest(path, "GET", {}, [guard, id](QNetworkReply* reply) {
        if (!guard)
        {
            reply->deleteLater();
            return;
        }
        guard->handlePollReply(reply, id);
    });
}

void SunoGenerationService::handlePreflightReply(QNetworkReply* reply, quint64 id)
{
    if (id != requestId_ || !client_)
    {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();
    if (!body)
    {
        fail(body.error());
        return;
    }
    if (status < 200 || status >= 300)
    {
        fail(QStringLiteral("Suno refused the generation preflight (%1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    preflight_ = parseError.error == QJsonParseError::NoError && document.isObject()
                         ? parsePreflight(document.object())
                         : Preflight{};
    preflightDone_ = true;
    setState(preflight_.required ? State::NeedsCaptcha : State::Idle);
    emit preflightReady();
    if (preflight_.required)
    {
        emit captchaRequired(preflight_.captchaVersion);
    }
}

void SunoGenerationService::handleSubmitReply(QNetworkReply* reply, const Request& request,
                                              quint64 id)
{
    Q_UNUSED(request);
    if (id != requestId_ || !client_)
    {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();
    if (!body)
    {
        fail(body.error());
        return;
    }
    if (status < 200 || status >= 300)
    {
        // The one refusal that must keep its own name: the challenge failed, so
        // the user is told to solve it rather than to "try again".
        const QString text = QString::fromUtf8(*body);
        if (text.contains(QStringLiteral("token_validation_failed")))
        {
            setState(State::NeedsCaptcha);
            emit captchaRequired(preflight_.captchaVersion);
            fail(QStringLiteral("Suno asked for a fresh captcha. Open suno.com/create to solve it."));
            return;
        }
        fail(QStringLiteral("Suno refused the generation (%1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        fail(QStringLiteral("Suno returned an unreadable generation response."));
        return;
    }
    clipIds_ = parseSubmittedClipIds(document.object());
    if (clipIds_.empty())
    {
        fail(QStringLiteral("Suno accepted the request but named no clip."));
        return;
    }
    setState(State::Polling);
    pollsRemaining_ = kMaxPolls;
    emit clipsSubmitted();
    pollTimer_->start();
}

void SunoGenerationService::handlePollReply(QNetworkReply* reply, quint64 id)
{
    if (id != requestId_ || !client_)
    {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();
    if (!body)
    {
        stopPolling();
        fail(body.error());
        return;
    }
    if (status < 200 || status >= 300)
    {
        stopPolling();
        fail(QStringLiteral("Suno refused the completion read (%1).").arg(status));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        stopPolling();
        fail(QStringLiteral("Suno returned an unreadable completion response."));
        return;
    }

    const QJsonObject root = document.object();
    const QJsonValue clipsValue = root.value(QStringLiteral("clips"));
    const QJsonArray entries = clipsValue.isArray() ? clipsValue.toArray() : QJsonArray{};
    QList<SunoClip> parsedClips;
    parsedClips.reserve(entries.size());
    bool allComplete = !entries.isEmpty();
    for (const QJsonValue& entry : entries)
    {
        if (!entry.isObject())
        {
            continue;
        }
        auto clip = ClipParser::parseClip(entry.toObject());
        if (!clip)
        {
            allComplete = false;
            continue;
        }
        if (clip->status != "complete")
        {
            allComplete = false;
        }
        if (clip->status == "error" || clip->status == "failed")
        {
            stopPolling();
            fail(QStringLiteral("Suno could not render this song."));
            return;
        }
        parsedClips.append(std::move(*clip));
    }
    if (!parsedClips.isEmpty())
    {
        clips_ = std::move(parsedClips);
        emit clipsUpdated();
    }
    if (allComplete)
    {
        stopPolling();
        setState(State::Completed);
        emit completed();
    }
}

void SunoGenerationService::stopPolling()
{
    if (pollTimer_)
    {
        pollTimer_->stop();
    }
}

void SunoGenerationService::setState(State state)
{
    if (state_ == state)
    {
        return;
    }
    state_ = state;
    emit stateChanged();
}

void SunoGenerationService::fail(const QString& message)
{
    stopPolling();
    error_ = message;
    setState(State::Failed);
    emit failed(message);
}

} // namespace vc::suno
