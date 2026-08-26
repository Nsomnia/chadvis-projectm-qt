#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#include "SunoModels.hpp"
#include "SunoEndpoints.hpp"

namespace vc::suno {
class SunoClient;
}

namespace vc {

/**
 * @brief Suno B-Side Orchestrator Client
 *
 * All HTTP traffic is routed through vc::suno::SunoClient so orchestrator
 * requests respect the shared rate-limiting queue and auth-refresh gate.
 */
class SunoOrchestrator : public QObject {
    Q_OBJECT

public:
    explicit SunoOrchestrator(vc::suno::SunoClient* client, QObject* parent = nullptr);
    ~SunoOrchestrator() override = default;

    void sendMessage(const QString& message, const QString& workspaceId = QString());
    void fetchHistory();

signals:
    void messageReceived(const QString& response, const QString& workspaceId);
    void historyFetched(const QVariantList& sessions);
    void errorOccurred(const QString& error);

private:
    void onMessageFinished(const QByteArray& body, const QString& workspaceId);
    void onHistoryFinished(const QByteArray& body);

    vc::suno::SunoClient* client_;
};

} // namespace vc
