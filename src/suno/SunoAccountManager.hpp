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

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <optional>

class QNetworkReply;

namespace vc::suno {

/// Terms-of-service acceptance state from
/// `GET /api/user/tos_acceptance` (ENDPOINT-INVENTORY §4.2, [T1] Burp
/// 2026-08-25). The capture establishes the two field names only; the
/// timestamp format is unpinned, so the raw server value is kept verbatim
/// and parsed tolerantly at the decision site instead of being coerced
/// into a `QDateTime` here.
struct TosAcceptance {
    bool hasAcceptedTos = false;
    QString hasAcceptedTosTimestamp;
};

/// Current onboarding state from
/// `GET /api/onboarding/current` (ENDPOINT-INVENTORY §4.2, [T1] Burp
/// 2026-08-25). No response field names/types are pinned by the capture
/// beyond the route itself, so the raw object is kept verbatim rather than
/// inventing typed fields. Named distinctly from TosAcceptance: this is
/// onboarding progress, not legal acceptance.
struct OnboardingState {
    QJsonObject raw;
};

class SunoAccountManager : public QObject {
    Q_OBJECT

public:
    explicit SunoAccountManager(SunoClient* client, QObject* parent = nullptr);
    ~SunoAccountManager() override;

    /// Fetch session catalog + billing info (used after auth turns ActiveValid).
    void refreshAll();
    /// Fetch only billing (cheap post-generation refresh).
    void refreshBilling();
    /// GET-only read of terms acceptance. Acceptance is a legal act the
    /// user performs in a browser; this client must never POST it, so no
    /// write path exists here. Unknown until the first successful reply;
    /// a failed read degrades back to unknown and never claims accepted.
    void refreshTosAcceptance();
    /// GET-only read of the current onboarding state. This client does not
    /// POST any onboarding write (start/back/skip/complete/submit); this
    /// slice wires the read only. Unknown until the first successful reply;
    /// a failed read degrades back to unknown and never claims a state.
    void refreshOnboardingCurrent();
    void clearSnapshots();

    /// The last successful TOS read, or `std::nullopt` when unknown (never
    /// fetched, signed out, or the last fetch failed).
    [[nodiscard]] const std::optional<TosAcceptance>& tosAcceptance() const
    {
        return tosAcceptance_;
    }
    [[nodiscard]] bool hasTosAcceptance() const { return tosAcceptance_.has_value(); }
    /// False when unknown, so a failed read cannot claim acceptance.
    [[nodiscard]] bool hasAcceptedTos() const
    {
        return tosAcceptance_.has_value() && tosAcceptance_->hasAcceptedTos;
    }
    /// Raw server timestamp, or empty when unknown.
    [[nodiscard]] QString hasAcceptedTosTimestamp() const
    {
        return tosAcceptance_.has_value() ? tosAcceptance_->hasAcceptedTosTimestamp : QString{};
    }
    /// Staleness predicate for an already-accepted account: false when not
    /// accepted (the initial prompt is a separate path), true when the
    /// timestamp precedes `2025-11-07T08:00:00Z`, and true on an
    /// unparseable timestamp (err toward asking).
    [[nodiscard]] bool needsTosReprompt() const
    {
        if (!tosAcceptance_.has_value()) {
            return false;
        }
        return needsTosReprompt(tosAcceptance_->hasAcceptedTos,
                                tosAcceptance_->hasAcceptedTosTimestamp);
    }
    /// The last successful onboarding read, or `std::nullopt` when unknown
    /// (never fetched, signed out, or the last fetch failed).
    [[nodiscard]] const std::optional<OnboardingState>& onboardingState() const
    {
        return onboardingState_;
    }
    [[nodiscard]] bool hasOnboardingState() const { return onboardingState_.has_value(); }
    /// Raw server object, or an empty QJsonObject when unknown.
    [[nodiscard]] QJsonObject onboardingRaw() const
    {
        return onboardingState_.has_value() ? onboardingState_->raw : QJsonObject{};
    }
    [[nodiscard]] static bool needsTosReprompt(bool hasAcceptedTos,
                                               const QString& timestamp);
    [[nodiscard]] static std::optional<QDateTime> parseTosTimestamp(const QString& timestamp);

    // Last-known values; empty/nullopt until the first successful reply.
    [[nodiscard]] const std::optional<SunoUserSummary>& user() const { return user_; }
    [[nodiscard]] const QList<SunoModelInfo>& models() const { return models_; }

    /// The model catalogue to actually show a user.
    ///
    /// Prefers `/api/billing/info/` over `/api/session/`, and the reason is not a
    /// preference. Measured against the captured bodies: `/api/session/`
    /// `models` is an **empty array** in every capture, while
    /// `/api/billing/info/` `models` carries the real catalogue with the same
    /// field shape the existing parser already reads — `name`, `external_key`,
    /// `major_version`, `description`, `can_use`, `is_default_model`,
    /// `capabilities`, `features`, `badges`, `max_lengths`. The session list is
    /// also keyed by client identity and its web branch was measured serving a
    /// **stale** set, whereas billing independently confirms the account's own
    /// model rights, so it is the one place the entitlement is authoritative.
    ///
    /// Falls back to the session list when billing yielded none, so a failed
    /// billing fetch cannot blank a catalogue that was otherwise working. Note
    /// the billing body also carries `accessible_features` — the 22-entry
    /// `PlanFeature` entitlement plane — which is not yet parsed.
    [[nodiscard]] const QList<SunoModelInfo>& preferredModels() const
    {
        return billingModels_.isEmpty() ? models_ : billingModels_;
    }
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
    void tosAcceptanceReady();
    void onboardingCurrentReady();

private:
    void handleSessionReply(QNetworkReply* reply);
    void handleBillingReply(QNetworkReply* reply);
    void handleTosReply(QNetworkReply* reply);
    void handleOnboardingReply(QNetworkReply* reply);

    SunoClient* client_;
    std::optional<SunoUserSummary> user_;
    QList<SunoModelInfo> models_;
    /// The catalogue from `/api/billing/info/`, which is the authoritative
    /// source; see `preferredModels()`. Kept separate rather than overwriting
    /// `models_` so a failed billing fetch cannot blank a working catalogue.
    QList<SunoModelInfo> billingModels_;
    std::optional<SunoBillingInfo> billing_;
    std::optional<TosAcceptance> tosAcceptance_;
    std::optional<OnboardingState> onboardingState_;
    GateResolver gates_;
};

} // namespace vc::suno
