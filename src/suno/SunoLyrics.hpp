#pragma once
// SunoLyrics.hpp - Structures for synced lyrics
// Word-level timestamps from Suno AI

#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc::suno {

struct AlignedWord {
    std::string word;
    f32 start_s;
    f32 end_s;
    f32 score; // p_align
};

struct AlignedLyrics {
    std::vector<AlignedWord> words;
    std::string songId;

    bool empty() const {
        return words.empty();
    }
};

} // namespace vc::suno
