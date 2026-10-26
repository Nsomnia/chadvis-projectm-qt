#pragma once
// SunoModels.hpp - value types mirroring the CAPTURED studio-api wire schema
// (T1 Burp capture, Aug 2026). All snake_case names match the JSON keys 1:1;
// parsing lives in ClipParser / SunoAccountManager, conversion to QVariantMap
// lives in the QML bridge - these stay plain PODs.

#include <optional>
#include <algorithm>
#include <string>
#include <string_view>
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

    // ── Download product entitlement (captured, and previously discarded) ─────
    // Both fields ride on the same clip object as `media_urls`. They are the
    // server's own answer to "may this account use Suno's DOWNLOAD PRODUCT for
    // this clip" — the flag the site's Download button is gated on.
    //
    // Evidence (2026-10-05, measured against two authenticated captured feed
    // bodies, 40 clips): `is_download_unlocked` was **false in 36 of 40**, and
    // `download_disabled_reason` was `"remix_contest"` on 4 of those 40. 32
    // clips are locked carrying NO reason at all. A third-party report describes
    // a metered per-plan monthly download allowance ("one song = one download,
    // regardless of format"); that is `[LEAD]`-grade and is NOT asserted as the
    // explanation anywhere.
    //
    // NOT a veto on the streamed media. This was read as one, and it refused
    // 36/40 clips before a byte was fetched — the whole play surface was dead.
    // The media this client fetches is the progressive asset the account already
    // streams (unsigned, from the captured CloudFront host), decrypted locally
    // with the clip's own content key; the meter belongs to
    // `GET /api/download/clip/{id}`, which this client never calls. See
    // `SunoDownloader::downloadProductLock` for the full argument.
    //
    // `std::optional` rather than a plain bool on purpose. A plain `bool`
    // cannot distinguish "the server said no" from "this payload never carried
    // the field", and only the former is a lock. Absent reads as no lock, and
    // that is pinned from both directions by test_DownloadEntitlement.cpp.
    std::optional<bool> is_download_unlocked;

    /// Why the server locked it, when it says. Empty means either "unlocked" or
    /// "locked with no stated reason" — `is_download_unlocked` disambiguates.
    std::string download_disabled_reason;

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

    // ── The PlanFeature entitlement plane ─────────────────────────────────────
    // `accessible_features` from the SAME `/api/billing/info/` reply. This is one
    // of seven independent gating planes and it was being discarded, like the
    // server flag map was two rounds ago.
    //
    // It is NOT redundant with `credits`, `plan`, or any server flag: it is the
    // per-plan feature entitlement, and the corpus shows the three plan tiers
    // carry genuinely different sets (a Premier account holds 20; `convert_audio`
    // is in the Pro set and absent from Premier's, while Premier adds `studio`).
    // That makes it the one plane that can say "your plan does not include this"
    // rather than "this is switched off right now".
    //
    // Kept as raw strings rather than a closed enum deliberately. It is
    // SERVER-OWNED DATA, not code: a hardcoded list would miss any feature Suno
    // adds, which is the same stale-snapshot mistake as baking in the flag names.
    // The catalog is `constexpr`; this is the account's answer to it.
    ///
    /// Measured shape (2026-10-05, one authenticated body): a flat array of
    /// objects each carrying a single `name` string. **20 entries**, not the 22
    /// earlier summaries claimed — the number came from a secondary description
    /// and the bytes disagree, so the bytes won.
    std::vector<std::string> accessible_features;

    [[nodiscard]] bool hasFeature(std::string_view feature) const;
};

inline bool SunoBillingInfo::hasFeature(const std::string_view feature) const
{
    return std::ranges::any_of(accessible_features, [feature](const std::string& held) {
        return held == feature;
    });
}

} // namespace vc::suno
