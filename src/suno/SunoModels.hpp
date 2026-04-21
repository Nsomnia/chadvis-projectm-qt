#pragma once
#include <optional>
#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc::suno {

struct SunoMetadata {
    std::string prompt;
    std::string tags;
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
    std::string video_url;
    std::string audio_url;
    std::string image_url;
    std::string image_large_url;
    std::string major_model_version;
    std::string model_name;
    std::string mv;
    std::string display_name;
    std::string handle;
    bool is_liked{false};
    bool is_trashed{false};
    bool is_public{false};
    std::string created_at;
    std::string status;

    SunoMetadata metadata;

    bool isStem() const {
        return metadata.type == "gen_stem" || metadata.type == "stem";
    }
};

struct SunoProject {
    std::string id;
    std::string name;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

struct SunoPlaylist {
    std::string id;
    std::string name;
    std::string description;
    std::string image_url;
    u32 num_total_clips{0};
};

// B-Side / Experimental feature tracking
struct SunoFeatureGate {
    std::string name;
    bool enabled{false};
    std::string value;
};

} // namespace vc::suno
