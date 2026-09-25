#pragma once

#include "SunoModels.hpp"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <expected>
#include <optional>

class QNetworkReply;

namespace vc::suno {

class SunoClient;

class SunoExploreService final : public QObject
{
    Q_OBJECT

public:
    struct Feed {
        QString id;
        QString label;
        QList<SunoClip> clips;
    };

    struct Page {
        QList<Feed> feeds;
        QString nextCursor;

        [[nodiscard]] bool hasMore() const
        { return !nextCursor.isEmpty(); }
    };

    explicit SunoExploreService(SunoClient* client, QObject* parent = nullptr);

    static QJsonObject requestBody(std::optional<QString> cursor);
    static std::expected<Page, QString> parsePage(const QJsonObject& root);

    void refresh();
    void loadMore();

    [[nodiscard]] const QList<Feed>& feeds() const
    { return feeds_; }
    [[nodiscard]] bool isLoading() const
    { return loading_; }
    [[nodiscard]] bool hasMore() const
    { return !nextCursor_.isEmpty(); }

signals:
    void reset();
    void pageReady();
    void loadingChanged();
    void hasMoreChanged();
    void failed(const QString& message);

private:
    void enqueueRequest(std::optional<QString> cursor, bool append);
    void handleReply(QNetworkReply* reply, bool append, quint64 requestId);
    void handleAuthenticationLost();
    void setLoading(bool loading);
    void fail(const QString& message);

    SunoClient* client_;
    QList<Feed> feeds_;
    QString nextCursor_;
    quint64 activeRequest_ = 0;
    bool loading_{false};
};

}
