#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <expected>
#include <optional>

class QNetworkReply;
class QTimer;

namespace vc::suno {

class SunoClient;

class SunoNotificationService final : public QObject
{
    Q_OBJECT

public:
    struct Author {
        QString userId;
        QString displayName;
        QString handle;
        // v3 carries the picture as `avatars[0].image_url` instead of a
        // `user_profiles` entry; empty when absent (v2 has no avatar source).
        QString avatarImageUrl;
    };

    struct Notification {
        QString id;
        QString type;
        QString caption;
        QString createdAt;
        bool read{false};
        Author author;
        QString contentType;
        QString contentId;
        QString contentAncillaryId;
        QString contentTitle;
        QString contentMessage;
        int priority{0};
        int totalUsers{0};
    };

    struct Response {
        QList<Notification> notifications;
        QString notifiedAt;
    };

    /// The caller-visible verbs. `Refresh` re-reads the list (v3) and the badge
    /// counter (v2) in one call; the other two are POST mutations that settle
    /// `Accepted` on a 2xx and parse nothing.
    enum class NotificationVerb {
        Refresh,
        MarkAllRead,
        ClearBadge,
    };

    /// What happened. Same vocabulary and semantics as
    /// `MutationOutcome` in `SunoLibraryMutations.hpp`, without depending on
    /// that header: Accepted is a 2xx with no field of the body asserted,
    /// Refused never sent a request, Rejected is a server non-2xx, Failed is
    /// a sent request with no readable verdict.
    enum class NotificationOutcome {
        Accepted,
        Refused,
        Rejected,
        Failed,
    };

    /// One settled verb. `subjectId` is empty for every verb this service has:
    /// mark-all-read, clear-badge and refresh are account-scoped, not
    /// per-notification, and the field is kept for shape parity. `httpStatus`
    /// is 0 for Refused and for a transport failure with no HTTP answer, and
    /// the observed status otherwise. `reason` is empty only for a clean
    /// Accepted and otherwise the same sentence `errorChanged` carries.
    struct NotificationResult {
        NotificationVerb verb{NotificationVerb::Refresh};
        QString subjectId;
        NotificationOutcome outcome{NotificationOutcome::Refused};
        int httpStatus{0};
        QString reason;
    };

    /// `pollIntervalMs` re-reads the surface on a timer so the unread badge
    /// moves without the Notifications view being opened. 0 disables polling;
    /// the default exists because nothing else in the app asks for this data.
    explicit SunoNotificationService(SunoClient* client, QObject* parent = nullptr,
                                     int pollIntervalMs = 60000);

    static std::expected<Response, QString> parseResponse(const QJsonObject& root);
    static std::expected<qint64, QString> parseBadgeCount(const QJsonObject& root);
    static QJsonObject markAllReadBody(const QDateTime& before);

    /// The status-only verdict: 2xx Accepted, 4xx Rejected, anything else
    /// Failed (0 is no HTTP answer, 5xx is the origin failing). A 204 with no
    /// body is a success; nothing else about the reply participates.
    [[nodiscard]] static NotificationOutcome outcomeForStatus(int httpStatus) noexcept;

    void refresh();
    void markAllRead();
    void clearBadge();

    [[nodiscard]] const QList<Notification>& notifications() const { return notifications_; }
    [[nodiscard]] int unreadCount() const { return unreadCount_; }
    [[nodiscard]] bool isLoading() const { return loading_; }
    [[nodiscard]] QString error() const { return error_; }

signals:
    void notificationsChanged();
    void unreadCountChanged();
    void loadingChanged();
    void errorChanged(const QString& message);
    void markAllReadCompleted();
    void badgeCleared();
    /// One verb reached a terminal state. `verb` names the verb, `outcome`
    /// the verdict, and `reason` the same sentence `errorChanged` carries,
    /// so a caller can act on the outcome without listening to the error.
    void notificationSettled(const NotificationResult& result);

private:
    enum class ReplyKind {
        List,
        Badge,
    };

    void enqueueRefresh();
    void enqueueMarkAllRead(const QDateTime& before);
    void enqueueClearBadge();
    void handleRefreshReply(QNetworkReply* reply, ReplyKind kind, quint64 generation);
    void handleMarkReadReply(QNetworkReply* reply, quint64 generation);
    void handleClearBadgeReply(QNetworkReply* reply, quint64 generation);
    void setLoading(bool loading);
    void setError(const QString& message);
    void failRefresh(const QString& message, NotificationOutcome outcome, int httpStatus);
    void failMarkRead(const QString& message, NotificationOutcome outcome, int httpStatus);
    void failClearBadge(const QString& message, NotificationOutcome outcome, int httpStatus);
    void settle(NotificationVerb verb, NotificationOutcome outcome, int httpStatus,
                const QString& reason);
    QString requestError(QNetworkReply* reply, const QString& fallback) const;

    SunoClient* client_{nullptr};
    QList<Notification> notifications_;
    int unreadCount_{0};
    QString error_;
    quint64 refreshGeneration_{0};
    quint64 markGeneration_{0};
    quint64 clearGeneration_{0};
    int refreshPending_{0};
    /// The last list body that was applied, so a poll that returns the same page
    /// does not re-emit notificationsChanged and rebuild the list under the
    /// user's scroll position.
    QByteArray lastListBody_;
    /// Refresh settles once when both halves are done: the last failure wins
    /// for the aggregate `reason`, mirroring `error()` which also ends on the
    /// last `setError`. `refreshHasFailure_` is false only while both halves
    /// succeeded.
    bool refreshHasFailure_{false};
    NotificationOutcome refreshFailureOutcome_{NotificationOutcome::Accepted};
    int refreshFailureStatus_{0};
    QString refreshFailureReason_;
    QTimer* pollTimer_{nullptr};
    bool refreshLoading_{false};
    bool markLoading_{false};
    bool clearLoading_{false};
    bool loading_{false};
};

[[nodiscard]] constexpr const char* toString(
        SunoNotificationService::NotificationVerb verb) noexcept
{
    switch (verb) {
        case SunoNotificationService::NotificationVerb::Refresh:
            return "refresh";
        case SunoNotificationService::NotificationVerb::MarkAllRead:
            return "mark-all-read";
        case SunoNotificationService::NotificationVerb::ClearBadge:
            return "clear-badge";
    }
    return "unknown";
}

[[nodiscard]] constexpr const char* toString(
        SunoNotificationService::NotificationOutcome outcome) noexcept
{
    switch (outcome) {
        case SunoNotificationService::NotificationOutcome::Accepted:
            return "accepted";
        case SunoNotificationService::NotificationOutcome::Refused:
            return "refused";
        case SunoNotificationService::NotificationOutcome::Rejected:
            return "rejected";
        case SunoNotificationService::NotificationOutcome::Failed:
            return "failed";
    }
    return "unknown";
}

}

Q_DECLARE_METATYPE(vc::suno::SunoNotificationService::NotificationResult)
