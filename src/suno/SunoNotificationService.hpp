#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <expected>
#include <optional>

class QNetworkReply;

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

    explicit SunoNotificationService(SunoClient* client, QObject* parent = nullptr);

    static std::expected<Response, QString> parseResponse(const QJsonObject& root);
    static std::expected<qint64, QString> parseBadgeCount(const QJsonObject& root);
    static QJsonObject markAllReadBody(const QDateTime& before);

    void refresh();
    void markAllRead();

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

private:
    enum class ReplyKind {
        List,
        Badge,
    };

    void enqueueRefresh();
    void enqueueMarkAllRead(const QDateTime& before);
    void handleRefreshReply(QNetworkReply* reply, ReplyKind kind, quint64 generation);
    void handleMarkReadReply(QNetworkReply* reply, quint64 generation);
    void setLoading(bool loading);
    void setError(const QString& message);
    void failRefresh(const QString& message);
    void failMarkRead(const QString& message);
    QString requestError(QNetworkReply* reply, const QString& fallback) const;

    SunoClient* client_{nullptr};
    QList<Notification> notifications_;
    int unreadCount_{0};
    QString error_;
    quint64 refreshGeneration_{0};
    quint64 markGeneration_{0};
    int refreshPending_{0};
    Response pendingResponse_;
    std::optional<qint64> pendingBadge_;
    bool refreshLoading_{false};
    bool markLoading_{false};
    bool loading_{false};
};

}
