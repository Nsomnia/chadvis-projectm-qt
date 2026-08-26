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

} // namespace vc::suno
