#include "SunoOrchestrator.hpp"
#include "SunoClient.hpp"
#include "core/Logger.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkAccessManager>

namespace vc {

SunoOrchestrator::SunoOrchestrator(vc::suno::SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

void SunoOrchestrator::sendMessage(const QString& message, const QString& workspaceId) {
    if (!client_) return;

    QNetworkRequest request(QUrl(modalBaseUrl_ + "/api/v1/orchestrator/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // Add auth headers from SunoClient
    request.setRawHeader("Authorization", "Bearer " + client_->token().toUtf8());

    QJsonObject body;
    body["message"] = message;
    if (!workspaceId.isEmpty()) {
        body["workspace_id"] = workspaceId;
    }

    QNetworkReply* reply = client_->networkManager()->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply, workspaceId]() {
        onMessageFinished(reply);
    });
}

void SunoOrchestrator::fetchHistory() {
    if (!client_) return;

    QNetworkRequest request(QUrl(modalBaseUrl_ + "/api/v1/orchestrator/history"));
    request.setRawHeader("Authorization", "Bearer " + client_->token().toUtf8());

    QNetworkReply* reply = client_->networkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHistoryFinished(reply);
    });
}

void SunoOrchestrator::onMessageFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        LOG_ERROR("SunoOrchestrator: Chat error: {}", reply->errorString().toStdString());
        emit errorOccurred(reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();
    QString response = obj["response"].toString();
    QString workspaceId = obj["workspace_id"].toString();

    emit messageReceived(response, workspaceId);
}

void SunoOrchestrator::onHistoryFinished(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        LOG_ERROR("SunoOrchestrator: History error: {}", reply->errorString().toStdString());
        emit errorOccurred(reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QVariantList sessions = doc.array().toVariantList();
    emit historyFetched(sessions);
}

} // namespace vc
