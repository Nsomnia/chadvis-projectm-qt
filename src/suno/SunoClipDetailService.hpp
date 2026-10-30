#pragma once

#include "SunoModels.hpp"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <expected>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

/// The song page's supplemental reads: what a clip is based on, how many remixes
/// it has, what people said about it, and what sounds like it.
///
/// Four independent GETs, deliberately not one request and deliberately not
/// all-or-nothing. They are captured `[T1]` page-inspection routes
/// (ENDPOINT-INVENTORY §4.6, §5.x) whose bodies were catalogued rather than
/// reproduced key-for-key, so every parser here is tolerant and every section
/// that comes back unreadable stays EMPTY instead of failing the sheet: the base
/// clip is already on screen, and losing "based on" must not cost the user the
/// thing they opened.
///
/// Read-only. Nothing in this class writes, and no route here is metered.
class SunoClipDetailService final : public QObject
{
    Q_OBJECT

public:
    struct Source {
        QString clipId;
        QString title;
        QString artist;
        /// `COV` (cover) / `EX` (extension) on the captured objects; passed
        /// through verbatim rather than mapped, because a third spelling would
        /// otherwise be silently lost.
        QString relationship;
    };

    struct Comment {
        QString id;
        QString author;
        QString text;
        QString createdAt;
        int likeCount{0};
    };

    struct Detail {
        QString clipId;
        QList<Source> sources;
        QList<Comment> comments;
        int totalComments{0};
        bool allowComments{true};
        /// -1 means "no answer yet" -- distinct from a real zero, which is a
        /// clip nobody has remixed.
        int remixCount{-1};
        QList<SunoClip> similar;
    };

    explicit SunoClipDetailService(SunoClient* client, QObject* parent = nullptr);

    static std::expected<QList<Source>, QString> parseAttribution(const QJsonObject& root);
    static std::expected<QList<Comment>, QString> parseComments(const QJsonObject& root);
    static int parseRemixCount(const QJsonObject& root);
    static std::expected<QList<SunoClip>, QString> parseSimilar(const QJsonObject& root);

    /// Start (or restart) the four reads for `clipId`. A second call for a
    /// different id abandons the first: the sheet shows one clip at a time.
    void load(const QString& clipId);

    [[nodiscard]] const Detail& detail() const { return detail_; }
    [[nodiscard]] bool isLoading() const { return pending_ > 0; }

signals:
    /// The open clip's detail changed -- emitted once per settled read, so a
    /// section appears as soon as its own reply lands rather than waiting for
    /// the slowest of the four.
    void detailChanged();
    void failed(const QString& message);

private:
    void enqueue(const QString& path, int route);
    void handleReply(QNetworkReply* reply, int route, const QString& clipId);
    void settle();

    SunoClient* client_;
    Detail detail_;
    int pending_{0};
    quint64 generation_{0};
};

} // namespace vc::suno
