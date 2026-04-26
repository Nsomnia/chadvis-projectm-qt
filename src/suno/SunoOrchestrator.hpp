#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QNetworkReply>
#include <memory>
#include "SunoModels.hpp"
#include "SunoEndpoints.hpp"

namespace vc::suno {
class SunoClient;
}

namespace vc {

/**
 * @brief Suno B-Side Orchestrator Client
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

private slots:
    void onMessageFinished(QNetworkReply* reply);
    void onHistoryFinished(QNetworkReply* reply);

private:
    vc::suno::SunoClient* client_;
    QString modalBaseUrl_{QString::fromUtf8(vc::suno::endpoints::MODAL_BASE.data(), static_cast<int>(vc::suno::endpoints::MODAL_BASE.size()))};
};

} // namespace vc
