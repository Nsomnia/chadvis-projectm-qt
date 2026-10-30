#include "SunoClipDetailService.hpp"

#include "ClipParser.hpp"
#include "SunoClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

namespace vc::suno {

namespace {

/// A clip inside one of these envelopes, unwrapped the way the feed does it:
/// `{clip: {...}}`, `{item: {...}}`, `{clip_schema: {...}}`, or the clip itself.
std::optional<SunoClip> unwrapClip(const QJsonObject& object)
{
    static const QStringList keys{QStringLiteral("clip"), QStringLiteral("item"),
                                  QStringLiteral("clip_schema")};
    for (const QString& key : keys) {
        if (object.value(key).isObject()) {
            auto clip = ClipParser::parseClip(object.value(key).toObject());
            if (clip && !clip->id.empty()) {
                return std::move(*clip);
            }
        }
    }
    auto clip = ClipParser::parseClip(object);
    if (clip && !clip->id.empty()) {
        return std::move(*clip);
    }
    return std::nullopt;
}

} // namespace

SunoClipDetailService::SunoClipDetailService(SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

std::expected<QList<SunoClipDetailService::Source>, QString>
SunoClipDetailService::parseAttribution(const QJsonObject& root)
{
    const QJsonValue sources = root.value(QStringLiteral("source_clips"));
    if (!sources.isArray()) {
        // Absence is not an error worth showing: it means "nothing is known to
        // be upstream", which is the common case for an original song.
        return QList<Source>{};
    }
    QList<Source> out;
    const QJsonArray entries = sources.toArray();
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        auto clip = unwrapClip(entry.toObject());
        if (!clip) {
            continue;
        }
        Source source;
        source.relationship =
                json::stringValue(entry.toObject().value(QStringLiteral("relationship")));
        source.clip = std::move(*clip);
        out.append(std::move(source));
    }
    return out;
}

std::expected<QList<SunoClipDetailService::Comment>, QString>
SunoClipDetailService::parseComments(const QJsonObject& root)
{
    // `results` is the captured key; `comments` is accepted because the endpoint
    // is named that and a rename would otherwise read as "no comments".
    QJsonArray entries;
    for (const QString& key : {QStringLiteral("results"), QStringLiteral("comments")}) {
        if (root.value(key).isArray()) {
            entries = root.value(key).toArray();
            break;
        }
    }
    QList<Comment> out;
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject object = entry.toObject();
        Comment comment;
        comment.text = json::stringValue(object.value(QStringLiteral("text")));
        if (comment.text.isEmpty()) {
            comment.text = json::stringValue(object.value(QStringLiteral("content")));
        }
        // The author is nested on every captured shape (`user.display_name`,
        // `user_profile.handle`), so the flat spellings are only a fallback.
        const QJsonValue user = object.value(QStringLiteral("user"));
        const QJsonObject userObject = user.isObject() ? user.toObject() : QJsonObject{};
        comment.author = json::stringValue(userObject.value(QStringLiteral("display_name")));
        if (comment.author.isEmpty()) {
            comment.author = json::stringValue(userObject.value(QStringLiteral("handle")));
        }
        if (comment.author.isEmpty()) {
            comment.author = json::stringValue(object.value(QStringLiteral("user_name")));
        }
        comment.likeCount = static_cast<int>(
                json::integerValue(object.value(QStringLiteral("like_count"))).value_or(0));
        if (!comment.text.isEmpty()) {
            out.append(std::move(comment));
        }
    }
    return out;
}

int SunoClipDetailService::parseRemixCount(const QJsonObject& root)
{
    for (const QString& key : {QStringLiteral("count"), QStringLiteral("remix_count"),
                               QStringLiteral("num_total_results")}) {
        if (root.contains(key)) {
            return static_cast<int>(json::integerValue(root.value(key)).value_or(0));
        }
    }
    // An array body (`{clips:[...]}`) answers the same question by length.
    if (root.value(QStringLiteral("clips")).isArray()) {
        return root.value(QStringLiteral("clips")).toArray().size();
    }
    return 0;
}

std::expected<QList<SunoClip>, QString> SunoClipDetailService::parseSimilar(const QJsonObject& root)
{
    QJsonArray entries;
    for (const QString& key : {QStringLiteral("clips"), QStringLiteral("similar"),
                               QStringLiteral("results")}) {
        if (root.value(key).isArray()) {
            entries = root.value(key).toArray();
            break;
        }
    }
    QList<SunoClip> out;
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        if (auto clip = unwrapClip(entry.toObject())) {
            out.append(std::move(*clip));
        }
    }
    return out;
}

void SunoClipDetailService::load(const QString& clipId)
{
    // No clip id is "nothing to ask about", not an auth failure: the sheet
    // closes as often as it opens, and a notice about signing in would be a
    // sentence about the wrong thing.
    if (clipId.isEmpty()) {
        return;
    }
    if (!client_ || !client_->isAuthenticated()) {
        fail(QStringLiteral("Sign in to Suno first."));
        return;
    }
    detail_ = {};
    detail_.clipId = clipId;
    pending_ = 4;
    ++generation_;
    emit detailChanged();

    const QString id = QString::fromStdString(clipId.toStdString());
    enqueue(Route::Attribution,
            QString(qstr(endpoints::CLIP_ATTRIBUTION)).replace(QStringLiteral("{}"), id));
    enqueue(Route::Comments,
            QString(qstr(endpoints::GEN_COMMENTS)).replace(QStringLiteral("{}"), id)
                    + QStringLiteral("?order=most_liked"));
    enqueue(Route::RemixCount, qstr(endpoints::CLIP_REMIXES_COUNT)
                                      + QStringLiteral("?clip_id=")
                                      + QUrl::toPercentEncoding(id));
    enqueue(Route::Similar, qstr(endpoints::CLIP_GET_SIMILAR) + QStringLiteral("?id=")
                                    + QUrl::toPercentEncoding(id));
}

void SunoClipDetailService::enqueue(Route route, const QString& path)
{
    const quint64 generation = generation_;
    QPointer<SunoClipDetailService> guard(this);
    client_->enqueueAuthenticatedRequest(
            path, "GET", {}, [guard, route, generation](QNetworkReply* reply) {
                if (!guard) {
                    reply->deleteLater();
                    return;
                }
                guard->handleReply(reply, route, QString::number(generation));
            });
}

void SunoClipDetailService::handleReply(QNetworkReply* reply, Route route,
                                        const QString& clipId)
{
    // A reply from an abandoned load is dropped, not merged: the sheet shows one
    // clip and a late answer about the previous one would attach its ancestry to
    // the wrong song.
    if (clipId != QString::number(generation_) || !client_) {
        reply->deleteLater();
        return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();

    // Every section degrades on its own: a 404 on comments must not blank the
    // ancestry that already arrived.
    if (!body || status < 200 || status >= 300) {
        settle();
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        settle();
        return;
    }
    const QJsonObject root = document.object();
    switch (route) {
    case Route::Attribution:
        if (auto parsed = parseAttribution(root)) {
            detail_.sources = std::move(*parsed);
        }
        break;
    case Route::Comments:
        if (auto parsed = parseComments(root)) {
            detail_.comments = std::move(*parsed);
        }
        detail_.totalComments = static_cast<int>(
                json::integerValue(root.value(QStringLiteral("total_count")))
                        .value_or(detail_.comments.size()));
        detail_.allowComments = json::booleanValue(
                root.value(QStringLiteral("allow_comment")), true);
        break;
    case Route::RemixCount:
        detail_.remixCount = parseRemixCount(root);
        break;
    case Route::Similar:
        if (auto parsed = parseSimilar(root)) {
            detail_.similar = std::move(*parsed);
        }
        break;
    }
    emit detailChanged();
    settle();
}

void SunoClipDetailService::settle()
{
    if (pending_ > 0) {
        --pending_;
    }
}

void SunoClipDetailService::fail(const QString& message)
{
    pending_ = 0;
    emit failed(message);
}

} // namespace vc::suno
