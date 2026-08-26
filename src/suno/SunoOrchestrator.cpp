#include "SunoOrchestrator.hpp"
#include "SunoClient.hpp"
#include "SunoEndpoints.hpp"
#include "core/Logger.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace vc {

SunoOrchestrator::SunoOrchestrator(vc::suno::SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

void SunoOrchestrator::sendMessage(const QString& message, const QString& workspaceId) {
    if (!client_) return;

    QJsonObject body;
    body["message"] = message;
    if (!workspaceId.isEmpty()) {
        body["workspace_id"] = workspaceId;
    }

    // Routed through SunoClient so the request respects the shared
    // rate-limiting queue and auth-refresh gate (previously bypassed it).
    client_->enqueueAuthenticatedRequest(
        vc::suno::qstr(vc::suno::endpoints::MODAL_BASE) + vc::suno::qstr(vc::suno::endpoints::ORCHESTRATOR_CHAT),
        "POST",
        QJsonDocument(body).toJson(),
        [this, workspaceId](QNetworkReply* reply) {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                LOG_ERROR("SunoOrchestrator: Chat error: {}", reply->errorString().toStdString());
                emit errorOccurred(reply->errorString());
                return;
            }
            onMessageFinished(reply->readAll(), workspaceId);
        });
}

void SunoOrchestrator::fetchHistory() {
    if (!client_) return;

    client_->enqueueAuthenticatedRequest(
        vc::suno::qstr(vc::suno::endpoints::MODAL_BASE) + vc::suno::qstr(vc::suno::endpoints::ORCHESTRATOR_HISTORY),
        "GET",
        {},
        [this](QNetworkReply* reply) {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                LOG_ERROR("SunoOrchestrator: History error: {}", reply->errorString().toStdString());
                emit errorOccurred(reply->errorString());
                return;
            }
            onHistoryFinished(reply->readAll());
        });
}

void SunoOrchestrator::onMessageFinished(const QByteArray& body, const QString& workspaceId) {
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject obj = doc.object();
    QString response = obj["response"].toString();
    // Prefer the server-reported workspace id when present.
    QString resolvedWorkspaceId = obj["workspace_id"].toString();
    if (resolvedWorkspaceId.isEmpty()) resolvedWorkspaceId = workspaceId;

    emit messageReceived(response, resolvedWorkspaceId);
}

void SunoOrchestrator::onHistoryFinished(const QByteArray& body) {
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QVariantList sessions = doc.array().toVariantList();
    emit historyFetched(sessions);
}

} // namespace vc
