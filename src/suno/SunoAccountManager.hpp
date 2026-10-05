#pragma once
// SunoAccountManager.hpp - session catalog + billing fetches.
//
// Owns the GET /api/session/ and GET /api/billing/info/ calls, routing them
// through SunoClient's authenticated queue. Parses via ClipParser's tolerant
// accessors into the captured-schema value structs and caches the last good
// values for synchronous read by the QML bridge.

#include "FeatureFlags.hpp"
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
    void clearSnapshots();

    // Last-known values; empty/nullopt until the first successful reply.
    [[nodiscard]] const std::optional<SunoUserSummary>& user() const { return user_; }
    [[nodiscard]] const QList<SunoModelInfo>& models() const { return models_; }
    [[nodiscard]] const std::optional<SunoBillingInfo>& billing() const { return billing_; }

    /// The gate resolver, fed from the SAME `/api/session/` reply this manager
    /// already fetches.
    ///
    /// This object is the natural owner because it already calls `SESSION` on
    /// every authenticated refresh. That reply carried 47 server flags
    /// anonymously, 57 authenticated, and 58 on staging, plus `roles`,
    /// `statsig_custom_properties`, `experiments` and `configs.gen-endpoint`
    /// (the server-selected generate route) — and every one of them was parsed
    /// and discarded, because only `user` and `models` were read. So the gating
    /// plane was being fetched on every sign-in and thrown away, which is why
    /// `SunoBridge::generationAvailable()` could only ever be a hardcoded
    /// `return false`.
    ///
    /// Held here rather than in `SunoClient` because the flags describe the
    /// *account*, and this is the account component. A signed-out resolver holds
    /// an empty capability set, which reads every flag-gated surface as
    /// `ServerGated` — the same verdict an anonymous flag map produces, and the
    /// correct one.
    [[nodiscard]] const GateResolver& gates() const { return gates_; }
    [[nodiscard]] GateResolver& gates() { return gates_; }

signals:
    void accountInfoReady();
    void billingInfoReady();
    void accountError(const QString& message);
    /// The server flag map changed, so every gate verdict may have changed.
    /// Separate from `accountInfoReady` because the UI needs to re-ask *why* a
    /// surface is unavailable, not merely that the account refreshed.
    void gatesChanged();

private:
    void handleSessionReply(QNetworkReply* reply);
    void handleBillingReply(QNetworkReply* reply);

    SunoClient* client_;
    std::optional<SunoUserSummary> user_;
    QList<SunoModelInfo> models_;
    std::optional<SunoBillingInfo> billing_;
    GateResolver gates_;
};

} // namespace vc::suno
