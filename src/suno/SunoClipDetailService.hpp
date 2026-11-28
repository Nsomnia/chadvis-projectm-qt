#pragma once

#include "SunoModels.hpp"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The song page's supplemental reads: what a clip is derived from, what it
/// actually is, how many remixes it has, what people said, and what sounds
/// like it.
///
/// Five independent GETs, deliberately not one request and never
/// all-or-nothing. All five are captured `[T1]` page-inspection routes whose
/// bodies were catalogued rather than reproduced key-for-key
/// (ENDPOINT-INVENTORY §4.6, §5.16), so every parser is tolerant and a section
/// that comes back unreadable stays EMPTY instead of failing the sheet: the base
/// clip is already on screen, and losing "based on" must not cost the user the
/// thing they opened.
///
/// Read-only and unmetered. `attribution` is the ancestry: `source_clips[]` says
/// which clips this one came from, and `relationship` distinguishes a cover
/// (`COV`) from an extension (`EX`). `roots` is the other direction of the same
/// question: `clips[]` plus `clip_attribution_type`, where an ORIGINAL song is
/// an empty list with `"original"` rather than a failed read.
class SunoClipDetailService final : public QObject
{
    Q_OBJECT

public:
    struct Source {
        SunoClip clip;
        /// `COV` / `EX` on the captured objects, passed through verbatim: a
        /// mapped vocabulary would silently drop a third spelling.
        QString relationship;
    };

    /// `GET /clips/clip_roots` — what the clip IS, not what it came from.
    ///
    /// Captured body (`[T1]` 2026-11-26): `{clips: [...], clip_attribution_type:
    /// "original"}`. `clips` empty together with `"original"` is the common,
    /// valid answer for an original song; `attributionType` stays empty when the
    /// server did not say, which is NOT the same as "original".
    ///
    /// The call was captured on the app's base and is issued on this client's
    /// (`suno.com/a/api`), so a non-2xx settles the section empty rather than
    /// retrying: a base variant is not a dead route and not a user-facing error.
    struct Roots {
        QList<SunoClip> clips;
        QString attributionType;
    };

    struct Comment {
        QString author;
        QString text;
        int likeCount{0};
    };

    struct Detail {
        QString clipId;
        QList<Source> sources;
        Roots roots;
        QList<Comment> comments;
        int totalComments{0};
        bool allowComments{true};
        /// -1 means "no answer yet", which is not the same as a real zero.
        int remixCount{-1};
        QList<SunoClip> similar;
    };

    explicit SunoClipDetailService(SunoClient* client, QObject* parent = nullptr);

    static std::expected<QList<Source>, QString> parseAttribution(const QJsonObject& root);
    static std::expected<Roots, QString> parseRoots(const QJsonObject& root);
    static std::expected<QList<Comment>, QString> parseComments(const QJsonObject& root);
    static int parseRemixCount(const QJsonObject& root);
    static std::expected<QList<SunoClip>, QString> parseSimilar(const QJsonObject& root);

    /// Start (or restart) the five reads. A second call for a different id
    /// abandons the first: a sheet shows one clip at a time.
    void load(const QString& clipId);

    [[nodiscard]] const Detail& detail() const { return detail_; }
    [[nodiscard]] bool isLoading() const { return pending_ > 0; }

signals:
    /// One section settled. Emitted per reply, so a fast section appears without
    /// waiting for the slowest of the five.
    void detailChanged();
    void failed(const QString& message);

private:
    enum class Route { Attribution, Roots, Comments, RemixCount, Similar };

    void enqueue(Route route, const QString& path);
    void handleReply(QNetworkReply* reply, Route route, const QString& clipId);
    void settle();
    void fail(const QString& message);

    SunoClient* client_;
    Detail detail_;
    int pending_{0};
    quint64 generation_{0};
};

} // namespace vc::suno
