#include "suno/CapturedHosts.hpp"

#include <QUrl>

#include <array>
#include <format>
#include <string>

namespace vc::suno::hosts {

namespace {

// ── The registry ──────────────────────────────────────────────────────────────
// One named constant per host, so the alphabetical master table and the per-role /
// per-tier views below are compositions of the same objects rather than two
// transcriptions of the same literals. `registryIsConsistent()` proves at compile
// time that every view's row appears in the master table with identical fields, so
// the two shapes cannot drift.
//
// No row may be added without an Evidence grade whose note names the capture that
// grade rests on. A bare host string in this file is a host we may send a cookie
// to; that is a bigger claim than it looks.

// Media bodies. The origin the captured `media_urls[]` entries actually resolved
// to, and what src/suno/SunoDownloader.cpp:121 fetches from. [T1] response values in
// the 2026-09-24 clip objects.
constexpr HostRule kAudiopipe{"audiopipe.suno.ai", Tier::Production, Evidence::Captured,
                              Role::MediaOrigin,
                              "captured media_urls[] origin, 2026-09-24 clip objects"};

// Clerk. [T1] 2026-08-25 Burp export, 13 API items. This row names the host; it is
// not an invitation to hand-roll the handshake (AGENTS.md §1).
constexpr HostRule kAuthSuno{"auth.suno.com", Tier::Production, Evidence::Captured, Role::ClerkAuth,
                             "2026-08-25 Burp export, 13 API items"};

// Artwork. [T1] response fields *and* direct requests, 2026-09-24. Two hosts, one
// role, one rule: the pair is not interchangeable with a wildcard, and neither is a
// reason to fetch artwork without the captured-host check.
constexpr HostRule kCdn1{"cdn1.suno.ai", Tier::Production, Evidence::Captured, Role::ArtworkOrigin,
                         "captured image asset host, 2026-09-24 Burp corpus"};
constexpr HostRule kCdn2{"cdn2.suno.ai", Tier::Production, Evidence::Captured, Role::ArtworkOrigin,
                         "captured image asset host, 2026-09-24 Burp corpus"};

// Captured progressive-media distribution. [T1] response values in captured clip
// objects. Note the inventory keeps the *constructed* paths on this host at [LEAD]:
// the host is captured, individual constructed paths are not.
constexpr HostRule kCloudFront{"d2lwuy8qc234o3.cloudfront.net", Tier::Production,
                               Evidence::Captured, Role::CloudFront,
                               "captured progressive-media host, clip objects"};

// Known only as a `stream_url` **response value** returned by
// `GET /api/realtime/discover`. Zero requests were ever sent to it by any capture,
// so it is a response value and NOT a captured request host.
//
// Graded `ResponseValue`, which is its own grade precisely so that
// `mayReceiveCredential` refuses it in code. An earlier revision graded this
// `Registered` and relied on the note below to stop a caller mailing the Suno
// bearer to Ably's infrastructure — which is a note, not a control, and notes
// get truncated by editors. Do not downgrade this row to `Registered` without
// first capturing a real request to this host.
constexpr HostRule kAbly{"main.realtime.ably.net", Tier::Production,
                         Evidence::ResponseValue, Role::Realtime,
                         "stream_url value from GET /api/realtime/discover; zero requests "
                         "ever sent, so a response value not a captured request host"};

// The primary studio API. [T1] across three independent capture families: the
// 2026-08-25 and 2026-09-24 Burp exports and the 2026-09-23 browser HAR.
constexpr HostRule kStudioProd{"studio-api-prod.suno.com", Tier::Production,
                               Evidence::Captured, Role::StudioApi,
                               "2026-08-25 Burp capture, plus 2026-09-24 Burp and "
                               "2026-09-23 HAR"};

// ── Staging. Captured, which is the surprising part and the reason this tier exists
// as a name rather than a comment. ─────────────────────────────────────────────
// [T1] in the sense this file uses it: the request, the 200, and the body shape are
// all recorded in ~/Documents/suno-recon/reports/DISCLOSURE-staging-api.md,
// re-confirmed 2026-10-04.
//
// Verbatim from that report §3.1: `GET https://studio-api-staging.suno.com/api/session/`
// returns `200` to a completely unauthenticated caller carrying no cookie and no
// `Authorization` header.
//
// Two facts that bound it, and that this row must not lose:
//  1. The SAME path answers 200 anonymously on prod too (report §1). That is a
//     shared code path serving both tiers, not a staging-only defect - so staging
//     being reachable proves nothing about staging being special.
//  2. ONLY `GET /api/session/` was ever called (report §8.1, plus one HEAD that
//     returned 405). No write route was touched, by deliberate choice: probing
//     whether staging enforces authorization on writes would mean issuing writes
//     against a non-production environment, and the report refuses to answer that by
//     guessing. Whether staging shares a database with production is likewise
//     unknown and untested (report §8.2).
// The grade is `Captured` because the capture happened. What was captured is one
// unauthenticated read of one route, and nothing here is a contract for any other
// staging path.
constexpr HostRule kStudioStaging{"studio-api-staging.suno.com", Tier::Staging, Evidence::Captured,
                                  Role::StudioApi,
                                  "DISCLOSURE-staging-api.md 3.1, 2026-10-04: unauthenticated "
                                  "GET /api/session/ returns 200; same path is open on prod "
                                  "(shared code path); only that GET was ever called (8.1)"};

// The Orpheus/Modal surface. [LEAD] - a string in a bundle, never requested by any
// capture, so `Evidence::Lead` and therefore refused as a credential transport at
// every tier, permanently. This row exists so the host has one home and one honest
// grade, and so the failure mode is a named refusal a diagnostics panel can render
// rather than a `[LEAD]` comment somebody eventually wires.
constexpr HostRule kOrpheus{"suno-ai--orpheus-prod-web.modal.run", Tier::Experimental,
                            Evidence::Lead, Role::ExperimentalOrchestrator,
                             "bundle string only, never requested by any capture; refused as "
                             "a credential transport at every tier"};

// suno-uploads.s3.amazonaws.com. The direct multipart upload target, graded
// `[T1]` in ENDPOINT-INVENTORY.md §2.1 on the 2026-09-24 request/response, and
// held as a constant at SunoEndpoints.hpp:84. Added here because
// `SunoAudioUploadService` contacts it with a hardcoded literal today: a
// registry that omits a host the tree actually reaches is not the single owner
// it claims to be, and the next person to adopt this table for uploads would
// find no row and conclude the host was unverified.
constexpr HostRule kUploadTarget{"suno-uploads.s3.amazonaws.com", Tier::Production,
                                 Evidence::Captured, Role::UploadOrigin,
                                 "2026-09-24 capture, temporary direct multipart upload "
                                 "target"};

// suno.com. `Origin`/`Referer` only - it is a header value this client imitates, not a
// request target. [T1] Burp/HAR/recon, and the OAuth final destination.
constexpr HostRule kSunoWeb{"suno.com", Tier::Production, Evidence::Captured, Role::WebApp,
                            "Origin/Referer header value and OAuth final destination"};

// Alphabetical by host, which is a reviewable order *and* an indexable one. The
// compile-time assertions below are what keep it alphabetical.
constexpr std::array<HostRule, 11> kRegistry{
    kAudiopipe,   kAuthSuno, kCdn1,        kCdn2,          kCloudFront,
    kAbly,        kStudioProd, kStudioStaging, kOrpheus,  kUploadTarget,
    kSunoWeb,
};

// Per-role and per-tier views. Members are the named constants above, never a
// repeated literal, so adding a host is a one-line change in exactly one place.
constexpr std::array<HostRule, 1> kMediaOrigins{kAudiopipe};
constexpr std::array<HostRule, 1> kClerkAuthHosts{kAuthSuno};
constexpr std::array<HostRule, 2> kArtworkOrigins{kCdn1, kCdn2};
constexpr std::array<HostRule, 1> kCloudFrontHosts{kCloudFront};
constexpr std::array<HostRule, 1> kWebAppHosts{kSunoWeb};
constexpr std::array<HostRule, 1> kRealtimeHosts{kAbly};
constexpr std::array<HostRule, 1> kUploadOrigins{kUploadTarget};
constexpr std::array<HostRule, 2> kStudioApiHosts{kStudioProd, kStudioStaging};
constexpr std::array<HostRule, 1> kOrchestratorHosts{kOrpheus};

constexpr std::array<HostRule, 9> kProductionHosts{kAudiopipe, kAuthSuno, kCdn1,
                                                   kCdn2,       kCloudFront, kAbly,
                                                   kStudioProd, kUploadTarget, kSunoWeb};
constexpr std::array<HostRule, 1> kStagingHosts{kStudioStaging};
constexpr std::array<HostRule, 1> kExperimentalHosts{kOrpheus};

/// ASCII case-insensitive equality between a registry host and a `QUrl` host.
///
/// Exists as a QString-taking overload rather than one string_view helper because
/// the alternative is `url.host().toStdString()` inside `classifyRefusal`, which
/// deep-converts UTF-16 to UTF-8 and heap-allocates on **every** credential check.
/// `QUrl::host()` itself is a refcount bump, so this keeps the hot predicate
/// allocation-free.
///
/// No trailing-dot handling, and that is deliberate rather than an oversight: a
/// host written `studio-api-prod.suno.com.` is the same origin, so accepting it
/// would be correct-but-widening, while rejecting it falls through to
/// `Refusal::HostUnknown` - the fail-closed answer this file exists to produce.
[[nodiscard]] bool equalsFoldedAscii(const std::string_view ascii, const QString& host) noexcept
{
    if (static_cast<std::size_t>(host.size()) != ascii.size()) {
        return false;
    }
    for (qsizetype i = 0; i < host.size(); ++i) {
        const char16_t left = host.at(i).unicode();
        const auto folded =
            (left >= U'A' && left <= U'Z') ? static_cast<char16_t>(left + 32) : left;
        const auto right =
            static_cast<char16_t>(static_cast<unsigned char>(ascii[static_cast<std::size_t>(i)]));
        if (folded != right) {
            return false;
        }
    }
    return true;
}

/// The same ASCII fold for two string_views. `findRule` takes a string_view, and
/// materialising a QString just to compare it would allocate on every lookup. Kept
/// separate rather than folded into one overload so the hot path above cannot grow
/// an allocation by accident.
[[nodiscard]] bool equalsFoldedAscii(const std::string_view left,
                                     const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        const auto fold = [](const unsigned char c) noexcept {
            return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
        };
        if (fold(a) != fold(b)) {
            return false;
        }
    }
    return true;
}

/// Lookup without the QString round-trip, for `classifyRefusal`.
const HostRule* findRuleForHost(const QString& host) noexcept
{
    for (const auto& row : kRegistry) {
        if (equalsFoldedAscii(row.host, host)) {
            return &row;
        }
    }
    return nullptr;
}

/// Sorted, strictly unique, and every row carries both a grade and a note.
[[nodiscard]] constexpr bool registryIsSortedAndUnique() noexcept
{
    for (std::size_t i = 0; i < kRegistry.size(); ++i) {
        if (kRegistry[i].host.empty() || kRegistry[i].note.empty()) {
            return false;
        }
        if (i > 0 && !(kRegistry[i - 1].host < kRegistry[i].host)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr bool containsIdentical(const std::span<const HostRule> haystack,
                                               const HostRule& needle) noexcept
{
    for (const auto& row : haystack) {
        if (row.host == needle.host && row.tier == needle.tier &&
            row.evidence == needle.evidence && row.role == needle.role &&
            row.note == needle.note) {
            return true;
        }
    }
    return false;
}

/// Every view row exists in the master table, and the views partition it: the
/// per-role sizes sum to the registry size and so do the per-tier sizes. This is
/// what makes "one host, one row" true rather than aspirational.
[[nodiscard]] constexpr bool registryIsConsistent() noexcept
{
    const std::array<std::span<const HostRule>, 9> roleViews{
        kMediaOrigins, kClerkAuthHosts, kArtworkOrigins, kCloudFrontHosts,
        kWebAppHosts,  kRealtimeHosts,  kStudioApiHosts, kOrchestratorHosts,
        kUploadOrigins};
    const std::array<std::span<const HostRule>, 3> tierViews{
        kProductionHosts, kStagingHosts, kExperimentalHosts};

    std::size_t roleTotal = 0;
    for (const auto& view : roleViews) {
        roleTotal += view.size();
        for (const auto& row : view) {
            if (!containsIdentical(kRegistry, row)) {
                return false;
            }
        }
    }
    std::size_t tierTotal = 0;
    for (const auto& view : tierViews) {
        tierTotal += view.size();
        for (const auto& row : view) {
            if (!containsIdentical(kRegistry, row)) {
                return false;
            }
        }
    }
    return roleTotal == kRegistry.size() && tierTotal == kRegistry.size();
}

static_assert(registryIsSortedAndUnique(),
              "kRegistry must stay alphabetical by host, strictly unique, and every row must "
              "carry both an Evidence grade and a note. Tests index this table, and the note is "
              "where an evidence grade explains itself.");
static_assert(registryIsConsistent(),
              "the per-role and per-tier views must partition kRegistry with identical rows. A "
              "host added to kRegistry but not to a view would be unreachable through "
              "hostsForRole/hostsForTier with no diagnostic anywhere.");
static_assert(evidencePermitsCredential(Evidence::Lead) == false,
              "a [LEAD] must never receive a credential (AGENTS.md 1). If this fires the grade "
              "gate was widened, which is a policy decision requiring a capture, not a refactor.");
static_assert(evidencePermitsCredential(Evidence::ResponseValue) == false,
              "a host seen only as a URL inside another host's response must never receive a "
              "credential. No request was ever sent there, so there is no evidence any belongs "
              "there, and the Suno bearer is not ours to hand to third-party realtime "
              "infrastructure. Demoting the gate below this requires a captured request to that "
              "host -- not a refactor, and not a note.");
static_assert(kExperimentalHosts.size() == 1 && !isSelectableTier(Tier::Experimental),
              "Experimental is the one tier with a registry row that TierPolicy may not be "
              "raised to. That is the whole point of the early refusal in "
              "mayReceiveCredential, so the two must not be changed independently.");

/// The user-facing sentence, one per cause. Each names the rule it refuses under,
/// because a caller has to be able to show a user *why* and "blocked" is not an
/// answer. Deliberately not `noexcept`: `std::format` allocates and may throw, and
/// this runs only on the diagnostic path.
[[nodiscard]] std::string refusalSentence(const QUrl& url, const Refusal refusal,
                                          const Tier activeTier)
{
    const std::string shown = url.toString().toStdString();
    const std::string host = url.host().toStdString();
    const HostRule* rule = findRule(host);
    const char* grade = rule != nullptr ? toString(rule->evidence) : "unknown";
    const char* hostTier = rule != nullptr ? toString(rule->tier) : "unknown";
    const std::string_view note = rule != nullptr ? rule->note : std::string_view{};

    switch (refusal) {
        case Refusal::InvalidUrl:
            return std::format("Refused \"{}\": not a valid absolute URL, and a captured-host "
                               "allowlist cannot be evaluated against a relative or malformed "
                               "target.",
                               shown);
        case Refusal::SchemeNotHttps:
            return std::format("Refused \"{}\": scheme is \"{}\", not https. A credential sent "
                               "over anything else is a credential sent in the clear.",
                               shown, url.scheme().toStdString());
        case Refusal::NonDefaultPort:
            return std::format("Refused \"{}\": port {} is neither the default port nor 443. A "
                               "non-default port is a different origin, and no capture ever "
                               "observed a credential sent there.",
                               shown, static_cast<int>(url.port()));
        case Refusal::UserInfoPresent:
            return std::format("Refused \"{}\": the URL carries userinfo. Captured Suno URLs "
                               "never do, and userinfo in an absolute URL is the classic way to "
                               "make a host look like something it is not.",
                               shown);
        case Refusal::HostUnknown:
            return std::format("Refused \"{}\": host \"{}\" is not in the captured-host registry. "
                               "AGENTS.md 1 fails closed on any host absent from a captured "
                               "allowlist - add it only alongside the capture that names it.",
                               shown, host);
        case Refusal::ExperimentalRefused:
            // Distinct from a tier mismatch on purpose: this is permanent, and no amount of
            // opting in changes it. A user staring at "tier not permitted" would reasonably
            // enable staging and then conclude the client is broken.
            return std::format("Refused \"{}\": host \"{}\" is an experimental surface graded "
                               "\"{}\" and is never permitted to receive a credential, at any "
                               "tier. It appears only as a string in a shipped bundle and was "
                               "never requested by any capture, so there is no evidence for a "
                               "session there.",
                               shown, host, grade);
        case Refusal::TierNotPermitted:
            return std::format("Refused \"{}\": host \"{}\" is a {} host and the active tier is "
                               "{}. Tier matching is exact, so enabling staging narrows the "
                               "reachable host set to staging rather than adding to production.",
                               shown, host, hostTier, toString(activeTier));
        case Refusal::EvidenceTooWeak:
            return std::format("Refused \"{}\": host \"{}\" is graded \"{}\" - {}. A lead is a "
                               "string in a bundle, never a captured request, and AGENTS.md 1 "
                               "forbids wiring it.",
                               shown, host, grade, note);
    }
    return std::format("Refused \"{}\": no policy detail available for this refusal.", shown);
}

} // namespace

std::span<const HostRule> hostRegistry() noexcept
{
    return std::span<const HostRule>(kRegistry);
}

std::span<const HostRule> hostsForRole(const Role role) noexcept
{
    switch (role) {
        case Role::StudioApi: return std::span<const HostRule>(kStudioApiHosts);
        case Role::ClerkAuth: return std::span<const HostRule>(kClerkAuthHosts);
        case Role::MediaOrigin: return std::span<const HostRule>(kMediaOrigins);
        case Role::ArtworkOrigin: return std::span<const HostRule>(kArtworkOrigins);
        case Role::CloudFront: return std::span<const HostRule>(kCloudFrontHosts);
        case Role::WebApp: return std::span<const HostRule>(kWebAppHosts);
        case Role::Realtime: return std::span<const HostRule>(kRealtimeHosts);
        case Role::UploadOrigin: return std::span<const HostRule>(kUploadOrigins);
        case Role::ExperimentalOrchestrator:
            return std::span<const HostRule>(kOrchestratorHosts);
    }
    return {};
}

std::span<const HostRule> hostsForTier(const Tier tier) noexcept
{
    switch (tier) {
        case Tier::Production: return std::span<const HostRule>(kProductionHosts);
        case Tier::Staging: return std::span<const HostRule>(kStagingHosts);
        case Tier::Experimental: return std::span<const HostRule>(kExperimentalHosts);
    }
    return {};
}

const HostRule* findRule(const std::string_view host) noexcept
{
    for (const auto& row : kRegistry) {
        if (equalsFoldedAscii(row.host, host)) {
            return &row;
        }
    }
    return nullptr;
}

std::optional<Refusal> classifyRefusal(const QUrl& url, const Tier activeTier) noexcept
{
    if (!url.isValid() || url.isRelative()) {
        return Refusal::InvalidUrl;
    }
    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        return Refusal::SchemeNotHttps;
    }
    // QUrl::port() returns qint16 and yields -1 for the default port, so this is the
    // same idiom the tree already uses at AuthHeaders.cpp:55.
    if (!(url.port() < 0 || url.port() == 443)) {
        return Refusal::NonDefaultPort;
    }
    if (!url.userInfo().isEmpty()) {
        return Refusal::UserInfoPresent;
    }

    const HostRule* rule = findRuleForHost(url.host());
    if (rule == nullptr) {
        return Refusal::HostUnknown;
    }

    // Experimental is refused here, before the tier comparison, and permanently.
    // This is not an oversight and not a missing branch: no tier, including a future
    // one, permits a credential to reach a host known only from a bundle string. It
    // gets its own Refusal so a diagnostics panel says "never" rather than "not at
    // this tier", which would invite a user to enable staging and then conclude the
    // client is broken.
    if (rule->tier == Tier::Experimental) {
        return Refusal::ExperimentalRefused;
    }

    // Exact match, deliberately - see the file header. A Production host is
    // reachable only while the active tier is Production, and a Staging host only
    // while it is Staging.
    if (rule->tier != activeTier) {
        return Refusal::TierNotPermitted;
    }

    if (!evidencePermitsCredential(rule->evidence)) {
        return Refusal::EvidenceTooWeak;
    }
    return std::nullopt;
}

bool mayReceiveCredential(const QUrl& url, const Tier activeTier) noexcept
{
    return !classifyRefusal(url, activeTier).has_value();
}

TierPolicy::TierPolicy() noexcept
    : activeTier_(Tier::Production)
{
}

bool TierPolicy::raiseTo(const Tier requested) noexcept
{
    if (!isSelectableTier(requested)) {
        return false;
    }
    // A tier the registry has no host for could only ever refuse everything, since
    // matching is exact. Refusing here is the honest answer, rather than reporting
    // success and leaving the client able to talk to nothing.
    if (hostsForTier(requested).empty()) {
        return false;
    }
    activeTier_ = requested;
    return true;
}

void TierPolicy::resetToProduction() noexcept
{
    activeTier_ = Tier::Production;
}

QString TierPolicy::describeRefusal(const QUrl& url, const Tier activeTier) const
{
    const std::optional<Refusal> refusal = classifyRefusal(url, activeTier);
    if (!refusal.has_value()) {
        return {};
    }
    return QString::fromStdString(refusalSentence(url, *refusal, activeTier));
}

} // namespace vc::suno::hosts