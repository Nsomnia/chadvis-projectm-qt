#pragma once

#include "util/Result.hpp"
#include "suno/SunoLyrics.hpp"
#include <filesystem>
#include <string>

namespace vc::suno {

namespace fs = std::filesystem;

class LyricAligner {
public:
    virtual ~LyricAligner() = default;

    virtual Result<AlignedLyrics> align(const fs::path& audioPath, 
                                        const std::string& lyricsText) = 0;
};

class PlaceholderAligner : public LyricAligner {
public:
    Result<AlignedLyrics> align(const fs::path& audioPath, 
                                const std::string& lyricsText) override {
        AlignedLyrics result;
        return Result<AlignedLyrics>::ok(result);
    }
};

}
