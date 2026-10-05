#include "FeatureFlags.hpp"

#include <QJsonValue>

#include <format>
#include <utility>

namespace vc::suno {

namespace {

// ── Precondition on the enum order this file does not own ───────────────────
// The resolution order compares evidence grades against a floor, and `enum class`
// has no `<`, so the comparison is on `static_cast<int>`. That is only correct if
// `Captured` is the lowest-valued enumerator, i.e. strictly the strongest grade.
//
// This was written against a stub with three grades and then compiled against the
// real four-grade enum (`Captured`, `Registered`, `ResponseValue`, `Lead`), which
// is why the check asserts a *property* rather than transcribing a literal list: a
// transcription would have needed editing every time a grade was added, and the
// thing that must not change is the ranking, not the roster. `ResponseValue`
// landing between `Registered` and `Lead` is correct on its own terms — a host
// seen only inside another host's response is stronger evidence than a bundle
// string and weaker than a 401 that demonstrably carried a credential — and it
// does not disturb the only comparison this file makes.
static_assert(static_cast<int>(hosts::Evidence::Captured) == 0,
              "vc::suno::hosts::Evidence must keep Captured at 0: GateResolver "
              "orders surfaces by comparing static_cast<int> of a grade against "
              "the Captured floor, so Captured is required to be the "
              "lowest-valued, strongest enumerator. If a new grade was inserted "
              "ahead of it, every [LEAD] surface would silently pass the floor "
              "and become Available. Fix the ranking there, deliberately, not "
              "this check.");
static_assert(static_cast<int>(hosts::Tier::Production) == 0 &&
                      static_cast<int>(hosts::Tier::Staging) == 1 &&
                      static_cast<int>(hosts::Tier::Experimental) == 2,
              "vc::suno::hosts::Tier must be declared weakest-value-first "
              "(Production, Staging, Experimental). The tier check compares "
              "integers; a reordered enum would gate the wrong surfaces.");

[[nodiscard]] constexpr int rank(const hosts::Evidence evidence) noexcept {
    return static_cast<int>(evidence);
}

/// Stronger evidence has a lower number, so "at least as evidenced as the floor"
/// is `<=`. `Captured` is the only floor any gate in this catalog is held to.
[[nodiscard]] constexpr bool meetsFloor(const hosts::Evidence actual,
                                        const hosts::Evidence floor) noexcept {
    return rank(actual) <= rank(floor);
}

/// A grade as a reader would say it. Named locally rather than via a
/// `toString(hosts::Evidence)`: this file must not add a second owner for a
/// grade vocabulary that `CapturedHosts` may well name itself.
[[nodiscard]] const char* evidenceGrade(const hosts::Evidence evidence) noexcept {
    switch (evidence) {
        case hosts::Evidence::Captured:
            return "[T1] directly captured";
        case hosts::Evidence::Registered:
            return "registered (existence proven, contract uncaptured)";
        case hosts::Evidence::ResponseValue:
            return "a response value only (the host appears inside another host's "
                   "response and was never requested by us)";
        case hosts::Evidence::Lead:
            return "[LEAD] (bundle string or reconstructed path, not a capture)";
    }
    return "ungraded";
}
/// A tier as a reader would say it. Local, for the same reason `evidenceGrade`
/// is: this file must not become a second owner of a vocabulary `CapturedHosts`
/// may name itself.
[[nodiscard]] const char* tierName(const hosts::Tier tier) noexcept {
    switch (tier) {
        case hosts::Tier::Production:
            return "production";
        case hosts::Tier::Staging:
            return "staging";
        case hosts::Tier::Experimental:
            return "experimental";
    }
    return "unknown";
}

/// The sentence for an exclusion. Carries the decision, not the consequence:
/// a user reading "this client will not cancel your subscription" learns
/// something, and a future maintainer reading the `MoneyMoving` branch learns
/// why reopening it is a product conversation and not a missing endpoint.
[[nodiscard]] std::string exclusionSentence(const ExclusionReason reason) {
    switch (reason) {
        case ExclusionReason::MoneyMoving:
            return "Excluded by product policy: a third-party client must not be "
                   "able to cancel a subscription, change a plan, or apply a "
                   "coupon. That is a support and terms-of-service surface, and "
                   "the decision holds even if a capture later lands.";
        case ExclusionReason::LegalConsentWrite:
            return "Excluded by product policy: this route writes a privacy or "
                   "programme consent record on the user's behalf, which is a "
                   "legal-terms surface rather than a feature. Reading the "
                   "existing record is a different act and is not excluded.";
        case ExclusionReason::None:
            break;
    }
    return "Excluded.";
}

/// Split `FeatureDefinition::serverFlags` into views. The views point into the
/// catalog's static storage, so no `std::string` is allocated per probe.
[[nodiscard]] std::vector<std::string_view> splitFlagList(const std::string_view csv) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? csv.size() : comma;
        std::string_view token = csv.substr(start, end - start);
        // Tolerate "a, b" and "a,b" alike. The catalog is written by hand, and a
        // leading space that silently became part of a flag name would turn a
        // ServerGated verdict into an unexplainable one.
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
            token.remove_prefix(1);
        }
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
            token.remove_suffix(1);
        }
        if (!token.empty()) {
            out.push_back(token);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

// ── The catalog ─────────────────────────────────────────────────────────────
// Every `evidence` value is the grade of the surface as a whole, which is the
// *weakest* grade among the routes behind it. A `[T1]` status read sitting next
// to an uncaptured submission route makes the surface a reader, not a
// capability, and saying so in `note` is the difference between a catalog and a
// wish list. Sources: `docs/suno_api/ENDPOINT-INVENTORY.md` §4 (the sole
// master), `src/suno/SunoEndpoints.hpp` (the implementation mirror, never the
// evidence), and the 2026-09-28 recon as recorded in the backlog.
constexpr std::array<FeatureDefinition, kFeatureGateCount> kCatalog{{
        {FeatureGate::Library, "Library", "library", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "POST /api/feed/v3 is [T1] with a captured cursor contract, and no observed "
         "flag gates the feed itself. `library-filters-v2` governs only the filter "
         "bar, so naming it here would report the entire library as ServerGated "
         "whenever that one sub-control is off. Server-side search is a [LEAD] "
         "(`searchText` was never captured); local filtering is the shipped design, "
         "not a gap."},
        {FeatureGate::Explore, "Explore", "discover", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "POST /api/unified/explore is [T1] (2026-09-24). No flag in either "
         "observed map mentions explore, and `agentic-simple` is an agentic-chat "
         "flag rather than a discovery flag -- the name similarity is a trap, not a "
         "wiring hint."},
        {FeatureGate::Notifications, "Notifications", "account", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "The three /api/notification/v2 routes are [T1] and wired -- and have zero "
         "hits across the recon's 95 chunks. That is a static negative, not proof of "
         "removal, so the surface should be re-probed rather than trusted: being "
         "implemented is not the same as being corroborated. `rate-limit-notifications` "
         "is about throttling notices, not about the surface existing."},
        {FeatureGate::Playlists, "Playlists", "library", hosts::Evidence::Captured,
         "playlists,playlist-condition", hosts::Tier::Production, ExclusionReason::None,
         "GET /api/playlist/me is [T1]; no playlist mutation is captured at all, so "
         "this is a viewer gate and must not be read as playlist management. "
         "`playlist-condition` is account-gated -- one of the ten flags an anonymous "
         "read never sees -- and matches PlanFeature.PLAYLIST_CONDITION, which makes "
         "it the flag that actually decides."},
        {FeatureGate::Projects, "Projects", "projects", hosts::Evidence::Captured,
         "create-projects", hosts::Tier::Production, ExclusionReason::None,
         "Project READS are [T1] (/project/me, /project/default, /project/{id}, "
         "pinned-clips). Project MUTATIONS are [REGISTERED] only: the 2026-09-28 "
         "recon saw create-project, save-project, collaborators, invite and a dozen "
         "more in the client bundle without probing one, so no save or collaboration "
         "contract may be wired on existence alone. Collaborative workspaces "
         "(`collab-workspaces`) is staging-only and is not a sub-gate here."},
        {FeatureGate::Generation, "Generation", "generation", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "POST /api/generate/v2-web/ and POST /api/c/check are [T1], so generation "
         "is NOT evidence-blocked. What blocks it is an undecided captcha-token flow "
         "and an uncaptured durable-processing contract -- which is exactly why it "
         "belongs behind the client switch instead of a hardcoded false. No flag in "
         "either observed map expresses generation availability; the server-selected "
         "route arrives as configs[\"gen-endpoint\"] on an authenticated read."},
        {FeatureGate::Upload, "Audio upload", "generation", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "initialize -> direct multipart -> upload-finish is [T1] end to end "
         "(2026-09-24) and is the only implemented transport sequence in the "
         "inventory. The fourth leg -- initialize-clip, which links an upload to a "
         "clip and is what would make this a 'make a song from this file' feature -- "
         "is [REGISTERED] only, and status, limits and validation are uncaptured. An "
         "upload currently completes and goes nowhere useful."},
        {FeatureGate::CustomModels, "Custom models", "account", hosts::Evidence::Captured,
         "custom-model-ui,custom-model-v6-cutover", hosts::Tier::Production, ExclusionReason::None,
         "Only GET /api/custom-model/pending/ is [T1]. No training, archive, plan "
         "matrix or voice-clone route is captured, so this is a state READ surface, "
         "not a custom-model builder. `custom-model-ui` is the cleanest proof that "
         "the flag map must be parsed rather than baked: staging-only in the "
         "2026-10-04 anonymous snapshot, already present in an authenticated prod "
         "read on 2026-09-30. `custom-model-v6-cutover` names the model path, not "
         "the UI, so both are required."},
        {FeatureGate::Contests, "Contests", "community", hosts::Evidence::Captured, "contest-hub",
         hosts::Tier::Production, ExclusionReason::None,
         "GET /api/contests/ is [T1]. `remix-contest-disable-downloads` is "
         "simultaneously present in the map and means contest clips are "
         "download-restricted, so this gate is a catalog entry -- never a licence to "
         "fetch a contest clip."},
        {FeatureGate::Personas, "Personas (voices)", "account", hosts::Evidence::Lead, "voices-ui",
         hosts::Tier::Production, ExclusionReason::None,
         "NO persona or voice route is captured: ENDPOINT-INVENTORY section 4.7 "
         "says so explicitly, and the nearest [T1] route (/api/lyricists) belongs to "
         "the lyrics area rather than to Voices. The account-side facts are captured "
         "-- the voices-ui flag, and access_group_attrs.is_voices_early_access on "
         "/api/user/metadata -- but a flag is not a route. Graded Lead, therefore "
         "EvidenceBlocked, until a Voices route is captured. `voices-ui-verify` is "
         "the verification step, not this surface's gate, so it is not listed."},
        {FeatureGate::Styles, "Styles and prompts", "generation", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "GET/POST /api/prompts/v2 is [T1] for prompt and style data. The style "
         "CONTROLS are flag-shaped and the flags are captured -- `negative-tags`, "
         "`max-mode`, `aug-creativity`, `control-sliders` -- but they gate "
         "individual controls rather than the surface, so the gate itself is not "
         "flag-gated and the controls are read per-flag."},
        {FeatureGate::LyricsCowrite, "Lyrics cowrite", "generation", hosts::Evidence::Captured,
         "ft-lyrics-model", hosts::Tier::Production, ExclusionReason::None,
         "POST /api/generate/cowrite-lyrics/ and the /api/lyrics-projects routes are "
         "[T1], though the inventory explicitly declines to canonicalise the "
         "lyrics-project mutation schema -- so a create is a parse risk rather than "
         "a contract. `ft-lyrics-model` gates the fine-tuned lyrics model "
         "specifically, not co-writing in general."},
        {FeatureGate::VideoGeneration, "Video generation", "generation", hosts::Evidence::Captured,
         "video-to-song", hosts::Tier::Production, ExclusionReason::None,
         "GET /api/video/generate/{id}/status/ and POST /api/video_gen/pending_batches "
         "are [T1] -- a STATUS reader, not a submission route. Whether a client can "
         "request video at all is uncaptured, which is why the gate keys on "
         "`video-to-song` rather than claiming the capability. ClipParser also stores "
         "video_url raw with no origin check, so playback of a video host is a "
         "separate open question."},
        {FeatureGate::Stems, "Stems", "generation", hosts::Evidence::Captured,
         "editing-stems,generative-stems,stem-dryer,crop-remove", hosts::Tier::Production,
         ExclusionReason::None,
         "All four flags sit in the anonymous map, so they carry no account "
         "information -- they are on or off for everybody. The local half is also "
         "empty: SunoClient has no stems method and SunoWorkspace::requestStems is a "
         "log line, so nothing here can be called whatever the verdict says. Catalogued "
         "because these are the four most product-shaped names in the map, and "
         "because a flag list that ignores them would look arbitrarily incomplete."},
        {FeatureGate::Sharing, "Sharing", "community", hosts::Evidence::Captured,
         "realtime-share-asset-status", hosts::Tier::Production, ExclusionReason::None,
         "GET /api/share/stats is [T1] but has zero hits across the recon's 95 "
         "chunks; drifted spelling is the likely explanation rather than deletion, so "
         "re-probe before demoting it. Creating a share link is not captured at all "
         "(section 4.8), so this gate covers a statistics read only. "
         "`enable-sharelist-and-share-notifications` is staging-only."},
        {FeatureGate::RealtimePush, "Realtime push", "library", hosts::Evidence::ResponseValue, "",
         hosts::Tier::Production, ExclusionReason::None,
         "GET /api/realtime/discover is [T1] and its body is modelled exactly "
         "(stream_url, embedded_token credential, x-ably-token header param). But "
         "the corpus sent ZERO requests to main.realtime.ably.net: that host is a "
         "value inside another host's response, which is what "
         "hosts::Evidence::ResponseValue exists to name -- a grade weaker than a "
         "401-proven host and one no credential may be sent to. A push surface "
         "needs a host we have actually spoken to, so the gate is graded on the "
         "host rather than on the route. Do not send the bearer there until an SSE "
         "capture lands."},
        {FeatureGate::ExperimentalOrchestrator, "Experimental orchestrator", "labs",
         hosts::Evidence::Lead, "agentic-simple", hosts::Tier::Production, ExclusionReason::None,
         "MODAL_BASE is [LEAD] (SunoEndpoints.hpp:43) and no reviewed capture ever "
         "requested that host. ORCHESTRATOR_CHAT and ORCHESTRATOR_HISTORY are [LEAD], "
         "and the recon's only path from that host is /session-history -- which is not "
         "what the client uses, so those two constants are not even the right shape. "
         "`agentic-simple` suggests agentic chat exists; a flag is not a contract. The "
         "/b-side staff routes are deliberately excluded from this catalog: 77 of them "
         "are compiled into the public bundle, including impersonate, and disclosure "
         "material is not a surface to build on."},
        {FeatureGate::Billing, "Billing", "account", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "The READ-ONLY billing surface is [T1] (/billing/info/, usage-plans, plan "
         "descriptions, the comparison table, eligible discounts). No billing "
         "mutation, checkout, portal, coupon, payment-method or survey route is "
         "captured -- and the money-moving ones we know of exist are [REGISTERED] only "
         "and are deliberately refused. See BillingMutations; this gate is the reads, "
         "and must not be dragged down by them."},
        {FeatureGate::AccountDeletion, "Account deletion", "account", hosts::Evidence::Lead,
         "can-delete-account", hosts::Tier::Staging, ExclusionReason::None,
         "No deletion route is captured on either tier, so nothing here can be called "
         "whatever the flags say. It is also the catalog's one genuinely tier-gated "
         "row: production carries `defer-account-deletions` while staging carries "
         "`can-delete-account`, which reads as deletion being un-deferred -- a "
         "compliance-adjacent code path that wants a human decision before it is even "
         "designed, and not a switch."},

        // ── Added rows: inventory areas with no honest home above ───────────────
        {FeatureGate::AccountProfile, "Account and profile", "account", hosts::Evidence::Captured,
         "", hosts::Tier::Production, ExclusionReason::None,
         "The session/account plane: /api/session/ itself, /api/user/metadata, "
         "/api/user/get_user_session_id/, /api/user/user_config/, "
         "/api/user/tos_acceptance and both /api/onboarding routes are [T1]. "
         "statsig_custom_properties.custom.{user_plan_key,us_state} is the only "
         "observed carrier of plan and US state for a signed-in account (roles is {} "
         "anonymously, 8 keys authenticated). None of it is entitlement: 47 server "
         "flags, 28 Statsig gates, 60 ParameterStore params and 22 PlanFeature "
         "entitlements are separate planes and the server enforces them "
         "independently, so this is visibility, not access. /api/personalization/"
         "memory and /settings are [T1] and belong to this plane rather than to "
         "Personas, which is blocked on absent evidence -- folding a captured route "
         "into a blocked gate would understate it."},
        {FeatureGate::ClipRelations, "Clip relations", "library", hosts::Evidence::Captured,
         "remix", hosts::Tier::Production, ExclusionReason::None,
         "parent, remixes, remixes/count, get_similar and /gen/{id}/comments are all "
         "[T1]. Four of them have zero hits across the 95-chunk recon and the live "
         "client calls drifted spellings instead (/clips/{clip_id}/toggle_remixes/, "
         "/gen/{id}/comments) -- drifted spelling, not deletion, so re-probe before "
         "demoting any of them. `clip-parent-populates-remix-sidebar` is staging-only. "
         "Nothing here is wired: no like, trash, share or remix verb is captured, and "
         "the DB already persists is_liked/is_trashed/is_public with no verbs to set "
         "them."},
        {FeatureGate::Rights, "Rights and appeals", "library", hosts::Evidence::Captured,
         "enable-trust-safety-appeals", hosts::Tier::Production, ExclusionReason::None,
         "POST /api/mango/rights is [T1] and returns {key, iv, glt}; the WEB client "
         "derives a per-user key (SHA-256 of the JWT, then AES-GCM, then AES-CTR) and "
         "decrypts in a service worker at /_sw-mango. That is obfuscation for a "
         "cooperating web page, not a DRM this client may reproduce, so the honest "
         "scope is the [T1] reads -- /api/clips/{id}/attribution and the rights "
         "surface -- and explicitly NOT decryption. "
         "`enable-trust-safety-appeals` is the appeals surface, not the audio path."},
        {FeatureGate::MediaDelivery, "Media delivery", "media", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "The product's core, and the one gate with no flag at all: a captured host is "
         "a prerequisite, not a privilege. Media hosts are [T1] -- audiopipe.suno.ai, "
         "cdn1/cdn2.suno.ai, d2lwuy8qc234o3.cloudfront.net -- while the constructed "
         "CDN paths at SunoEndpoints.hpp:193-194 are [LEAD] and must not be used. "
         "selectDownloadUrl still accepts only content_type == \"mp3\", which leaves "
         "the captured m4a-opus/progressive item unplayable; that is an evidence "
         "question with a one-GET capture behind it, not a gate decision. "
         "cdn-o.suno.com is excluded by name and must never be wired."},
        {FeatureGate::AppChrome, "App chrome", "app", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "/api/modals and both /api/cms/nudges routes are [T1], as is /api/labs/configs "
         "-- but every probed /labs path is 404 AND noindex and absent from all 4,575 "
         "sitemap URLs, so a labs gate is not a product surface and is not catalogued "
         "as one. Server-driven chrome is data this client may render, at most; it is "
         "not a feature it owns."},
        {FeatureGate::MusicPlayer, "Music player sync", "player", hosts::Evidence::Captured, "",
         hosts::Tier::Production, ExclusionReason::None,
         "GET/POST /api/music_player/playbar_state is [T1] -- cross-device playbar "
         "state sync. No product value for a local renderer, catalogued so the "
         "captured surface is accounted for rather than silently missing from the "
         "table. If it is ever wanted it is one read plus one sync write and nothing "
         "else; it is emphatically not remote playback control."},
        {FeatureGate::BillingMutations, "Billing mutations", "account", hosts::Evidence::Lead, "",
         hosts::Tier::Production, ExclusionReason::MoneyMoving,
         "cancel-sub, cancel-sub/undo, change-plan, change-plan/preview, pause-sub, "
         "unpause-sub, create-session, accept-sub-coupon and "
         "billing/auto-reload/enable|disable are all [REGISTERED] from the recon -- "
         "existence only, no captured contract, none of them wired. The grade would "
         "block them anyway. What binds is the exclusion: a third-party client that "
         "can cancel a subscription or apply a coupon is a support and "
         "terms-of-service surface, and that judgement is not undone by a capture."},
        {FeatureGate::PrivacyConsentWrites, "Privacy and consent writes", "account",
         hosts::Evidence::Lead, "", hosts::Tier::Production, ExclusionReason::LegalConsentWrite,
         "PUT /api/privacy/data-sharing-consent and POST /api/user/vip_program_acceptance "
         "are [REGISTERED] only. Excluded regardless of grade: these write a consent "
         "record on the user's behalf, and a desktop client that records privacy or "
         "VIP-programme acceptance is a legal-terms problem with real consequences, "
         "not a missing feature. Reading data_sharing_consent out of the session "
         "envelope is a different act and is preserved verbatim by the parser; "
         "writing one is not ours."},
}};

/// Every gate has exactly one row, and the rows are in gate order. Both halves
/// matter: the first means no gate can quietly resolve to nothing, the second is
/// what makes `evaluateAll()`'s ordering deterministic without a sort.
consteval bool catalogIsCompleteAndOrdered() {
    if (kCatalog.size() != kFeatureGateCount) {
        return false;
    }
    for (std::size_t i = 0; i < kCatalog.size(); ++i) {
        if (static_cast<std::size_t>(kCatalog[i].gate) != i) {
            return false;
        }
    }
    return true;
}
static_assert(catalogIsCompleteAndOrdered(),
              "featureCatalog() must hold exactly one row per FeatureGate, in "
              "FeatureGate order. Either a new enumerator has no row (that gate "
              "would resolve to nothing) or a row is out of order (evaluateAll "
              "would stop being deterministic). Fix the catalog, not this check.");

/// Where a key was found. `Absent` and `NotAnObject` need different answers --
/// the first is an account with no flags, the second is a mis-parse worth saying
/// out loud -- so they are not collapsed.
enum class KeyShape { Absent, NotAnObject, Object };

/// Nested-tolerant lookup for one key: flat, then under `session`, then under
/// `data`. This mirrors `SunoAccountManager.cpp:45-75` (parseUser) exactly rather
/// than than inventing a third nesting policy, because a session envelope that
/// is flat for `user` but wrapped for `flags` is precisely the case one shared
/// walk handles and two bespoke ones do not.
[[nodiscard]] KeyShape pickNested(const QJsonObject& root, const QString& key, QJsonObject& out) {
    const QJsonObject scopes[]{
            root,
            root.value(QStringLiteral("session")).toObject(),
            root.value(QStringLiteral("data")).toObject(),
    };
    for (const QJsonObject& scope : scopes) {
        const QJsonValue value = scope.value(key);
        if (value.isObject()) {
            out = value.toObject();
            return KeyShape::Object;
        }
        // A JSON null is "present but empty", which is exactly what the observed
        // anonymous envelope carries for `configs` and `experiments`. It is not a
        // type error, so it must not stop the search of the outer scopes.
        if (!value.isUndefined() && !value.isNull()) {
            return KeyShape::NotAnObject;
        }
    }
    return KeyShape::Absent;
}

} // namespace

// ── Capability parsing ──────────────────────────────────────────────────────

SessionCapabilities parseSessionCapabilities(const QJsonObject& envelope) {
    SessionCapabilities caps;
    caps.raw = envelope;

    QJsonObject flagsObject;
    switch (pickNested(envelope, QStringLiteral("flags"), flagsObject)) {
        case KeyShape::Object:
            caps.flagMapShape = FlagMapShape::Present;
            break;
        case KeyShape::NotAnObject:
            caps.flagMapShape = FlagMapShape::NotAnObject;
            break;
        case KeyShape::Absent:
            caps.flagMapShape = FlagMapShape::Absent;
            break;
    }
    caps.flagsRaw = flagsObject;

    for (auto it = flagsObject.constBegin(); it != flagsObject.constEnd(); ++it) {
        const std::string name = it.key().toStdString();
        const QJsonValue value = it.value();
        if (!value.isBool()) {
            // Recorded rather than dropped, per the tolerance rule. A non-bool
            // is not an explicit `false`, so it lands in `flags` (enabled) and
            // in `flagsNonBool` (diagnosed) -- see the header for why that is
            // the right side of the line.
            caps.flags.insert(name);
            caps.flagsNonBool.insert(name);
            continue;
        }
        (value.toBool() ? caps.flags : caps.flagsFalse).insert(name);
    }

    // Everything else is preserved verbatim and never interpreted. `roles` is
    // `{}` anonymously and populated when authenticated; `statsigCustom` is the
    // only observed carrier of plan and US state; `dataSharingConsent` was
    // never characterised by any capture, which is a reason to preserve it
    // untouched rather than a reason to parse it.
    //
    // The KeyShape is deliberately discarded on all four. "Preserve verbatim"
    // and "record that this key had the wrong type" are different contracts, and
    // for these four the honest verbatim answer to a wrong-typed key is an empty
    // object -- which is what `pickNested` leaves behind. `flags` is the one key
    // whose shape changes the meaning of what we know (absent vs unparseable),
    // so it is the one key whose shape is kept.
    const auto discardShape = [](KeyShape) {};
    discardShape(pickNested(envelope, QStringLiteral("roles"), caps.roles));
    discardShape(
            pickNested(envelope, QStringLiteral("statsig_custom_properties"), caps.statsigCustom));
    discardShape(pickNested(envelope, QStringLiteral("experiments"), caps.experiments));
    discardShape(
            pickNested(envelope, QStringLiteral("data_sharing_consent"), caps.dataSharingConsent));

    // configs.gen-endpoint is the server-selected generate route. `configs` is
    // null on every anonymous read and populates only when authenticated, so the
    // presence of this one value is itself a signal -- and its absence must not
    // be guessed into a default route, which is how a constructed path gets
    // wired by accident.
    QJsonObject configs;
    if (pickNested(envelope, QStringLiteral("configs"), configs) == KeyShape::Object) {
        const QJsonValue endpoint = configs.value(QStringLiteral("gen-endpoint"));
        if (endpoint.isString()) {
            std::string selected = endpoint.toString().toStdString();
            if (!selected.empty()) {
                caps.genEndpoint = std::move(selected);
            }
        }
    }

    return caps;
}

// ── Catalog access ──────────────────────────────────────────────────────────

std::span<const FeatureDefinition> featureCatalog() noexcept {
    return std::span<const FeatureDefinition>(kCatalog.data(), kCatalog.size());
}

const FeatureDefinition* findFeature(const FeatureGate gate) noexcept {
    const auto index = static_cast<std::size_t>(gate);
    if (index >= kCatalog.size()) {
        return nullptr;
    }
    // The index is the gate's position, which the static_assert above pinned to
    // the catalog's ordering. No search, and no way for the two to disagree.
    return &kCatalog[index];
}

// ── Resolver ────────────────────────────────────────────────────────────────

void GateResolver::setCapabilities(SessionCapabilities caps) { caps_ = std::move(caps); }

void GateResolver::setTierPolicy(const hosts::TierPolicy& tier) { tier_ = tier; }

bool GateResolver::switchMayHold(const FeatureGate gate) const {
    const FeatureDefinition* def = findFeature(gate);
    if (def == nullptr) {
        return false;
    }
    // Deliberately does NOT consult the tier or the server flags. The switch is
    // this client's own intent -- "we would use this if the server let us" --
    // and must be settable while the verdict is ServerGated. Reading the tier
    // here would make the switch a mirror of the verdict, and then it would
    // carry no information the verdict does not already carry.
    if (def->exclusion != ExclusionReason::None) {
        return false;
    }
    return meetsFloor(def->evidence, hosts::Evidence::Captured);
}

bool GateResolver::setLocallyEnabled(const FeatureGate gate, const bool enabled) {
    if (enabled && !switchMayHold(gate)) {
        // No state change. Storing `true` for an evidence-blocked or excluded
        // gate would put two contradictory answers in one object, and something
        // downstream would eventually believe the wrong one.
        return false;
    }
    const auto index = static_cast<std::size_t>(gate);
    if (index >= locallyEnabled_.size()) {
        return false;
    }
    locallyEnabled_[index] = enabled;
    return true;
}

GateVerdict GateResolver::evaluate(const FeatureGate gate) const {
    const FeatureDefinition* def = findFeature(gate);
    if (nullptr == def) {
        // Unreachable for every enumerator in this header: the static_assert
        // above guarantees one row per gate. EvidenceBlocked is the
        // conservative verdict, and the sentence blames the catalog rather than
        // the evidence, because the catalog is what is missing here.
        return {GateStatus::EvidenceBlocked, gate,
                QStringLiteral("No catalog entry exists for this gate.")};
    }

    // 1. Excluded -- a hard-coded product or legal decision, ahead of everything
    //    else. Not even a capture can move it.
    if (def->exclusion != ExclusionReason::None) {
        return {GateStatus::Excluded, gate,
                QString::fromStdString(exclusionSentence(def->exclusion))};
    }

    // 2. Evidence floor. Unconditional, and deliberately so. AGENTS.md section 1
    //    forbids promoting a [LEAD] without a direct human capture and requires
    //    failing closed on unverified hosts; a server flag is the server
    //    asserting that a feature exists, which is a different claim from
    //    showing us its contract, and the two cannot stand in for each other.
    if (!meetsFloor(def->evidence, hosts::Evidence::Captured)) {
        return {GateStatus::EvidenceBlocked, gate,
                QString::fromStdString(std::format(
                        "Blocked on evidence: \"{}\" is {}. No server flag, config, "
                        "tier or client switch can override that (AGENTS.md section 1).",
                        def->title, evidenceGrade(def->evidence)))};
    }

    // 3. Tier. Checked before the flags, because a staging-only surface has no
    //    production flag map to be satisfied by -- reporting "flag absent" would
    //    be true and useless.
    const int activeTier = static_cast<int>(tier_.activeTier());
    if (activeTier < static_cast<int>(def->minTier)) {
        return {GateStatus::ServerGated, gate,
                QString::fromStdString(std::format(
                        "Not on this tier: \"{}\" does not exist below the {} tier, "
                        "and this client is on {}.",
                        def->title, tierName(def->minTier), tierName(tier_.activeTier())))};
    }

    // 4. Server flags. Every named flag must be present and not explicitly
    //    false -- AND, not OR. Distinguishing "absent" from "explicitly false"
    //    matters: one is an account that lacks the feature, the other is a
    //    feature the server switched off for this account.
    const std::vector<std::string_view> required = splitFlagList(def->serverFlags);
    for (const std::string_view flag : required) {
        if (caps_.hasFlag(flag)) {
            continue;
        }
        if (caps_.flagsFalse.contains(flag)) {
            return {GateStatus::ServerGated, gate,
                    QString::fromStdString(
                            std::format("The server has \"{}\" switched off for this account "
                                        "(flag \"{}\" is present and explicitly false).",
                                        def->title, flag))};
        }
        return {GateStatus::ServerGated, gate,
                QString::fromStdString(
                        std::format("Not enabled for this account: \"{}\" needs server flag "
                                    "\"{}\", which is absent from this session's flag map.",
                                    def->title, flag))};
    }

    // 5. The client switch. Off by default, for every gate, with no exception
    //    to remember.
    const auto index = static_cast<std::size_t>(gate);
    if (index >= locallyEnabled_.size() || !locallyEnabled_[index]) {
        return {GateStatus::LocallyDisabled, gate,
                QString::fromStdString(
                        std::format("Available on the server but switched off in this build: "
                                    "\"{}\". One deliberate switch away.",
                                    def->title))};
    }

    // 6. Available.
    if (required.empty()) {
        return {GateStatus::Available, gate,
                QString::fromStdString(std::format(
                        "Available: \"{}\" is switched on and not flag-gated.", def->title))};
    }
    std::string flags;
    for (const std::string_view flag : required) {
        if (!flags.empty()) {
            flags += ", ";
        }
        flags += flag;
    }
    return {GateStatus::Available, gate,
            QString::fromStdString(
                    std::format("Available: \"{}\" is switched on and server flag{} {} present.",
                                def->title, required.size() == 1 ? "" : "s", flags))};
}

std::vector<GateVerdict> GateResolver::evaluateAll() const {
    std::vector<GateVerdict> out;
    out.reserve(featureCatalog().size());
    for (const FeatureDefinition& def : featureCatalog()) {
        out.push_back(evaluate(def.gate));
    }
    return out;
}

} // namespace vc::suno
