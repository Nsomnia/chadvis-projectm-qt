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

} // namespace vc::suno
