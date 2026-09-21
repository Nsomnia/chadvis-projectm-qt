#pragma once
// SunoEndpoints.hpp - Centralized Suno API endpoint map
// One source of truth for every URL we hit.
//
// Updated 2026-09-21 from HAR capture 2026-06-10 (iOS "Orion" browser,
// Rest API Inspector v2.0.0, user sderek02@gmail.com / Premier plan).
// New endpoints discovered: realtime/discover (Ably SSE), notification/v2,
// contests, music_player/playbar_state, statsig/experiment (Orpheus flags),
// cms/nudges, personalization, custom-model/pending, prompts, modals,
// video_gen/pending_batches, share/stats, unified/homepage, profiles info,
// project pinned-clips, billing comparison/faq/descriptions, eligible-discounts,
// user_config, tos_acceptance, get_user_session_id.

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
constexpr std::string_view API_BASE       = "https://studio-api-prod.suno.com/api";
constexpr std::string_view MODAL_BASE     = "https://suno-ai--orpheus-prod-web.modal.run";
constexpr std::string_view CDN_BASE       = "https://cdn1.suno.ai";
constexpr std::string_view CDN_CLOUDFRONT_BASE = "https://d2lwuy8qc234o3.cloudfront.net";
constexpr std::string_view WEB_BASE       = "https://suno.com";

// NOTE: Clerk auth endpoints/versions live in suno/auth/ClerkAuthClient.hpp
// (AUTH_BASE, LEGACY_BASE, CLERK_API_VERSION, CLERK_JS_VERSION).

// ── Studio API: Generation ─────────────────────────────────
constexpr std::string_view GENERATE       = "/generate/v2-web/";

// ── Studio API: Lyrics ─────────────────────────────────────
constexpr std::string_view ALIGNED_LYRICS = "/gen/{}/aligned_lyrics/v2";
constexpr std::string_view CONVERT_WAV    = "/gen/{}/convert_wav/";
constexpr std::string_view WAV_FILE       = "/gen/{}/wav_file/";

// ── Studio API: Library / Feed ─────────────────────────────
constexpr std::string_view LIBRARY_FEED   = "/feed/v3";          // POST, cursor-based
constexpr std::string_view UNIFIED_HOMEPAGE = "/unified/homepage"; // POST, cursor-based
constexpr std::string_view SESSION        = "/session/";
constexpr std::string_view SESSION_CATALOG = "/session/";       // alias for SESSION

// ── Studio API: Billing ────────────────────────────────────
constexpr std::string_view BILLING_INFO   = "/billing/info/";
constexpr std::string_view BILLING_ELIGIBLE_DISCOUNTS = "/billing/eligible-discounts";
constexpr std::string_view BILLING_USAGE_PLAN_COMPARISON = "/billing/usage-plan-web-table-comparison/";
constexpr std::string_view BILLING_USAGE_PLAN_FAQ = "/billing/usage-plan-faq/";
constexpr std::string_view BILLING_USAGE_PLAN_DESCRIPTIONS = "/billing/usage-plan-descriptions/";

// ── Studio API: User / Profile / Account ────────────────────
constexpr std::string_view USER_CONFIG    = "/api/user/user_config/";
constexpr std::string_view USER_TOS_ACCEPTANCE = "/api/user/tos_acceptance";
constexpr std::string_view USER_GET_SESSION_ID = "/api/user/get_user_session_id/";
constexpr std::string_view PROFILES_INFO   = "/api/profiles/{}/info";
constexpr std::string_view PROFILES_PINNED_CLIPS = "/api/profiles/pinned-clips";

// ── Studio API: Projects ───────────────────────────────────
constexpr std::string_view PROJECT_PINNED_CLIPS = "/api/project/default/pinned-clips";

// ── Studio API: Notifications ───────────────────────────────
constexpr std::string_view NOTIFICATION_V2 = "/api/notification/v2";
constexpr std::string_view NOTIFICATION_BADGE_COUNT = "/api/notification/v2/badge-count";

// ── Studio API: Custom Models ──────────────────────────────
constexpr std::string_view CUSTOM_MODEL_PENDING = "/api/custom-model/pending/";

// ── Studio API: Personalization ─────────────────────────────
constexpr std::string_view PERSONALIZATION_MEMORY = "/api/personalization/memory";
constexpr std::string_view PERSONALIZATION_SETTINGS = "/api/personalization/settings";

// ── Studio API: Prompts ────────────────────────────────────
constexpr std::string_view PROMPTS = "/api/prompts/";

// ── Studio API: Contests ───────────────────────────────────
constexpr std::string_view CONTESTS = "/api/contests/";

// ── Studio API: Music Player ────────────────────────────────
constexpr std::string_view MUSIC_PLAYER_PLAYBAR_STATE = "/api/music_player/playbar_state";

// ── Studio API: Modals ─────────────────────────────────────
constexpr std::string_view MODALS = "/api/modals";

// ── Studio API: Video Generation ───────────────────────────
constexpr std::string_view VIDEO_GEN_PENDING_BATCHES = "/api/video_gen/pending_batches";

// ── Studio API: Statsig / Orpheus ──────────────────────────
constexpr std::string_view STATSIG_EXPERIMENT = "/api/statsig/experiment/";

// ── Studio API: CMS Nudges ─────────────────────────────────
constexpr std::string_view CMS_NUDGES_PUBLISH = "/api/cms/nudges/publish-nudge";
constexpr std::string_view CMS_NUDGES_SHARE = "/api/cms/nudges/share-nudge";

// ── Studio API: Sharing ────────────────────────────────────
constexpr std::string_view SHARE_STATS = "/api/share/stats";

// ── Realtime (Ably SSE) ────────────────────────────────────
constexpr std::string_view REALTIME_DISCOVER = "/api/realtime/discover";

// ── B-Side / Orchestrator ──────────────────────────────────
constexpr std::string_view ORCHESTRATOR_CHAT    = "/api/v1/orchestrator/chat";
constexpr std::string_view ORCHESTRATOR_HISTORY = "/api/v1/orchestrator/history";

// ── CDN ────────────────────────────────────────────────────
constexpr std::string_view CDN_CLIP_MP3   = "/{}.mp3";
constexpr std::string_view CDN_CLIP_M4A   = "/1/clip/{}.m4a";  // m4a-opus on CloudFront

} // namespace vc::suno::endpoints