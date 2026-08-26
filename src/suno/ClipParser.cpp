#include "ClipParser.hpp"

#include "core/Logger.hpp"

#include <QJsonValue>

namespace vc::suno {

// ─────────────────────────────────────────────────────────────
// Tolerant accessors
// ─────────────────────────────────────────────────────────────

QString ClipParser::optString(const QJsonObject& obj, const QString& key,
                              const QString& def) {
    const QJsonValue v = obj.value(key);
    return v.isString() ? v.toString() : def;
}

qint64 ClipParser::optInt(const QJsonObject& obj, const QString& key, qint64 def) {
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) {
        return static_cast<qint64>(v.toDouble());
    }
    // Some fields arrive as numeric strings; accept them rather than dropping.
    if (v.isString()) {
        bool ok = false;
        const qint64 parsed = v.toString().toLongLong(&ok);
        if (ok) return parsed;
    }
    return def;
}

bool ClipParser::optBool(const QJsonObject& obj, const QString& key, bool def) {
    const QJsonValue v = obj.value(key);
    if (v.isBool()) {
        return v.toBool();
    }
    // Captured filters use "True"/"False" strings; be liberal in what we read.
    if (v.isString()) {
        const QString s = v.toString();
        if (s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) return true;
        if (s.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) return false;
    }
    return def;
}

double ClipParser::optDouble(const QJsonObject& obj, const QString& key, double def) {
    const QJsonValue v = obj.value(key);
    if (v.isDouble()) {
        return v.toDouble();
    }
    if (v.isString()) {
        bool ok = false;
        const double parsed = v.toString().toDouble(&ok);
        if (ok) return parsed;
    }
    return def;
}

// ─────────────────────────────────────────────────────────────
// Clip parsing
// ─────────────────────────────────────────────────────────────

namespace {

void parseMediaUrls(const QJsonObject& obj, SunoClip& clip) {
    const QJsonValue v = obj.value(QStringLiteral("media_urls"));
    if (!v.isArray()) {
        return;
    }
    for (const auto& entry : v.toArray()) {
        const QJsonObject m = entry.toObject();
        if (m.isEmpty()) {
            continue;
        }
        SunoMediaUrl media;
        media.url = ClipParser::optString(m, "url").toStdString();
        media.content_type = ClipParser::optString(m, "content_type").toStdString();
        media.delivery = ClipParser::optString(m, "delivery").toStdString();
        media.encoding = ClipParser::optString(m, "encoding").toStdString();
        if (!media.url.empty()) {
            clip.media_urls.push_back(std::move(media));
        }
    }
}

void parseMetadata(const QJsonObject& obj, SunoClip& clip) {
    const QJsonValue metaValue = obj.value(QStringLiteral("metadata"));
    if (!metaValue.isObject()) {
        return; // metadata is optional/tolerant per contract
    }
    const QJsonObject meta = metaValue.toObject();
    auto& md = clip.metadata;
    md.prompt = ClipParser::optString(meta, "prompt").toStdString();
    md.tags = ClipParser::optString(meta, "tags").toStdString();
    md.lyrics = ClipParser::optString(meta, "lyrics").toStdString();
    md.negative_tags = ClipParser::optString(meta, "negative_tags").toStdString();
    md.error_message = ClipParser::optString(meta, "error_message").toStdString();
    md.duration = ClipParser::optString(meta, "duration").toStdString();
    md.type = ClipParser::optString(meta, "type").toStdString();
    md.bpm = ClipParser::optString(meta, "bpm").toStdString();
    md.key = ClipParser::optString(meta, "key").toStdString();
    md.model_id = ClipParser::optString(meta, "model_id").toStdString();
    md.refund_credits = ClipParser::optBool(meta, "refund_credits", false);
    md.stream = ClipParser::optBool(meta, "stream", false);
    md.make_instrumental = ClipParser::optBool(meta, "make_instrumental", false);
    md.weirdness = ClipParser::optDouble(meta, "weirdness", 0.0);
    md.style_weight = ClipParser::optDouble(meta, "style_weight", 0.0);
}

} // namespace

std::expected<SunoClip, QString> ClipParser::parseClip(const QJsonObject& obj) {
    if (obj.isEmpty()) {
        return std::unexpected(QStringLiteral("clip object is empty"));
    }

    SunoClip clip;
    clip.id = optString(obj, "id").toStdString();
    if (clip.id.empty()) {
        return std::unexpected(QStringLiteral("clip object has no id"));
    }

    clip.title = optString(obj, "title").toStdString();
    clip.video_url = optString(obj, "video_url").toStdString();
    clip.audio_url = optString(obj, "audio_url").toStdString();
    clip.image_url = optString(obj, "image_url").toStdString();
    clip.image_large_url = optString(obj, "image_large_url").toStdString();
    clip.major_model_version = optString(obj, "major_model_version").toStdString();
    clip.model_name = optString(obj, "model_name").toStdString();
    clip.display_name = optString(obj, "display_name").toStdString();
    clip.handle = optString(obj, "handle").toStdString();
    clip.user_id = optString(obj, "user_id").toStdString();
    clip.entity_type = optString(obj, "entity_type").toStdString();
    clip.status = optString(obj, "status").toStdString();
    clip.created_at = optString(obj, "created_at").toStdString();

    clip.play_count = optInt(obj, "play_count");
    clip.upvote_count = optInt(obj, "upvote_count");
    clip.batch_index = optInt(obj, "batch_index");
    clip.allow_comments = optBool(obj, "allow_comments", true);
    clip.is_verified = optBool(obj, "is_verified", false);
    clip.has_hook = optBool(obj, "has_hook", false);
    clip.is_persona_root = optBool(obj, "is_persona_root", false);
    clip.is_liked = optBool(obj, "is_liked", false);
    clip.is_trashed = optBool(obj, "is_trashed", false);
    clip.is_public = optBool(obj, "is_public", false);

    parseMetadata(obj, clip);
    parseMediaUrls(obj, clip);
    return clip;
}

std::expected<QList<SunoClip>, QString>
ClipParser::parseClipArray(const QJsonArray& arr) {
    QList<SunoClip> clips;
    clips.reserve(arr.size());

    int skipped = 0;
    for (const auto& item : arr) {
        if (!item.isObject()) {
            ++skipped;
            continue;
        }
        auto parsed = parseClip(item.toObject());
        if (parsed) {
            clips.push_back(std::move(*parsed));
        } else {
            ++skipped;
            LOG_DEBUG("ClipParser: skipped malformed clip: {}",
                      parsed.error().toStdString());
        }
    }
    if (skipped > 0) {
        LOG_INFO("ClipParser: parsed {} clips, skipped {} malformed entries",
                 static_cast<i64>(clips.size()), skipped);
    }
    return clips;
}

std::expected<ClipParser::FeedPage, QString>
ClipParser::parseFeedEnvelope(const QJsonObject& root) {
    FeedPage page;

    const QJsonValue clipsValue = root.value(QStringLiteral("clips"));
    if (!clipsValue.isArray()) {
        return std::unexpected(QStringLiteral("feed envelope has no clips array"));
    }

    auto clips = parseClipArray(clipsValue.toArray());
    if (!clips) {
        return std::unexpected(clips.error());
    }
    page.clips = std::move(*clips);

    // next_cursor is ABSENT when the feed is exhausted; an explicit null or
    // empty string means the same thing.
    const QJsonValue cursorValue = root.value(QStringLiteral("next_cursor"));
    if (cursorValue.isString()) {
        page.nextCursor = cursorValue.toString();
    }

    // Trust the explicit flag when present; fall back to cursor presence.
    const QJsonValue moreValue = root.value(QStringLiteral("has_more"));
    page.hasMore = moreValue.isBool() ? moreValue.toBool() : !page.nextCursor.isEmpty();

    return page;
}

} // namespace vc::suno
