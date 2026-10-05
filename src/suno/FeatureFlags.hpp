#pragma once
// FeatureFlags.hpp — the ONE owner of "which Suno product surfaces may this
// client use, and why".
//
// ── Why this file exists ────────────────────────────────────────────────────
// Found 2026-10-05 by reading `SunoAccountManager::fetchSession`, which has
// called `GET /api/session/` (`endpoints::SESSION`, `SunoEndpoints.hpp:86`)
// since it shipped and parses **two** of that envelope's top-level keys:
// `user` (`SunoAccountManager.cpp:231`) and `models` (`:238`). The rest —
// `flags`, `roles`, `statsig_custom_properties`, `configs`, `experiments`,
// `data_sharing_consent` — are read by nobody.
//
// `flags` is not decoration. It is the server's own account-gating plane, and
// the surface this client most obviously lacks is a UI that can say **why**:
// `SunoBridge::generationAvailable()` returns a hardcoded `false`
// (`SunoBridge.cpp:434`) while `SunoPanel.qml:35-67` renders a real model
// dropdown behind it, so the user gets a well-built door to nowhere and not one
// sentence explaining it. A UI cannot honestly explain an absence until
// something owns the answer. This file is that something. It decides; it does
// not fetch — `parseSessionCapabilities` takes a `QJsonObject` the caller
// already has, and there is no network code here.
//
// ── The two axes, and why they are kept apart ───────────────────────────────
// **The flag map is DATA.** It is server-owned and it drifts, measurably:
//   * anonymous prod read, 2026-10-04:  **47** flags, every value `true`
//   * anonymous staging read, 2026-10-04: **58** flags — 14 staging-only,
//     3 prod-only
//   * *authenticated* prod read, 2026-09-30: **57** flags
// The 47 and the 57 differ by 10 account-gated flags an anonymous request
// never sees (`voices-ui`, `voices-ui-verify`, `vocal-gender-toggle`,
// `under-over-painting`, `studio-midi-conditioning`, `create-sounds`,
// `custom-model-ui`, `auk-model`, `bluejay-model`, `playlist-condition`). So
// the anonymous map is a strict *subset* of a real account's map — it is not
// "the" map. Any list of flag names baked into a `constexpr` table would be
// wrong twice over: it would miss those 10 for every signed-in user, and it
// would age into a snapshot that silently over-reports availability as the
// server renames and retires flags. `custom-model-ui` made the promotion
// visible inside a single observation window — staging-only in the 2026-10-04
// anonymous snapshot, already present in an authenticated prod read on
// 2026-09-30. Hence `SessionCapabilities` keeps what the server *sent*
// (`flags`, `flagsFalse`, `flagsNonBool`, `flagsRaw`, `raw`) and knows the name
// of no flag at all.
//
// **The feature catalog is CODE.** `FeatureDefinition` is ours and is versioned
// with us: which product surfaces we might offer, which server flag each one
// keys on, and the evidence floor it demands. It deliberately does **not**
// contain the flag map. A flag name appears here as *one input to one gate*,
// never as an assertion that the flag exists.
//
// ── Tolerance on the wire, strictness on evidence ───────────────────────────
// On the wire this parser is deliberately tolerant — a wrong-typed field is
// skipped, never fatal, matching `ClipParser`'s documented contract and the
// nesting walk at `SunoAccountManager.cpp:45-75` (flat, then `session`, then
// `data`). On *evidence* it is strictly fail-closed: `GateResolver` refuses any
// surface graded below `hosts::Evidence::Captured`, and no flag, config, tier or
// capability can override that. Being liberal about what we accept is only safe
// because being conservative about what we build on it is a separate and
// explicit step, and it is step 2 of six below.
//
// ── Resolution order, exactly as implemented in FeatureFlags.cpp ───────────
// `GateResolver::evaluate` applies these in sequence and stops at the first
// one that decides:
//   1. **Excluded** — a deliberate product/legal decision carried by
//      `ExclusionReason`. Never available. Reported with the reason, because
//      "we decided not to" and "we could not" are different sentences and a
//      user asking about a subscription deserves the first one.
//   2. **Evidence below `Captured`** → `EvidenceBlocked`, unconditionally.
//      No flag, no config, no tier, no capability, and no client switch can
//      override it. AGENTS.md §1: *"Never promote `[LEAD]` to `[T1]` without a
//      direct human capture"* and *"Fail closed on unverified hosts."* A server
//      flag is not a human capture — it is the server telling us it has a
//      feature, which is a different claim from showing us its contract.
//   3. **Tier** — a surface that only exists on `hosts::Tier::Staging` (or
//      later) resolves to `ServerGated` while the active tier is Production.
//   4. **Server flag absent** → `ServerGated`. Every flag named in
//      `FeatureDefinition::serverFlags` must be present **and** not explicitly
//      `false`; an empty list means the surface is not flag-gated at all.
//   5. **Server flag present, client switch off** → `LocallyDisabled`. Every
//      switch starts off. This is the fail-closed default, and it is what
//      replaces `generationAvailable()`'s hardcoded `false` with something that
//      can be turned on for a reason.
//   6. Otherwise → `Available`.
//
// Note what step 2 costs us and why it is still right: `Personas`,
// `RealtimePush`, `AccountDeletion` and `ExperimentalOrchestrator` resolve
// `EvidenceBlocked` today and no switch can change that. That is the point. A
// captured flag on an uncaptured route is exactly the trap AGENTS.md §1 exists
// to close.
//
// ── No std::expected anywhere in this header ────────────────────────────────
// Every function below is total: the parse cannot fail by design (that is the
// tolerance contract), and `setLocallyEnabled`'s `bool` is the outcome a caller
// needs. There is no fallible path to wrap, and manufacturing one — an
// `expected` around a function with no failure mode — is the kind of ceremony
// that makes the next reader hunt for the failure that does not exist.

#include "suno/CapturedHosts.hpp"

#include <QJsonObject>
#include <QString>

#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc::suno {

// ── Parsed session capabilities ─────────────────────────────────────────────

/// How the `flags` key actually arrived, so a diagnostics panel can say
/// "the server never mentioned flags" differently from "flags was not an
/// object". The two were one verdict before this enum existed, and the
/// difference is exactly the difference between a dark UI and a mis-parse.
enum class FlagMapShape {
    /// No `flags` key at any tolerated depth.
    Absent,
    /// A `flags` key exists but is not an object (array, string, bool).
    /// Recorded and skipped; the rest of the envelope still parses.
    NotAnObject,
    /// Parsed as an object. An empty object is still `Present` — an account
    /// with zero flags is a fact, not a missing key.
    Present,
};

/// Flag names, ordered by a transparent comparator.
///
/// **Measured on this machine's compiler, 2026-10-05.** The obvious spelling —
/// `std::unordered_set<std::string, std::hash<std::string_view>, std::equal_to<>>`
/// — does not build here: `contains(std::string_view)` fails on the CommandLineTools
/// libc++ (`no matching member function for call to 'contains'`), verified with
/// `c++ -std=gnu++23` against a two-line probe, not merely reported by an editor.
/// Heterogeneous lookup for unordered containers is a library feature that
/// libc++ only picked up late, and this project's macOS verification build uses
/// that libc++.
///
/// `std::set` with `std::less<>` has had heterogeneous lookup since C++14 and
/// works on both target toolchains, so `hasFlag(std::string_view)` allocates
/// nothing on either. The cost is O(log n) string comparisons instead of a hash:
/// with 47-57 entries that is about six comparisons against short strings,
/// against the unordered_set above, which on *this* toolchain would allocate a
/// `std::string` for every probe. Not a close call, and the portable spelling is
/// the one that builds.
using FlagNameSet = std::set<std::string, std::less<>>;

/// The gating plane of one `GET /api/session/` envelope.
///
/// Presence and truth are recorded **separately**, which is not pedantry. The
/// corpus measured only anonymous reads and found every value `true` — a
/// presence list — but an authenticated read may not be, and a single `false`
/// entry read as "present" would *unlock* a surface the server just switched
/// off. That is a fail-open bug, and splitting the sets is what makes it
/// impossible: `flags` holds names that were not explicitly `false`.
struct SessionCapabilities {
    /// Flag names the server did not send as `false`. Enabled, or at least not
    /// explicitly disabled. A flag absent from the anonymous map is simply not
    /// here — see the file header on the 47-vs-57 gap.
    FlagNameSet flags;

    /// Flag names sent as an explicit JSON `false`. Present, and off. Keeping
    /// these is what lets a diagnostic say "the server told us this exists and
    /// turned it off" instead of "no such feature".
    FlagNameSet flagsFalse;

    /// Flag names whose value was neither `true` nor `false` (a string, a
    /// number, null, an object). Recorded because the tolerance rule requires
    /// recording, and because a non-bool is worth naming: we treat it as
    /// **not** `false` and therefore as enabled. That is the only judgement in
    /// this file that could read as fail-open, and it is taken deliberately —
    /// the server enforces its own entitlements regardless, so the worst case
    /// is a visible control the server refuses, not an access we did not have.
    FlagNameSet flagsNonBool;

    /// The `flags` map exactly as received, at whatever depth it was found.
    /// A copy, not a re-serialisation: `raw` already holds the envelope, but
    /// this one is the map *without* having to re-walk the nesting.
    QJsonObject flagsRaw;

    /// Where `flags` was found. See `FlagMapShape`.
    FlagMapShape flagMapShape{FlagMapShape::Absent};

    /// `roles` preserved verbatim. `{}` anonymously, populated on an
    /// authenticated read.
    QJsonObject roles;

    /// `statsig_custom_properties` preserved verbatim:
    /// `{custom:{user_plan_key,us_state,...}, custom_ids:{...}}`. The only
    /// observed carrier of plan and US state for a signed-in account. Preserved
    /// and **not** interpreted — see `FeatureDefinition::AccountProfile`.
    QJsonObject statsigCustom;

    /// `configs["gen-endpoint"]` — the server-selected generate route. Null on
    /// every anonymous read (the corpus saw `configs: null`); populated only
    /// when authenticated, which is what makes it a useful signal rather than
    /// a constant.
    std::optional<std::string> genEndpoint;

    /// `experiments` preserved verbatim. Null anonymously.
    QJsonObject experiments;

    /// `data_sharing_consent` preserved verbatim. Its contents were never
    /// characterised by any capture. Reading a consent record is not the same
    /// act as writing one; `FeatureGate::PrivacyConsentWrites` is excluded for
    /// the write.
    QJsonObject dataSharingConsent;

    /// The whole envelope, exactly as handed in, nesting included.
    QJsonObject raw;

    /// True when the server enabled `name` — i.e. sent it and did not send
    /// `false`. This is the query `GateResolver` uses.
    [[nodiscard]] bool hasFlag(std::string_view name) const { return flags.contains(name); }

    /// True when the server mentioned `name` at all, enabled or not. The
    /// diagnostic query: it separates "not for this account" from "not a
    /// feature we know about".
    [[nodiscard]] bool hasFlagEntry(std::string_view name) const {
        return flags.contains(name) || flagsFalse.contains(name);
    }

    /// Distinct flag names seen, across the enabled and disabled sets. This is
    /// the **presence** count, because that is the number the corpus quotes — an
    /// observed anonymous prod envelope must report 47 here and an authenticated
    /// one 57.
    ///
    /// `flagsNonBool` is deliberately NOT added: a non-bool name is recorded in
    /// both `flags` (enabled, because it is not an explicit `false`) and
    /// `flagsNonBool` (diagnosed), so summing all three would double-count it.
    /// `flags` is the authoritative enabled set and `flagsFalse` is disjoint from
    /// it by construction, so their sum is the union.
    [[nodiscard]] int flagCount() const {
        return static_cast<int>(flags.size() + flagsFalse.size());
    }
};

/// Tolerant parse of one `GET /api/session/` envelope.
///
/// Never throws, and cannot fail. A `flags` value that is not an object, a
/// `configs` that is null, a missing key, a non-bool flag value — each is
/// recorded and stepped over, because the alternative (refusing the envelope)
/// throws away the one thing this endpoint is uniquely good for. Nothing is
/// invented: what cannot be understood is reported as an empty shape rather
/// than guessed at.
[[nodiscard]] SessionCapabilities parseSessionCapabilities(const QJsonObject& envelope);

// ── The feature catalog ─────────────────────────────────────────────────────

/// A product surface this client might offer. Ordered; the catalog is stored in
/// exactly this order, a `static_assert` pins it, and `evaluateAll` relies on
/// it for its deterministic ordering.
///
/// The first nineteen enumerators are the project's original set. The last
/// eight were added because the inventory's §4 route areas do not fit honestly
/// into them, and mis-filing a route into a neighbouring gate is precisely the
/// kind of small lie this file exists to stop: `Billing` is the *read-only*
/// billing surface and must not be dragged down by the excluded mutations;
/// `AccountDeletion` says nothing about `/api/user/metadata`; folding
/// `/api/personalization/memory` into `Personas` would have understated a
/// `[T1]` surface by burying it behind a gate blocked on absent evidence.
enum class FeatureGate {
    Library,
    Explore,
    Notifications,
    Playlists,
    Projects,
    Generation,
    Upload,
    CustomModels,
    Contests,
    Personas, ///< Voices.
    Styles,
    LyricsCowrite,
    VideoGeneration,
    Stems,
    Sharing,
    RealtimePush,
    ExperimentalOrchestrator,
    Billing,
    AccountDeletion,

    // ── Added: areas with no honest home above ─────────────────────────────
    AccountProfile,       ///< session/user/profile/onboarding, personalization
    ClipRelations,        ///< parent, remixes, similar, comments
    Rights,               ///< attribution, mango rights, trust-safety appeals
    MediaDelivery,        ///< playback and download from captured media hosts
    AppChrome,            ///< modals and CMS nudges
    MusicPlayer,          ///< cross-device playbar state
    BillingMutations,     ///< EXCLUDED: money-moving subscription writes
    PrivacyConsentWrites, ///< EXCLUDED: consent records written for the user
};

/// Number of gates, and the size of the resolver's per-gate switch table.
/// Pinned against `featureCatalog().size()` by a `static_assert` in the .cpp,
/// so adding an enumerator without adding an entry is a build error rather than
/// a silently-unavailable surface.
inline constexpr std::size_t kFeatureGateCount =
        static_cast<std::size_t>(FeatureGate::PrivacyConsentWrites) + 1;

/// Named, and returning `const char*` deliberately: Qt's QCOMPARE finds a
/// `toString` overload by ADL and requires exactly that return type, so an enum
/// whose `toString` returns `std::string_view` cannot be compared in a test at
/// all. (The codebase convention — see `HttpPolicy.hpp:200-205`.)
[[nodiscard]] constexpr const char* toString(const FeatureGate gate) noexcept {
    switch (gate) {
        case FeatureGate::Library:
            return "library";
        case FeatureGate::Explore:
            return "explore";
        case FeatureGate::Notifications:
            return "notifications";
        case FeatureGate::Playlists:
            return "playlists";
        case FeatureGate::Projects:
            return "projects";
        case FeatureGate::Generation:
            return "generation";
        case FeatureGate::Upload:
            return "upload";
        case FeatureGate::CustomModels:
            return "custom-models";
        case FeatureGate::Contests:
            return "contests";
        case FeatureGate::Personas:
            return "personas";
        case FeatureGate::Styles:
            return "styles";
        case FeatureGate::LyricsCowrite:
            return "lyrics-cowrite";
        case FeatureGate::VideoGeneration:
            return "video-generation";
        case FeatureGate::Stems:
            return "stems";
        case FeatureGate::Sharing:
            return "sharing";
        case FeatureGate::RealtimePush:
            return "realtime-push";
        case FeatureGate::ExperimentalOrchestrator:
            return "experimental-orchestrator";
        case FeatureGate::Billing:
            return "billing";
        case FeatureGate::AccountDeletion:
            return "account-deletion";
        case FeatureGate::AccountProfile:
            return "account-profile";
        case FeatureGate::ClipRelations:
            return "clip-relations";
        case FeatureGate::Rights:
            return "rights";
        case FeatureGate::MediaDelivery:
            return "media-delivery";
        case FeatureGate::AppChrome:
            return "app-chrome";
        case FeatureGate::MusicPlayer:
            return "music-player";
        case FeatureGate::BillingMutations:
            return "billing-mutations";
        case FeatureGate::PrivacyConsentWrites:
            return "privacy-consent-writes";
    }
    return "unknown";
}

/// Why a surface is off. Six verdicts, because "disabled" is not an answer a
/// user can act on and one that sends a future reader hunting.
enum class GateStatus {
    /// Evidence, flags, tier and client switch all permit it.
    Available,
    /// Evidence permits it, but the server has not enabled the flag for this
    /// account — or this surface does not exist on the active tier.
    ServerGated,
    /// Evidence permits it and the server enables it, but the client switch is
    /// off. One deliberate switch away.
    LocallyDisabled,
    /// The evidence floor is below `Captured`. Never available, per the
    /// resolution order's step 2.
    EvidenceBlocked,
    /// A deliberate product or legal decision. Carries an `ExclusionReason`.
    Excluded,
};

[[nodiscard]] constexpr const char* toString(const GateStatus status) noexcept {
    switch (status) {
        case GateStatus::Available:
            return "available";
        case GateStatus::ServerGated:
            return "server-gated";
        case GateStatus::LocallyDisabled:
            return "locally-disabled";
        case GateStatus::EvidenceBlocked:
            return "evidence-blocked";
        case GateStatus::Excluded:
            return "excluded";
    }
    return "unknown";
}

/// Why a gate is `Excluded`. `None` means "not excluded", so the field is
/// self-describing and needs no parallel bool that could disagree with it.
enum class ExclusionReason {
    None,
    /// Subscription cancel/pause/change-plan, coupons, checkout sessions and
    /// auto-reload toggles. A third-party client that can cancel a
    /// subscription or apply a coupon is a support and terms-of-service
    /// surface. This judgement does not evaporate if a capture ever lands.
    MoneyMoving,
    /// `data-sharing-consent` and `vip_program_acceptance` — writes that record
    /// privacy or programme consent **on the user's behalf**. A desktop client
    /// that writes consent records is a legal-terms problem with real
    /// consequences, not a missing feature.
    LegalConsentWrite,
};

[[nodiscard]] constexpr const char* toString(const ExclusionReason reason) noexcept {
    switch (reason) {
        case ExclusionReason::None:
            return "none";
        case ExclusionReason::MoneyMoving:
            return "money-moving";
        case ExclusionReason::LegalConsentWrite:
            return "legal-consent-write";
    }
    return "unknown";
}

/// One catalog row. A `constexpr` literal, so the whole catalog is a compile
/// artefact: it cannot be mutated at runtime and cannot drift from the binary
/// that ships it.
struct FeatureDefinition {
    FeatureGate gate;
    /// Short human name for a UI row. No emoji: this string is also what a
    /// diagnostics report prints.
    std::string_view title;
    /// Owning area, matching the inventory's route sections ("library",
    /// "generation", "account", "media", "app").
    std::string_view area;
    /// Evidence floor, and the strongest of the routes behind this gate. A
    /// surface is only as evidenced as its least-evidenced half; a `[T1]`
    /// status read next to an uncaptured submission route is a reader, and
    /// `VideoGeneration`'s note says so rather than claiming a capability.
    hosts::Evidence evidence;
    /// Comma-separated server flag names that unlock this gate. **All** of them
    /// must be present and not `false` — AND, not OR, because a surface gated
    /// on two flags is gated on both. Empty means "not flag-gated", which is a
    /// real and common answer: the flag map is a server *feature* switch, and
    /// most of this client's surfaces are not features Suno switches.
    std::string_view serverFlags;
    /// The lowest tier on which this surface exists. Staging-only surfaces are
    /// `ServerGated` while the active tier is Production.
    hosts::Tier minTier{hosts::Tier::Production};
    /// `None` for everything but the two excluded surfaces.
    ExclusionReason exclusion{ExclusionReason::None};
    /// Why this grade, and what it would take to change it. Every row has one;
    /// a row without it is a declaration pretending to be a decision.
    std::string_view note;
};

/// The whole catalog, in `FeatureGate` order. Every gate appears exactly once —
/// a `static_assert` in the .cpp checks both that and the ordering, so a new
/// enumerator without a row is a build error rather than a surface that quietly
/// resolves to nothing.
[[nodiscard]] std::span<const FeatureDefinition> featureCatalog() noexcept;

/// The row for `gate`, or `nullptr` for an enumerator with no row. Cannot
/// happen for any gate in this header; `nullptr` is returned rather than
/// asserted so a future enumerator cannot turn a diagnostic into a crash.
[[nodiscard]] const FeatureDefinition* findFeature(FeatureGate gate) noexcept;

// ── The resolver ────────────────────────────────────────────────────────────

struct GateVerdict {
    GateStatus status{GateStatus::EvidenceBlocked};
    FeatureGate gate{FeatureGate::Library};
    /// The sentence a user sees. Names the actual cause — the missing flag, the
    /// evidence grade, the tier, the switch — and never a bare "disabled".
    QString reason;
};

/// Decides availability from three inputs: what the server sent, which tier is
/// active, and what this client has deliberately switched on.
///
/// Default-constructed, it resolves **every** gate to something other than
/// `Available`: the switch table starts all-off, and the evidence floor and
/// the two exclusions hold regardless. That is the fail-closed default stated
/// once, in one place, instead of as a hardcoded `false` in each caller.
class GateResolver {
public:
    GateResolver() = default;

    /// Replace the parsed capability set. A signed-out resolver simply holds an
    /// empty one, and every flag-gated surface reads `ServerGated` — the same
    /// verdict an empty anonymous flag map produces, which is correct: an
    /// account with no map is not an account with every feature.
    void setCapabilities(SessionCapabilities caps);

    /// Adopt the host tier policy. Until this is called the resolver holds a
    /// default-constructed `TierPolicy`, i.e. the fail-closed default that
    /// `CapturedHosts` defines — the captured production allowlist.
    void setTierPolicy(const hosts::TierPolicy& tier);

    /// The explicit client-side master switch for one gate. Off by default.
    ///
    /// Returns false and **changes nothing** when the request cannot be
    /// honoured: enabling a gate whose evidence is below `Captured`, or one
    /// that carries an exclusion. Storing the flag anyway would be a lie waiting
    /// to be misread — the switch would say `true` while the verdict said
    /// `EvidenceBlocked`, and a diagnostics panel would have to choose which of
    /// the two to believe.
    ///
    /// `[[nodiscard]]` because ignoring this return is exactly the mistake:
    /// a refused enable that was not noticed looks identical to an accepted one.
    [[nodiscard]] bool setLocallyEnabled(FeatureGate gate, bool enabled);

    [[nodiscard]] GateVerdict evaluate(FeatureGate gate) const;

    [[nodiscard]] bool isAvailable(FeatureGate gate) const {
        return evaluate(gate).status == GateStatus::Available;
    }

    /// Everything, for a diagnostics panel. Deterministic: catalog order, which
    /// is `FeatureGate` order, pinned by a `static_assert`.
    [[nodiscard]] std::vector<GateVerdict> evaluateAll() const;

    /// The parsed capabilities, for a caller that also wants `roles`,
    /// `genEndpoint` or the raw map. Read-only; the resolver never mutates
    /// what it was given.
    [[nodiscard]] const SessionCapabilities& capabilities() const { return caps_; }

private:
    /// `false` when the gate's evidence floor is not met or it is excluded —
    /// the two conditions under which `setLocallyEnabled` must refuse.
    [[nodiscard]] bool switchMayHold(FeatureGate gate) const;

    SessionCapabilities caps_{};
    hosts::TierPolicy tier_{};
    std::array<bool, kFeatureGateCount> locallyEnabled_{};
};

} // namespace vc::suno
