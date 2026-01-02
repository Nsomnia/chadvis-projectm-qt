#pragma once
// SunoModels.hpp - Data structures for Suno AI API
// Maps JSON responses to C++ structs

#include <optional>
#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc::suno {

struct SunoMetadata {
    std::string prompt;
    std::string tags;
    std::string type; // "gen", "upload"
    std::string lyrics;
    std::string history;
    std::string error_message;
    std::string duration;
    bool refund_credits{false};
    bool stream{false};
};

struct SunoClip {
    std::string id; // UUID
    std::string title;
    std::string video_url;
    std::string audio_url;
    std::string image_url;
    std::string image_large_url;
    std::string major_model_version; // "v3", "v3.5"
    std::string model_name; // "chirp-v3"
    std::string display_name; // Artist name usually
    std::string handle; // User handle
    bool is_liked{false};
    bool is_trashed{false};
    bool is_public{false};
    std::string created_at;
    std::string status; // "streaming", "complete"

    SunoMetadata metadata;

    // Helper to check if it's a stem
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

} // namespace vc::suno
