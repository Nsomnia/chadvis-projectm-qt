#pragma once
// SunoEndpoints.hpp - Centralized Suno API endpoint map
// One source of truth for every URL we hit.
//
// Evidence sources (secrets redacted; paths are outside this repository):
// - 2026-08-25 Burp captures: ~/Documents/suno-burp-exports/auth.suno.com and
//   ~/Documents/suno-burp-exports/studio-api-prod.suno.com. Sanitized extracts
//   are indexed by ~/Documents/suno-media-station-glm5.2/docs/captures/raw/
//   burp-session-2026-08/README.md.
// - 2026-08-25 full Burp export: ~/Documents/suno-capture-burp.
// - 2026-09-23 browser HAR: ~/Documents/suno-master-utility-browser-extension-
//   kilo/scratchpad/suno.com.har.
// - 2026-09-22 sanitized OAuth recon: docs/suno_api/raw/
//   sanitized-recon-2026-09-22.json.
//
// Evidence tiers:
// - [T1] observed in one of the real captures listed above.
// - [LEAD] recon/research/constructed path only; not present in a real capture.
// Entries are grouped by capture family so provenance stays reviewable.
// Studio-api path constants are relative to API_BASE and MUST NOT start /api.

#include <QString>
#include <string_view>

namespace vc::suno {

/// Convert a std::string_view endpoint constant to QString without the
/// fromUtf8/data()/size() boilerplate repeated at every call site.
inline QString qstr(std::string_view sv) {
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

} // namespace vc::suno

// Top-level block: writing vc::suno::endpoints INSIDE namespace vc::suno
// would create vc::suno::vc::suno::endpoints and leave the outer namespace
// unclosed, poisoning every include that follows.
namespace vc::suno::endpoints {

// ── Base URLs ──────────────────────────────────────────────
// T1 bases: Burp 2026-08-25 / browser HAR 2026-09-23. MODAL_BASE is LEAD.
constexpr std::string_view API_BASE = "https://studio-api-prod.suno.com/api"; // [T1]
constexpr std::string_view MODAL_BASE = "https://suno-ai--orpheus-prod-web.modal.run"; // [LEAD]
constexpr std::string_view CDN_BASE = "https://cdn1.suno.ai"; // [T1]
constexpr std::string_view CDN_CLOUDFRONT_BASE = "https://d2lwuy8qc234o3.cloudfront.net"; // [T1]
constexpr std::string_view WEB_BASE = "https://suno.com"; // [T1]

// NOTE: Clerk auth endpoints/versions live in suno/auth/ClerkAuthClient.hpp
// (AUTH_BASE, LEGACY_BASE, CLERK_API_VERSION, CLERK_JS_VERSION).

// ── Studio API: Generation ─────────────────────────────────
// T1: generation-v2-web.md + cowrite-lyrics.md (Burp 2026-08-25).
constexpr std::string_view GENERATE = "/generate/v2-web/"; // [T1] POST
constexpr std::string_view CAPTCHA_CHECK = "/c/check"; // [T1] POST
constexpr std::string_view COWRITE_LYRICS = "/generate/cowrite-lyrics/"; // [T1] POST

// ── Studio API: Lyrics / analysis ───────────────────────────
// T1: aligned-lyrics-v2.md (Burp 2026-08-25). WAV paths are LEAD.
constexpr std::string_view ALIGNED_LYRICS = "/gen/{}/aligned_lyrics/v2/"; // [T1] GET
constexpr std::string_view DOWNBEATS_STREAMING = "/gen/{}/downbeats_streaming/v2"; // [T1] POST
constexpr std::string_view WAVEFORM_AGGREGATES = "/gen/{}/waveform-aggregates"; // [T1] GET
constexpr std::string_view CONVERT_WAV = "/gen/{}/convert_wav/"; // [LEAD] POST
constexpr std::string_view WAV_FILE = "/gen/{}/wav_file/"; // [LEAD] GET

// ── Studio API: Lyrics projects / prompts ───────────────────
// T1: lyrics-projects-crud.md, styles-prompts-v2.md, cowrite-lyrics.md.
constexpr std::string_view LYRICS_PROJECTS = "/lyrics-projects"; // [T1] GET/POST
constexpr std::string_view LYRICS_PROJECTS_FLUSH = "/lyrics-projects/{}/flush"; // [T1] POST
constexpr std::string_view PROMPTS_V2 = "/prompts/v2"; // [T1] GET/POST
constexpr std::string_view PROMPTS_SUGGESTIONS = "/prompts/suggestions"; // [T1] GET
constexpr std::string_view PROMPTS_UPSAMPLE = "/prompts/upsample"; // [T1] POST
constexpr std::string_view LYRICISTS = "/lyricists"; // [T1] GET

// ── Studio API: Library / Feed ─────────────────────────────
// T1: feed-v3-library-listing.md (Burp 2026-08-25); full Burp export also
// captured POST /api/unified/homepage.
constexpr std::string_view LIBRARY_FEED = "/feed/v3"; // [T1] POST
constexpr std::string_view UNIFIED_FEED = "/unified/feed"; // [T1] POST
constexpr std::string_view UNIFIED_HOMEPAGE = "/unified/homepage"; // [T1] POST
constexpr std::string_view SESSION = "/session/"; // [T1] GET
constexpr std::string_view SESSION_CATALOG = "/session/"; // [T1] alias for SESSION

// ── Studio API: Playlists ───────────────────────────────────
// T1: playlists.md (Burp 2026-08-25).
constexpr std::string_view PLAYLIST_ME = "/playlist/me"; // [T1] GET

// ── Studio API: Billing ────────────────────────────────────
// T1: billing-suite.md (Burp 2026-08-25).
constexpr std::string_view BILLING_INFO = "/billing/info/"; // [T1] GET
constexpr std::string_view BILLING_ELIGIBLE_DISCOUNTS = "/billing/eligible-discounts"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLANS = "/billing/usage-plans"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLAN_COMPARISON = "/billing/usage-plan-web-table-comparison/"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLAN_FAQ = "/billing/usage-plan-faq/"; // [T1] GET
constexpr std::string_view BILLING_USAGE_PLAN_DESCRIPTIONS = "/billing/usage-plan-descriptions/"; // [T1] GET
constexpr std::string_view BILLING_AUTO_RELOAD_NUDGE_CHECK = "/billing/auto-reload/nudge-check"; // [T1] POST
constexpr std::string_view BILLING_CONVERSION_TRACKING = "/billing/conversion-tracking"; // [T1] GET

// ── Studio API: User / Profile / Account ────────────────────
// T1: session-user-config.md, playlists.md (Burp 2026-08-25).
constexpr std::string_view USER_CONFIG = "/user/user_config/"; // [T1] POST
constexpr std::string_view USER_TOS_ACCEPTANCE = "/user/tos_acceptance"; // [T1] GET
constexpr std::string_view USER_GET_SESSION_ID = "/user/get_user_session_id/"; // [T1] GET
constexpr std::string_view USER_METADATA = "/user/metadata"; // [T1] GET
constexpr std::string_view PROFILES_INFO = "/profiles/{}/info"; // [T1] GET
constexpr std::string_view PROFILES_PINNED_CLIPS = "/profiles/pinned-clips"; // [T1] GET

// ── Studio API: Projects ───────────────────────────────────
// T1: feed-v3-library-listing.md + browser HAR 2026-09-23.
constexpr std::string_view PROJECT_DEFAULT = "/project/default"; // [T1] GET
constexpr std::string_view PROJECT_ME = "/project/me"; // [T1] GET
constexpr std::string_view PROJECT_BY_ID = "/project/{}"; // [T1] GET
constexpr std::string_view PROJECT_PINNED_CLIPS = "/project/default/pinned-clips"; // [T1] GET

// ── Studio API: Clips / relations ───────────────────────────
// T1: clips-relations.md + feed-v3-library-listing.md.
constexpr std::string_view CLIP_ATTRIBUTION = "/clips/{}/attribution"; // [T1] GET
constexpr std::string_view CLIP_PARENT = "/clips/parent"; // [T1] GET
constexpr std::string_view CLIP_REMIXES = "/clips/remixes"; // [T1] GET
constexpr std::string_view CLIP_REMIXES_COUNT = "/clips/remixes/count"; // [T1] GET
constexpr std::string_view CLIP_GET_SIMILAR = "/clips/get_similar/"; // [T1] GET
constexpr std::string_view CLIP_GET_SONGS_BY_IDS = "/clips/get_songs_by_ids"; // [T1] GET
constexpr std::string_view GEN_COMMENTS = "/gen/{}/comments"; // [T1] GET

// ── Studio API: Video generation ───────────────────────────
// T1: video-render-status.md (Burp 2026-08-25).
constexpr std::string_view VIDEO_GENERATE_STATUS = "/video/generate/{}/status/"; // [T1] GET
constexpr std::string_view VIDEO_GEN_PENDING_BATCHES = "/video_gen/pending_batches"; // [T1] POST

// ── Studio API: Rights ─────────────────────────────────────
// T1: clips-relations.md (Burp 2026-08-25).
constexpr std::string_view MANGO_RIGHTS = "/mango/rights"; // [T1] POST

// ── Studio API: Notifications ───────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view NOTIFICATION_V2 = "/notification/v2"; // [T1] GET
constexpr std::string_view NOTIFICATION_BADGE_COUNT = "/notification/v2/badge-count"; // [T1] GET

// ── Studio API: Custom Models ──────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view CUSTOM_MODEL_PENDING = "/custom-model/pending/"; // [T1] GET

// ── Studio API: Personalization ─────────────────────────────
// T1: session-user-config.md (Burp 2026-08-25).
constexpr std::string_view PERSONALIZATION_MEMORY = "/personalization/memory"; // [T1] GET
constexpr std::string_view PERSONALIZATION_SETTINGS = "/personalization/settings"; // [T1] GET

// ── Studio API: Prompts (legacy) ───────────────────────────
// Recon-only; retained for source compatibility.
constexpr std::string_view PROMPTS = "/prompts/"; // [LEAD]

// ── Studio API: Contests ───────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view CONTESTS = "/contests/"; // [T1] GET

// ── Studio API: Music Player ────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view MUSIC_PLAYER_PLAYBAR_STATE = "/music_player/playbar_state"; // [T1] GET/POST

// ── Studio API: Modals ─────────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view MODALS = "/modals"; // [T1] GET

// ── Studio API: Statsig / Orpheus ──────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view STATSIG_EXPERIMENT = "/statsig/experiment/"; // [T1] POST

// ── Studio API: CMS Nudges ─────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view CMS_NUDGES_PUBLISH = "/cms/nudges/publish-nudge"; // [T1] GET
constexpr std::string_view CMS_NUDGES_SHARE = "/cms/nudges/share-nudge"; // [T1] GET

// ── Studio API: Sharing ────────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view SHARE_STATS = "/share/stats"; // [T1] GET

// ── Realtime (Ably SSE) ────────────────────────────────────
// T1: misc-telemetry.md (Burp 2026-08-25).
constexpr std::string_view REALTIME_DISCOVER = "/realtime/discover"; // [T1] GET

// ── B-Side / Orchestrator ──────────────────────────────────
// Recon/research only. These are joined to MODAL_BASE, not API_BASE.
constexpr std::string_view ORCHESTRATOR_CHAT = "/v1/orchestrator/chat"; // [LEAD]
constexpr std::string_view ORCHESTRATOR_HISTORY = "/v1/orchestrator/history"; // [LEAD]

// ── CDN ────────────────────────────────────────────────────
// Constructed fallbacks; no exact constructed path was captured.
constexpr std::string_view CDN_CLIP_MP3 = "/{}.mp3"; // [LEAD]
constexpr std::string_view CDN_CLIP_M4A = "/1/clip/{}.m4a"; // [LEAD]

} // namespace vc::suno::endpoints
