#include "SunoNotificationService.hpp"

#include "SunoClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <initializer_list>

namespace vc::suno {

namespace {

QString scalarString(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return {};
}

std::optional<qint64> integerValue(const QJsonValue& value)
{
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        if (ok) {
            return parsed;
        }
    }
    return std::nullopt;
}

bool booleanValue(const QJsonValue& value, bool fallback = false)
{
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isString()) {
        const QString normalized = value.toString().trimmed().toLower();
        if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")) {
            return true;
        }
        if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")) {
            return false;
        }
    }
    if (value.isDouble()) {
        return value.toDouble() != 0.0;
    }
    return fallback;
}

QString firstString(std::initializer_list<QString> values)
{
    for (const QString& value : values) {
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QJsonObject firstProfile(const QJsonObject& object)
{
    const QJsonValue profiles = object.value(QStringLiteral("user_profiles"));
    if (profiles.isArray()) {
        const QJsonArray profileValues = profiles.toArray();
        for (const QJsonValue& profileValue : profileValues) {
            if (profileValue.isObject()) {
                return profileValue.toObject();
            }
        }
        return {};
    }
    if (profiles.isObject()) {
        return profiles.toObject();
    }
    const QJsonValue profile = object.value(QStringLiteral("user_profile"));
    return profile.isObject() ? profile.toObject() : QJsonObject{};
}

std::optional<SunoNotificationService::Notification> parseNotification(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonObject authorObject = firstProfile(object);
    const QJsonValue contentValue = object.value(QStringLiteral("content"));
    const QJsonObject contentObject = contentValue.isObject() ? contentValue.toObject() : QJsonObject{};

    SunoNotificationService::Notification notification;
    notification.id = scalarString(object.value(QStringLiteral("id")));
    notification.type = firstString({
        scalarString(object.value(QStringLiteral("notification_type"))),
        scalarString(object.value(QStringLiteral("type"))),
    });
    notification.createdAt = firstString({
        scalarString(object.value(QStringLiteral("updated_at"))),
        scalarString(object.value(QStringLiteral("created_at"))),
        scalarString(object.value(QStringLiteral("notified_at"))),
    });
    notification.read = booleanValue(object.value(QStringLiteral("is_read")),
                                     booleanValue(object.value(QStringLiteral("read"))));

    notification.author.userId = firstString({
        scalarString(authorObject.value(QStringLiteral("user_id"))),
        scalarString(authorObject.value(QStringLiteral("id"))),
    });
    notification.author.displayName = firstString({
        scalarString(authorObject.value(QStringLiteral("display_name"))),
        scalarString(authorObject.value(QStringLiteral("name"))),
        scalarString(authorObject.value(QStringLiteral("username"))),
    });
    notification.author.handle = firstString({
        scalarString(authorObject.value(QStringLiteral("handle"))),
        scalarString(authorObject.value(QStringLiteral("username"))),
    });

    notification.contentType = firstString({
        scalarString(object.value(QStringLiteral("content_type"))),
        scalarString(contentObject.value(QStringLiteral("content_type"))),
        scalarString(contentObject.value(QStringLiteral("type"))),
        scalarString(object.value(QStringLiteral("entity_type"))),
    });
    notification.contentId = firstString({
        scalarString(object.value(QStringLiteral("content_id"))),
        scalarString(contentObject.value(QStringLiteral("content_id"))),
        scalarString(contentObject.value(QStringLiteral("id"))),
    });
    notification.contentAncillaryId = firstString({
        scalarString(object.value(QStringLiteral("content_ancillary_id"))),
        scalarString(contentObject.value(QStringLiteral("content_ancillary_id"))),
        scalarString(contentObject.value(QStringLiteral("ancillary_id"))),
    });
    notification.contentTitle = firstString({
        scalarString(object.value(QStringLiteral("content_title"))),
        scalarString(contentObject.value(QStringLiteral("content_title"))),
        scalarString(contentObject.value(QStringLiteral("title"))),
        scalarString(contentObject.value(QStringLiteral("name"))),
    });
    notification.contentMessage = firstString({
        scalarString(object.value(QStringLiteral("content_message"))),
        scalarString(object.value(QStringLiteral("caption"))),
        scalarString(contentObject.value(QStringLiteral("content_message"))),
        scalarString(contentObject.value(QStringLiteral("message"))),
        scalarString(contentObject.value(QStringLiteral("caption"))),
    });
    notification.caption = firstString({
        scalarString(object.value(QStringLiteral("caption"))),
        notification.contentMessage,
        contentValue.isString() ? contentValue.toString() : QString{},
        scalarString(contentObject.value(QStringLiteral("description"))),
    });
    notification.priority = static_cast<int>(integerValue(object.value(QStringLiteral("priority"))).value_or(0));
    notification.totalUsers = static_cast<int>(integerValue(object.value(QStringLiteral("total_users"))).value_or(0));
    return notification;
}

}

SunoNotificationService::SunoNotificationService(SunoClient* client, QObject* parent)
    : QObject(parent)
    , client_(client)
{
    if (client_) {
        connect(client_, &SunoClient::authStateChanged, this, [this]() {
            if (client_ && client_->isAuthenticated()) {
                return;
            }
            if (refreshLoading_) {
                failRefresh(QStringLiteral("Not authenticated"));
            }
            if (markLoading_) {
                failMarkRead(QStringLiteral("Not authenticated"));
            }
        });
    }
}

std::expected<SunoNotificationService::Response, QString>
SunoNotificationService::parseResponse(const QJsonObject& root)
{
    const QJsonValue notificationsValue = root.value(QStringLiteral("notifications"));
    if (!notificationsValue.isArray()) {
        return std::unexpected(QStringLiteral("Notification response has no notifications array"));
    }

    Response response;
    response.notifiedAt = scalarString(root.value(QStringLiteral("notified_at")));
    const QJsonArray values = notificationsValue.toArray();
    response.notifications.reserve(values.size());
    for (const QJsonValue& value : values) {
        if (auto notification = parseNotification(value)) {
            response.notifications.push_back(std::move(*notification));
        }
    }
    return response;
}

std::expected<qint64, QString> SunoNotificationService::parseBadgeCount(const QJsonObject& root)
{
    const auto count = integerValue(root.value(QStringLiteral("badge_count")));
    if (!count || *count < 0) {
        return std::unexpected(QStringLiteral("Notification badge response has no valid badge_count"));
    }
    return *count;
}

QJsonObject SunoNotificationService::markAllReadBody(const QDateTime& before)
{
    QJsonObject body;
    body[QStringLiteral("all")] = true;
    body[QStringLiteral("before_datetime_utc")] = before.isValid()
                                                   ? before.toUTC().toString(Qt::ISODateWithMs)
                                                   : QString{};
    return body;
}

void SunoNotificationService::refresh()
{
    if (loading_) {
        return;
    }
    if (!client_ || !client_->isAuthenticated()) {
        failRefresh(QStringLiteral("Not authenticated"));
        return;
    }

    setError({});
    ++refreshGeneration_;
    refreshPending_ = 2;
    pendingResponse_ = {};
    pendingBadge_.reset();
    refreshLoading_ = true;
    setLoading(true);
    enqueueRefresh();
}

void SunoNotificationService::enqueueRefresh()
{
    const quint64 generation = refreshGeneration_;
    QPointer<SunoNotificationService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::NOTIFICATION_V2), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleRefreshReply(reply, ReplyKind::List, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            });
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::NOTIFICATION_BADGE_COUNT), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleRefreshReply(reply, ReplyKind::Badge, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            });
}

void SunoNotificationService::handleRefreshReply(QNetworkReply* reply, ReplyKind kind,
                                                  quint64 generation)
{
    if (!reply) {
        return;
    }
    if (generation != refreshGeneration_) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = requestError(reply, QStringLiteral("Notification request failed"));
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (!error.isEmpty()) {
        failRefresh(error);
        return;
    }
    if (status < 200 || status >= 300) {
        failRefresh(QStringLiteral("Notification request failed"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failRefresh(QStringLiteral("Notification response returned invalid JSON"));
        return;
    }

    if (kind == ReplyKind::List) {
        auto parsed = parseResponse(document.object());
        if (!parsed) {
            failRefresh(parsed.error());
            return;
        }
        pendingResponse_ = std::move(*parsed);
    } else {
        auto parsed = parseBadgeCount(document.object());
        if (!parsed) {
            failRefresh(parsed.error());
            return;
        }
        pendingBadge_ = *parsed;
    }

    --refreshPending_;
    if (refreshPending_ != 0 || !pendingBadge_) {
        return;
    }

    notifications_ = std::move(pendingResponse_.notifications);
    const int nextUnreadCount = *pendingBadge_;
    if (unreadCount_ != nextUnreadCount) {
        unreadCount_ = nextUnreadCount;
        emit unreadCountChanged();
    }
    emit notificationsChanged();
    refreshLoading_ = false;
    refreshPending_ = 0;
    setLoading(markLoading_);
}

void SunoNotificationService::markAllRead()
{
    if (loading_) {
        return;
    }
    if (!client_ || !client_->isAuthenticated()) {
        failMarkRead(QStringLiteral("Not authenticated"));
        return;
    }

    setError({});
    ++markGeneration_;
    markLoading_ = true;
    setLoading(true);
    enqueueMarkAllRead(QDateTime::currentDateTimeUtc());
}

void SunoNotificationService::enqueueMarkAllRead(const QDateTime& before)
{
    const quint64 generation = markGeneration_;
    const QByteArray payload = QJsonDocument(markAllReadBody(before)).toJson(QJsonDocument::Compact);
    QPointer<SunoNotificationService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::NOTIFICATION_V2_READ), "POST", payload,
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleMarkReadReply(reply, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            });
}

void SunoNotificationService::handleMarkReadReply(QNetworkReply* reply, quint64 generation)
{
    if (!reply) {
        return;
    }
    if (generation != markGeneration_) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = requestError(reply, QStringLiteral("Mark all read failed"));
    const QByteArray payload = reply->readAll();
    reply->deleteLater();
    if (!error.isEmpty() || status < 200 || status >= 300) {
        failMarkRead(error.isEmpty() ? QStringLiteral("Mark all read failed") : error);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failMarkRead(QStringLiteral("Mark all read returned invalid JSON"));
        return;
    }

    markLoading_ = false;
    setLoading(refreshLoading_);
    for (auto& notification : notifications_) {
        notification.read = true;
    }
    if (unreadCount_ != 0) {
        unreadCount_ = 0;
        emit unreadCountChanged();
    }
    emit notificationsChanged();
    emit markAllReadCompleted();
    refresh();
}

QString SunoNotificationService::requestError(QNetworkReply* reply,
                                              const QString& fallback) const
{
    if (reply->error() == QNetworkReply::NoError) {
        return {};
    }
    const QString text = reply->errorString();
    return text.isEmpty() ? fallback : text;
}

void SunoNotificationService::setLoading(bool loading)
{
    if (loading_ == loading) {
        return;
    }
    loading_ = loading;
    emit loadingChanged();
}

void SunoNotificationService::setError(const QString& message)
{
    if (error_ == message) {
        return;
    }
    error_ = message;
    emit errorChanged(error_);
}

void SunoNotificationService::failRefresh(const QString& message)
{
    ++refreshGeneration_;
    refreshPending_ = 0;
    refreshLoading_ = false;
    pendingResponse_ = {};
    pendingBadge_.reset();
    setLoading(markLoading_);
    setError(message);
}

void SunoNotificationService::failMarkRead(const QString& message)
{
    ++markGeneration_;
    markLoading_ = false;
    setLoading(refreshLoading_);
    setError(message);
}

}
