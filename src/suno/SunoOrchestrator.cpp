#include "SunoOrchestrator.hpp"
#include "SunoClient.hpp"
#include "core/Logger.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>

namespace vc {

SunoOrchestrator::SunoOrchestrator(vc::suno::SunoClient* client, QObject* parent)
    : QObject(parent), client_(client) {}

void SunoOrchestrator::sendMessage(const QString& message, const QString& workspaceId) {
    if (!client_) return;
    // ... logic ...
}

void SunoOrchestrator::fetchHistory() {
    // ... logic ...
}

void SunoOrchestrator::onMessageFinished(QNetworkReply* reply) {
    reply->deleteLater();
}

void SunoOrchestrator::onHistoryFinished(QNetworkReply* reply) {
    reply->deleteLater();
}

} // namespace vc
