#pragma once
// SunoAccountManager.hpp - session catalog + billing fetches.
//
// Owns the GET /api/session/ and GET /api/billing/info/ calls, routing them
// through SunoClient's authenticated queue. Parses via ClipParser's tolerant
// accessors into the captured-schema value structs and caches the last good
// values for synchronous read by the QML bridge.

#include "SunoClient.hpp"
#include "SunoModels.hpp"

#include <QObject>
#include <optional>

class QNetworkReply;

namespace vc::suno {

class SunoAccountManager : public QObject {
    Q_OBJECT

public:
    explicit SunoAccountManager(SunoClient* client, QObject* parent = nullptr);
    ~SunoAccountManager() override;

    /// Fetch session catalog + billing info (used after auth turns ActiveValid).
    void refreshAll();
    /// Fetch only billing (cheap post-generation refresh).
    void refreshBilling();

    // Last-known values; empty/nullopt until the first successful reply.
    [[nodiscard]] const std::optional<SunoUserSummary>& user() const { return user_; }
    [[nodiscard]] const QList<SunoModelInfo>& models() const { return models_; }
    [[nodiscard]] const std::optional<SunoBillingInfo>& billing() const { return billing_; }

signals:
    void accountInfoReady();
    void billingInfoReady();
    void accountError(const QString& message);

private:
    void handleSessionReply(QNetworkReply* reply);
    void handleBillingReply(QNetworkReply* reply);

    SunoClient* client_;
    std::optional<SunoUserSummary> user_;
    QList<SunoModelInfo> models_;
    std::optional<SunoBillingInfo> billing_;
};

} // namespace vc::suno
