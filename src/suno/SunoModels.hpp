#pragma once
// SunoModels.hpp - value types mirroring the CAPTURED studio-api wire schema
// (T1 Burp capture, Aug 2026). All snake_case names match the JSON keys 1:1;
// parsing lives in ClipParser / SunoAccountManager, conversion to QVariantMap
// lives in the QML bridge - these stay plain PODs.

#include <optional>
#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc::suno {

/// One entry of a clip's media_urls array (progressive delivery variants).
struct SunoMediaUrl {
    std::string url;
    std::string content_type; // e.g. "m4a-opus" | "mp3"
    std::string delivery;     // e.g. "progressive"
    std::string encoding;     // optional on the wire
};

struct SunoMetadata {
    std::string prompt;
    std::string tags;
    std::string negative_tags; // optional on the wire
    std::string type;
    std::string lyrics;
    std::string infillLyrics;
    std::string history;
    std::string error_message;
    std::string duration;
    std::string bpm;
    std::string key;
    bool refund_credits{false};
    bool stream{false};
    double weirdness{0.0};
    double style_weight{0.0};
    bool make_instrumental{false};
    std::string model_id;
};

struct SunoClip {
    std::string id;
    std::string title;
    std::string video_url;     // "" until the video render finishes
    std::string audio_url;     // "" until status == "complete"
    std::string image_url;
    std::string image_large_url;
    std::string major_model_version;
    std::string model_name;
    std::string mv;
    std::string display_name;
    std::string handle;
    std::string user_id;
    std::string entity_type;   // "song_schema" on current captures
    std::string status;        // "submitted" | "complete"
    std::string created_at;

    i64 play_count{0};
    i64 upvote_count{0};
    i64 batch_index{0};
    bool allow_comments{true};
    bool is_verified{false};
    bool has_hook{false};
    bool is_persona_root{false};
    bool is_liked{false};
    bool is_trashed{false};
    bool is_public{false};

    SunoMetadata metadata;

    /// Progressive delivery variants (m4a-opus/mp3); may be empty.
    std::vector<SunoMediaUrl> media_urls;

    bool isStem() const {
        return metadata.type == "gen_stem" || metadata.type == "stem";
    }
};

// ── GET /api/session/ payload subset ────────────────────────────────────────

struct SunoUserSummary {
    std::string email;
    std::string username;
    std::string id;
    std::string clerk_id;
    std::string display_name;
    std::string handle;
    std::string avatar_image_url;
    bool is_vip{false};
    i64 total_clips{0};
};

/// max_lengths sub-object of a model entry (character limits).
struct SunoModelLimits {
    i64 title{0};
    i64 prompt{0};
    i64 tags{0};
    i64 negative_tags{0};
    i64 gpt_description_prompt{0};
};

struct SunoModelInfo {
    std::string name;
    std::string external_key;
    std::string major_version;
    std::string description;
    bool can_use{false};
    bool is_default_model{false};
    bool is_default_free_model{false};
    SunoModelLimits max_lengths;
    std::vector<std::string> capabilities;
    std::vector<std::string> features;
    std::vector<std::string> badges;
};

// ── GET /api/billing/info/ payload subset ───────────────────────────────────

struct SunoPlanInfo {
    std::string plan_key;
    std::string name;
    std::string level;
    double monthly_price_usd{0.0};
};

struct SunoBillingInfo {
    i64 credits{0};
    bool is_active{false};
    bool subscription_type{false}; // bool on the wire, yes
    std::string period;            // "month"
    i64 monthly_usage{0};
    i64 monthly_limit{0};
    std::string renews_on;         // ISO date string
    SunoPlanInfo plan;
    i64 credit_pack_count{0};
};

// ── GET /api/billing/eligible-discounts payload ──────────────

struct SunoBonusOffer {
    std::string plan_key;
    std::string period;
    i64 bonus_credits{0};
};

struct SunoEligibleDiscounts {
    // eligible_discounts is an object/map on the wire; we keep it as a JSON
    // string for flexibility and expose structured bonus_offers.
    std::string raw_json;
    std::vector<SunoBonusOffer> bonus_offers;
};

// ── GET /api/cms/nudges/* payload ─────────────────────────────

struct SunoNudge {
    std::string slug;
    std::string title;
    std::string body;
    std::string cta_label;
    std::string cta_url;
    bool is_active{true};
};

// ── GET /api/notification/v2 payload ──────────────────────────

struct SunoNotificationAuthor {
    std::string user_id;
    std::string display_name;
    std::string handle;
    std::string avatar_image_url;
};

struct SunoNotification {
    std::string id;
    std::string type;            // e.g. "caption_mention"
    std::string caption;
    std::string created_at;
    bool read{false};
    SunoNotificationAuthor author;
    std::string content_type;    // e.g. "clip"
    std::string content_id;
};

// ── GET /api/contests/ payload ────────────────────────────────

struct SunoContest {
    std::string id;
    std::string title;
    std::string description;
    std::string status;          // e.g. "active", "ended"
    std::string base_clip_id;
    std::string ends_at;         // ISO date
    i64 submission_count{0};
    i64 prize_credits{0};
};

// ── GET /api/music_player/playbar_state payload ───────────────

struct SunoPlaybarState {
    std::string state;           // "paused" | "playing"
    double song_play_time{0.0};
    std::string repeat_state;    // "no-repeat" | "repeat" | "repeat-all"
    double volume{100.0};
    std::string device_id;
    std::string device_type;     // e.g. "Android"
};

// ── POST /api/statsig/experiment/ payload (Orpheus flags) ─────

struct SunoOrpheusFlags {
    bool is_enabled{false};
    bool is_auto_mode{false};
    bool is_canvas_enabled{false};
    bool default_to_chat{false};
    std::string group;           // "CONTROL" | "TREATMENT" | ...
};

// ── GET /api/personalization/settings payload ─────────────────

struct SunoPersonalizationSettings {
    bool styles_augmentation{false};
};

// ── GET /api/custom-model/pending/ payload ─────────────────────

struct SunoCustomModelPending {
    bool has_pending{false};
    std::vector<std::string> pending_models;
};

// ── GET /api/share/stats payload ───────────────────────────────

struct SunoShareStats {
    std::string content_type;
    i64 num_shared{0};
};

// ── GET /api/modals payload ────────────────────────────────────
// Returns an array of modal definitions; we keep the raw JSON for flexibility.

// ── GET /api/realtime/discover payload ─────────────────────────

struct SunoRealtimeDiscover {
    std::string stream_url;      // e.g. "https://main.realtime.ably.net/sse?v=1.2&enveloped=false"
    std::string credential;      // "embedded_token"
    std::string jwt_header_param; // "x-ably-token"
};

// ── GET /api/prompts/ payload ──────────────────────────────────
// Returns paginated saved prompts (tags + lyrics). Raw JSON kept for flexibility.

// ── GET /api/user/user_config/ payload ─────────────────────────

struct SunoUserConfig {
    bool shown_creation_tour{false};
    std::string preferred_tags;
    std::string notification_preferences;
    bool publish_remix_default{false};
};

// ── GET /api/user/tos_acceptance payload ───────────────────────

struct SunoTosAcceptance {
    bool has_accepted_tos{false};
    std::string has_accepted_tos_timestamp;
};

// ── GET /api/user/get_user_session_id/ payload ─────────────────

struct SunoUserSessionId {
    std::string session_id;
};

} // namespace vc::suno
