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
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        LOG_WARN("SunoOrchestrator: chat reply is not a JSON object ({} bytes)",
                 static_cast<i64>(body.size()));
        emit errorOccurred(QStringLiteral("Malformed chat reply from orchestrator"));
        return;
    }

    // Tolerant read: both fields are optional on the wire.
    const QJsonObject obj = doc.object();
    const QString response = obj.value(QStringLiteral("response")).toString();
    // Prefer the server-reported workspace id when present.
    QString resolvedWorkspaceId =
            obj.value(QStringLiteral("workspace_id")).toString();
    if (resolvedWorkspaceId.isEmpty()) resolvedWorkspaceId = workspaceId;

    emit messageReceived(response, resolvedWorkspaceId);
}

void SunoOrchestrator::onHistoryFinished(const QByteArray& body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) {
        // Some deployments wrap the array in an object; tolerate {"sessions":[...]}.
        const QJsonValue sessions = doc.object().value(QStringLiteral("sessions"));
        if (sessions.isArray()) {
            emit historyFetched(sessions.toArray().toVariantList());
            return;
        }
        LOG_WARN("SunoOrchestrator: history reply is not a JSON array");
        emit historyFetched({});
        return;
    }
    emit historyFetched(doc.array().toVariantList());
}

} // namespace vc
