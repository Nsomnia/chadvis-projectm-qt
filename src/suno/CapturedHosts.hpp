#pragma once
// CapturedHosts.hpp - the ONE owner of this tree's outbound-host policy and
// deployment-tier gating.
//
// ── Why this file exists ──────────────────────────────────────────────────────
// Found by grep: the host allowlist was written three times over, with no shared
// owner and no notion of tier.
//   * src/suno/auth/AuthHeaders.cpp:49  isAllowedStudioApiUrl()  - hardcoded
//     "studio-api-prod.suno.com" (AuthHeaders.hpp:22). The only
//     credential-transport choke point in the tree.
//   * src/suno/SunoDownloader.cpp:99    isUsableCapturedUrl()    - hardcoded
//     "audiopipe.suno.ai", plus its own sentinel/path/userinfo/port rules.
//   * src/suno/ClipParser.cpp:67        isCapturedImageUrl()     - hardcoded
//     "cdn1.suno.ai" / "cdn2.suno.ai".
// Three predicates, three literals, three sets of edge rules, drifting apart, and a
// fourth host string parked in SunoEndpoints.hpp:45. None of them can answer "is
// this host staging?", because none of them knows staging exists.
//
// The defect was never "someone forgot a host". It is that a policy with no owner
// is a policy with N copies, and adding a deployment tier would mean editing a
// literal in a function whose job is unrelated to deployment. One owner is the
// fix; a checklist is not.
//
// ── Why the evidence grade is the load-bearing field ─────────────────────────
// This file deliberately mirrors the discipline in
// docs/suno_api/ENDPOINT-INVENTORY.md, because that document is the API-spec
// master (AGENTS.md §1) and a second, quieter grading scheme in a header would be
// exactly the "two files claim the same thing" bug AGENTS.md §6 forbids.
//   * Evidence::Captured  == `[T1]`. A real request AND a real response were
//     directly observed.
//   * Evidence::Registered == existence proven without a contract. The recon
//     corpus proves route/host existence for 62 endpoints that answered 401
//     unauthenticated; a 401 observes no request and no body, so there is nothing
//     to wire.
//   * Evidence::Lead == `[LEAD]`. Appears only as a string in a shipped JS
//     bundle or as a constructed path. Never wire it, never promote it without a
//     direct human capture.
// AGENTS.md §1: never promote `[LEAD]` to `[T1]` without a direct human capture;
// fail closed on unverified hosts; never send cookies, bearers, or automatic remote
// image requests to an absolute host absent from a captured allowlist. `[T1]` means
// "directly captured" and does NOT mean "shipped" - the inventory's `Implemented`
// column is an independent axis, and so is Tier below.
//
// ── What a tier is, and is not ────────────────────────────────────────────────
// Tier is a *deployment* axis, orthogonal to evidence. Production is the only tier
// this build speaks to by default; Staging requires explicit opt-in via
// TierPolicy::raiseTo, which is deliberately the only mutator in the class.
// Experimental exists so the Orpheus/Modal surface has a name in one table
// instead of a `[LEAD]` comment next to a base URL.
//
// Tier matching in mayReceiveCredential is EXACT, not a floor: a Production host
// is reachable only while the active tier is Production, and a Staging host only
// while it is Staging. That is the fail-closed reading - raising the tier narrows
// the reachable set to that tier rather than widening it to "production plus
// staging" - and it is what lets raiseTo() refuse a tier the registry has no hosts
// for, because such a tier could only ever narrow the set to nothing. The cost is
// that a caller wanting both tiers at once needs two policies; the benefit is
// that "I enabled staging" can never be a sentence that also means "and I kept
// talking to prod".

#include <QString>
#include <QtGlobal>

#include <optional>
#include <span>
#include <string_view>

class QUrl;

namespace vc::suno::hosts {

/// Deployment tier. See the file header for why matching is exact.
enum class Tier {
    /// The captured, user-facing surfaces. The default, and the only tier this
    /// build talks to without somebody asking for something else.
    Production,
    /// Suno's non-production API surface. Never reached without an explicit
    /// `TierPolicy::raiseTo(Tier::Staging)`.
    Staging,
    /// The Orpheus/Modal surface. Named so it has a home; see the early
    /// `return false` in `mayReceiveCredential` - a host at this tier can never
    /// receive a credential, at any tier, permanently.
    Experimental,
};

/// `const char*` deliberately, not `std::string_view`: Qt's QCOMPARE finds a
/// `toString` overload by ADL and requires exactly that return type, so an enum
/// whose toString returns a view cannot be compared in a test at all. Same reason
/// as vc::suno::http::toString(PolicyError) (HttpPolicy.hpp:205) and
/// vc::suno::toString(DownloadState).
[[nodiscard]] constexpr const char* toString(const Tier tier) noexcept
{
    switch (tier) {
        case Tier::Production: return "production";
        case Tier::Staging: return "staging";
        case Tier::Experimental: return "experimental";
    }
    return "unknown";
}

/// Why a host is in the table. This is the `[T1]` / `[LEAD]` axis from
/// docs/suno_api/ENDPOINT-INVENTORY.md, in code, so a predicate can read it.
enum class Evidence {
    /// `[T1]`. A real request AND a real response were directly observed.
    Captured,
    /// Existence proven, contract unknown. A 401 observes no request and no
    /// response body, so there is nothing to wire - but the host is real.
    Registered,
    /// Seen only as a **value inside another host's response**, never as a host
    /// this client ever sent a request to. This grade exists because conflating
    /// it with `Registered` is a credential-disclosure hole, not a nicety:
    /// `Registered` is established *by* sending a request (a 401 is a request
    /// that carried a credential and was rejected), whereas a response-value
    /// host has never received one. Nothing in AGENTS.md §1 permits mailing a
    /// bearer to a host whose only appearance is a URL string the API handed us.
    ResponseValue,
    /// `[LEAD]`. A string in a bundle, or a path someone constructed. AGENTS.md §1
    /// forbids wiring this and forbids promoting it without a direct human
    /// capture.
    Lead,
};

[[nodiscard]] constexpr const char* toString(const Evidence evidence) noexcept
{
    switch (evidence) {
        case Evidence::Captured: return "captured";
        case Evidence::Registered: return "registered";
        case Evidence::ResponseValue: return "response-value";
        case Evidence::Lead: return "lead";
    }
    return "unknown";
}

/// What the host is *for*. Named by role rather than by the subsystem that
/// happens to use it, so moving a route between services cannot silently change
/// which allowlist it lands in.
enum class Role {
    /// The captured `/api/*` studio surface. Both tiers of it.
    StudioApi,
    /// Clerk auth envelopes. Never hand-rolled (AGENTS.md §1).
    ClerkAuth,
    /// Captured media bodies: `audiopipe.suno.ai`.
    MediaOrigin,
    /// Captured image assets: the `cdn1`/`cdn2` pair.
    ArtworkOrigin,
    /// Captured progressive-media CloudFront distribution.
    CloudFront,
    /// suno.com itself. `Origin`/`Referer` only - never a request target.
    WebApp,
    /// Ably SSE push. Known only as a response value; see the registry note.
    Realtime,
    /// Direct-to-storage multipart upload target. Present because
    /// `SunoAudioUploadService` contacts it today with a hardcoded literal; a
    /// registry that omitted a host the tree actually reaches would not be the
    /// single owner it claims to be.
    UploadOrigin,
    /// The Orpheus/Modal host. A name for a refusal, not for a destination.
    ExperimentalOrchestrator,
};

[[nodiscard]] constexpr const char* toString(const Role role) noexcept
{
    switch (role) {
        case Role::StudioApi: return "studio-api";
        case Role::ClerkAuth: return "clerk-auth";
        case Role::MediaOrigin: return "media-origin";
        case Role::ArtworkOrigin: return "artwork-origin";
        case Role::CloudFront: return "cloudfront";
        case Role::WebApp: return "web-app";
        case Role::Realtime: return "realtime";
        case Role::UploadOrigin: return "upload-origin";
        case Role::ExperimentalOrchestrator: return "experimental-orchestrator";
    }
    return "unknown";
}

/// One row. `note` is not decoration: it is the only place the evidence grade gets
/// to explain itself, and `Evidence::Registered` is permitted by the grade gate,
/// so for that row the note is load-bearing. Keep it short, keep the capture name
/// in it, and keep it true.
struct HostRule {
    /// Bare host, no scheme and no trailing slash. ASCII; matched case-insensitively
    /// (DNS is), and never with a trailing dot - see `findRule`.
    std::string_view host;
    Tier tier;
    Evidence evidence;
    Role role;
    std::string_view note;
};

/// The whole table, alphabetical by host so a test can index it and a reviewer can
/// spot a row out of place. No host may be added without an `Evidence` grade whose
/// `note` names the capture that grade rests on.
[[nodiscard]] std::span<const HostRule> hostRegistry() noexcept;

/// Rows for one role. Used to replace the per-file host literals: the downloader
/// wants `Role::MediaOrigin`, the clip parser wants `Role::ArtworkOrigin`.
[[nodiscard]] std::span<const HostRule> hostsForRole(Role role) noexcept;

/// Rows for one tier. One host at Staging, one at Experimental, eight at Production.
[[nodiscard]] std::span<const HostRule> hostsForTier(Tier tier) noexcept;

/// Exact host lookup, case-insensitive, no allocation. `nullptr` when the host is
/// not in the table, which is the fail-closed answer for an unverified host.
[[nodiscard]] const HostRule* findRule(std::string_view host) noexcept;

/// May the evidence grade alone permit a credential?
///
/// `Lead` never does (AGENTS.md §1), and neither does `ResponseValue`. The
/// second exclusion is the reason that grade was added: without it, the only
/// host graded `ResponseValue` — the realtime push origin — satisfied this
/// predicate, and the sole guard against mailing the Suno bearer to a third
/// party's realtime infrastructure was a prose note in a table row. A note is
/// not a control. Notes get truncated by editors; this gets compiled.
///
/// `Registered` still permits, and that is correct rather than lax: the way a
/// route's existence gets established is by sending it a request and being
/// answered `401`, so a `Registered` host has demonstrably already received a
/// credential and refused it.
[[nodiscard]] constexpr bool evidencePermitsCredential(Evidence evidence) noexcept
{
    return evidence == Evidence::Captured || evidence == Evidence::Registered;
}

/// Why a URL is refused. Distinct values with distinct prose, because "blocked"
/// tells a user nothing and a future reader nothing either. `None` is never
/// returned by `classifyRefusal`; it exists so a diagnostics panel can hold one
/// of these and render it.
enum class Refusal {
    /// Not permitted. Empty/relative/malformed - an allowlist cannot be evaluated
    /// against a target that is not an absolute URL.
    InvalidUrl,
    /// Not https. A credential sent over anything else is sent in the clear.
    SchemeNotHttps,
    /// A port other than default/443. A non-default port is a different origin,
    /// and no capture ever saw a credential go there.
    NonDefaultPort,
    /// userinfo present.
    UserInfoPresent,
    /// Host absent from the registry entirely (AGENTS.md §1: fail closed).
    HostUnknown,
    /// A registry host whose tier the active tier does not permit. Exact match.
    TierNotPermitted,
    /// A registry host at `Tier::Experimental`. Permanent, at every tier, and
    /// reported as its own cause rather than as a confusing tier mismatch.
    ExperimentalRefused,
    /// A registry host whose evidence grade is too weak to carry a credential.
    EvidenceTooWeak,
};

[[nodiscard]] constexpr const char* toString(const Refusal refusal) noexcept
{
    switch (refusal) {
        case Refusal::InvalidUrl: return "invalid-url";
        case Refusal::SchemeNotHttps: return "scheme-not-https";
        case Refusal::NonDefaultPort: return "non-default-port";
        case Refusal::UserInfoPresent: return "userinfo-present";
        case Refusal::HostUnknown: return "host-unknown";
        case Refusal::TierNotPermitted: return "tier-not-permitted";
        case Refusal::ExperimentalRefused: return "experimental-refused";
        case Refusal::EvidenceTooWeak: return "evidence-too-weak";
    }
    return "unknown";
}

/// The single implementation of the policy. `std::nullopt` means permitted;
/// anything else is the one reason it is not. Both `mayReceiveCredential` and
/// `TierPolicy::describeRefusal` are thin wrappers over this, so a predicate and a
/// user-facing sentence can never disagree about what was refused or why.
[[nodiscard]] std::optional<Refusal> classifyRefusal(const QUrl& url, Tier activeTier) noexcept;

/// May this URL be sent a bearer or a cookie? Fails closed.
///
/// Checks, in order: valid and absolute; https; default port or 443; no userinfo;
/// host present in the registry; not `Tier::Experimental`; tier matches exactly;
/// evidence grade permits it. Note what is NOT here: presence in the registry is
/// not a credential grant. `cdn1.suno.ai` is in the table because artwork may be
/// fetched; it must still be fetched anonymously.
[[nodiscard]] bool mayReceiveCredential(const QUrl& url, Tier activeTier) noexcept;

/// Tiers a `TierPolicy` may be raised to. `Experimental` never: the registry does
/// list an Experimental host, but that host is permanently refused as a credential
/// transport, so "selecting" the tier could only ever narrow the reachable set to
/// nothing.
[[nodiscard]] constexpr bool isSelectableTier(const Tier tier) noexcept
{
    return tier == Tier::Production || tier == Tier::Staging;
}

/// The active tier, and the only mutable policy state in the tree.
///
/// Fail closed by construction: a default-constructed policy is Production and
/// cannot become anything else without an explicit `raiseTo`. Not thread-safe, and
/// deliberately so - this is configured once before requests start, and making it
/// atomic would only make it tempting to flip mid-flight, which is the one moment
/// where narrowing the reachable host set could strand an in-flight request.
class TierPolicy {
public:
    /// Production. Not configurable-by-accident: there is no constructor that
    /// takes a tier, so no call site can enable staging by forgetting that it
    /// should not have.
    TierPolicy() noexcept;

    [[nodiscard]] Tier activeTier() const noexcept { return activeTier_; }

    /// The only mutation. Idempotent for Production. Returns `false` - and changes
    /// nothing - when the requested tier is not selectable (Experimental) or when
    /// the registry has no host at it, which is the honest answer: such a tier
    /// could only ever refuse everything.
    bool raiseTo(Tier requested) noexcept;

    void resetToProduction() noexcept;

    [[nodiscard]] bool stagingEnabled() const noexcept
    {
        return activeTier_ == Tier::Staging;
    }

    /// A user-facing sentence naming exactly why this URL would be refused at
    /// `activeTier`, or an empty string when it is permitted (so a caller can show
    /// nothing without a special case).
    ///
    /// The tier is a parameter rather than read from `activeTier_` on purpose: a
    /// diagnostics panel can ask the counterfactual - "what would this say if
    /// staging were on?" - without mutating policy to find out.
    [[nodiscard]] QString describeRefusal(const QUrl& url, Tier activeTier) const;

private:
    Tier activeTier_{Tier::Production};
};

} // namespace vc::suno::hosts