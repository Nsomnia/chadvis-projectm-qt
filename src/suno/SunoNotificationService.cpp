#include "SunoNotificationService.hpp"

#include <QTimer>

#include "SunoClient.hpp"
#include "ClipParser.hpp"

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

QString firstString(std::initializer_list<QString> values) {
    for (const QString& value : values) {
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QJsonObject firstProfile(const QJsonObject& object) {
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

/// v3's human-readable sentence is an array of rich-text segments:
/// `text[] = {text, bold, action?}`. The client renders one caption, so the
/// segments are concatenated in order. Whitespace is NOT inserted: the server
/// sends the spaces inside the segments, and inventing separators would double
/// them ("Ada  followed you"). Returns empty when the field is absent or not an
/// array, which is what v2 does.
QString notificationText(const QJsonValue& value) {
    if (!value.isArray()) {
        return {};
    }
    QString text;
    for (const QJsonValue& segment : value.toArray()) {
        if (segment.isObject()) {
            text += json::stringValue(segment.toObject().value(QStringLiteral("text")));
        }
    }
    return text;
}

std::optional<SunoNotificationService::Notification> parseNotification(const QJsonValue& value) {
    if (!value.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonObject authorObject = firstProfile(object);
    const QJsonValue contentValue = object.value(QStringLiteral("content"));
    const QJsonObject contentObject =
            contentValue.isObject() ? contentValue.toObject() : QJsonObject{};

    SunoNotificationService::Notification notification;
    notification.id = json::stringValue(object.value(QStringLiteral("id")));
    notification.type = firstString({
            json::stringValue(object.value(QStringLiteral("notification_type"))),
            json::stringValue(object.value(QStringLiteral("type"))),
    });
    notification.createdAt = firstString({
            json::stringValue(object.value(QStringLiteral("updated_at"))),
            json::stringValue(object.value(QStringLiteral("created_at"))),
            json::stringValue(object.value(QStringLiteral("notified_at"))),
    });
    notification.read = json::booleanValue(object.value(QStringLiteral("is_read")),
                                     json::booleanValue(object.value(QStringLiteral("read"))));

    notification.author.userId = firstString({
            json::stringValue(authorObject.value(QStringLiteral("user_id"))),
            json::stringValue(authorObject.value(QStringLiteral("id"))),
    });
    notification.author.displayName = firstString({
            json::stringValue(authorObject.value(QStringLiteral("display_name"))),
            json::stringValue(authorObject.value(QStringLiteral("name"))),
            json::stringValue(authorObject.value(QStringLiteral("username"))),
    });
    notification.author.handle = firstString({
            json::stringValue(authorObject.value(QStringLiteral("handle"))),
            json::stringValue(authorObject.value(QStringLiteral("username"))),
    });

    notification.contentType = firstString({
            json::stringValue(object.value(QStringLiteral("content_type"))),
            json::stringValue(contentObject.value(QStringLiteral("content_type"))),
            json::stringValue(contentObject.value(QStringLiteral("type"))),
            json::stringValue(object.value(QStringLiteral("entity_type"))),
    });
    notification.contentId = firstString({
            json::stringValue(object.value(QStringLiteral("content_id"))),
            json::stringValue(contentObject.value(QStringLiteral("content_id"))),
            json::stringValue(contentObject.value(QStringLiteral("id"))),
    });
    notification.contentAncillaryId = firstString({
            json::stringValue(object.value(QStringLiteral("content_ancillary_id"))),
            json::stringValue(contentObject.value(QStringLiteral("content_ancillary_id"))),
            json::stringValue(contentObject.value(QStringLiteral("ancillary_id"))),
    });
    notification.contentTitle = firstString({
            json::stringValue(object.value(QStringLiteral("content_title"))),
            json::stringValue(contentObject.value(QStringLiteral("content_title"))),
            json::stringValue(contentObject.value(QStringLiteral("title"))),
            json::stringValue(contentObject.value(QStringLiteral("name"))),
    });
    notification.contentMessage = firstString({
            json::stringValue(object.value(QStringLiteral("content_message"))),
            json::stringValue(object.value(QStringLiteral("caption"))),
            notificationText(object.value(QStringLiteral("text"))),
            json::stringValue(contentObject.value(QStringLiteral("content_message"))),
            json::stringValue(contentObject.value(QStringLiteral("message"))),
            json::stringValue(contentObject.value(QStringLiteral("caption"))),
    });
    notification.caption = firstString({
            json::stringValue(object.value(QStringLiteral("caption"))),
            notificationText(object.value(QStringLiteral("text"))),
            notification.contentMessage,
            contentValue.isString() ? contentValue.toString() : QString{},
            json::stringValue(contentObject.value(QStringLiteral("description"))),
    });
    notification.priority =
            static_cast<int>(json::integerValue(object.value(QStringLiteral("priority"))).value_or(0));
    notification.totalUsers =
            static_cast<int>(json::integerValue(object.value(QStringLiteral("total_users"))).value_or(0));
    return notification;
}

} // namespace

SunoNotificationService::SunoNotificationService(SunoClient* client, QObject* parent,
                                                 int pollIntervalMs)
    : QObject(parent), client_(client) {
    if (pollIntervalMs > 0) {
        pollTimer_ = new QTimer(this);
        pollTimer_->setInterval(pollIntervalMs);
        // Silent when signed out or already busy: refresh() would otherwise
        // report "Not authenticated" every interval, which is a timer talking,
        // not a user action, and the sign-out path already reports once.
        connect(pollTimer_, &QTimer::timeout, this, [this]() {
            if (!client_ || !client_->isAuthenticated() || loading_ || markLoading_) {
                return;
            }
            refresh();
        });
        pollTimer_->start();
    }
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
SunoNotificationService::parseResponse(const QJsonObject& root) {
    const QJsonValue notificationsValue = root.value(QStringLiteral("notifications"));
    if (!notificationsValue.isArray()) {
        return std::unexpected(QStringLiteral("Notification response has no notifications array"));
    }

    Response response;
    response.notifiedAt = json::stringValue(root.value(QStringLiteral("notified_at")));
    const QJsonArray values = notificationsValue.toArray();
    response.notifications.reserve(values.size());
    for (const QJsonValue& value : values) {
        if (auto notification = parseNotification(value)) {
            response.notifications.push_back(std::move(*notification));
        }
    }
    return response;
}

std::expected<qint64, QString> SunoNotificationService::parseBadgeCount(const QJsonObject& root) {
    const auto count = json::integerValue(root.value(QStringLiteral("badge_count")));
    if (!count || *count < 0) {
        return std::unexpected(
                QStringLiteral("Notification badge response has no valid badge_count"));
    }
    return *count;
}

QJsonObject SunoNotificationService::markAllReadBody(const QDateTime& before) {
    QJsonObject body;
    body[QStringLiteral("all")] = true;
    body[QStringLiteral("before_datetime_utc")] =
            before.isValid() ? before.toUTC().toString(Qt::ISODateWithMs) : QString{};
    return body;
}

void SunoNotificationService::refresh() {
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
    refreshLoading_ = true;
    setLoading(true);
    enqueueRefresh();
}

void SunoNotificationService::enqueueRefresh() {
    const quint64 generation = refreshGeneration_;
    QPointer<SunoNotificationService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::NOTIFICATION_V3) + QStringLiteral("?include_hooks=true"), "GET", {},
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleRefreshReply(reply, ReplyKind::List, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            });
    client_->enqueueAuthenticatedRequest(qstr(endpoints::NOTIFICATION_BADGE_COUNT), "GET", {},
                                         [guard, generation](QNetworkReply* reply) {
                                             if (guard) {
                                                 guard->handleRefreshReply(reply, ReplyKind::Badge,
                                                                           generation);
                                             } else if (reply) {
                                                 reply->deleteLater();
                                             }
                                         });
}

void SunoNotificationService::handleRefreshReply(QNetworkReply* reply, ReplyKind kind,
                                                 quint64 generation) {
    if (!reply) {
        return;
    }
    if (generation != refreshGeneration_) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = requestError(reply, QStringLiteral("Notification request failed"));

    // Body first, transport error second, and the order is load-bearing. SunoClient
    // arms every reply it issues with a readyRead-driven BodyReader, so a body past
    // the studio-API cap was already aborted mid-transfer: reply->error() is
    // OperationCanceledError and errorString() says only "Operation canceled". The
    // sticky refusal names the limit, and it only survives if we ask for the body
    // before discarding it.
    //
    // readTrackedBody() is also the only path that still has the bytes -- this
    // reply has been pumped, so readAll() would hand back an empty QByteArray that
    // is indistinguishable from a server that sent nothing, and would surface as
    // "Notification response returned invalid JSON".
    if (!client_) {
        reply->deleteLater();
        failRefresh(QStringLiteral("Notifications has no SunoClient to read the response with"));
        return;
    }
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();

    if (!body) {
        failRefresh(body.error());
        return;
    }
    if (!error.isEmpty()) {
        failRefresh(error);
        return;
    }
    if (status < 200 || status >= 300) {
        failRefresh(QStringLiteral("Notification request failed"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(*body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failRefresh(QStringLiteral("Notification response returned invalid JSON"));
        return;
    }

// Each half is applied the moment it arrives. The old shape paired them and
    // required BOTH to succeed, so a 404 on the decorative badge counter threw
    // away a perfectly good notification list (T0067) -- and the badge is the
    // fragile one: it is a counter, not a document.
    if (kind == ReplyKind::List) {
        auto parsed = parseResponse(document.object());
        if (!parsed) {
            failRefresh(parsed.error());
            return;
        }
        if (*body != lastListBody_) {
            lastListBody_ = *body;
            notifications_ = std::move(parsed->notifications);
            emit notificationsChanged();
        }
    } else {
        auto parsed = parseBadgeCount(document.object());
        if (!parsed) {
            failRefresh(parsed.error());
            return;
        }
        const int nextUnreadCount = static_cast<int>(*parsed);
        if (unreadCount_ != nextUnreadCount) {
            unreadCount_ = nextUnreadCount;
            emit unreadCountChanged();
        }
    }

    if (refreshPending_ > 0) {
        --refreshPending_;
    }
    if (refreshPending_ == 0) {
        refreshLoading_ = false;
        setLoading(markLoading_);
    }
}

void SunoNotificationService::markAllRead() {
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

void SunoNotificationService::enqueueMarkAllRead(const QDateTime& before) {
    const quint64 generation = markGeneration_;
    const QByteArray payload =
            QJsonDocument(markAllReadBody(before)).toJson(QJsonDocument::Compact);
    QPointer<SunoNotificationService> guard(this);
    client_->enqueueAuthenticatedRequest(
            qstr(endpoints::NOTIFICATION_V2_READ), "POST", payload,
            [guard, generation](QNetworkReply* reply) {
                if (guard) {
                    guard->handleMarkReadReply(reply, generation);
                } else if (reply) {
                    reply->deleteLater();
                }
            },
            false);
}

void SunoNotificationService::handleMarkReadReply(QNetworkReply* reply, quint64 generation) {
    if (!reply) {
        return;
    }
    if (generation != markGeneration_) {
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = requestError(reply, QStringLiteral("Mark all read failed"));

    // Same ordering and same reason as handleRefreshReply: the body carries the
    // breach diagnosis, the transport error only says the reply was cancelled.
    if (!client_) {
        reply->deleteLater();
        failMarkRead(QStringLiteral("Notifications has no SunoClient to read the response with"));
        return;
    }
    auto body = client_->readTrackedBody(reply);
    reply->deleteLater();

    if (!body) {
        failMarkRead(body.error());
        return;
    }
    if (!error.isEmpty() || status < 200 || status >= 300) {
        failMarkRead(error.isEmpty() ? QStringLiteral("Mark all read failed") : error);
        return;
    }

    // A 2xx is accepted without parsing: 204 No Content is a legitimate
    // success for this mutation, and the shape of any 2xx body is unverified
    // per the endpoint inventory. Requiring a JSON object here reported the
    // server's accepted mark-all-read back to the user as a failure and
    // left every notification unread. SunoLibraryMutations::handleReply
    // treats a 2xx the same way — parse nothing.
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

QString SunoNotificationService::requestError(QNetworkReply* reply, const QString& fallback) const {
    if (reply->error() == QNetworkReply::NoError) {
        return {};
    }
    const QString text = reply->errorString();
    return text.isEmpty() ? fallback : text;
}

void SunoNotificationService::setLoading(bool loading) {
    if (loading_ == loading) {
        return;
    }
    loading_ = loading;
    emit loadingChanged();
}

void SunoNotificationService::setError(const QString& message) {
    if (error_ == message) {
        return;
    }
    error_ = message;
    emit errorChanged(error_);
}

void SunoNotificationService::failRefresh(const QString& message) {
    // One half failing must not cancel the other. This used to bump the
    // generation, which invalidated the sibling reply still in flight, and then
    // clear both pending slots -- so a badge 404 discarded the list that had
    // already arrived, and a list failure did the same to a good count (T0067).
    // Loading ends when the outstanding replies are done, not when one errors.
    if (refreshPending_ > 0) {
        --refreshPending_;
    }
    if (refreshPending_ == 0) {
        refreshLoading_ = false;
        setLoading(markLoading_);
    }
    setError(message);
}

void SunoNotificationService::failMarkRead(const QString& message) {
    ++markGeneration_;
    markLoading_ = false;
    setLoading(refreshLoading_);
    setError(message);
}

} // namespace vc::suno
