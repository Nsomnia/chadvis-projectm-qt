#pragma once
// LineageFingerprint.hpp - one clip reduced to the evidence the heuristic reads.
//
// The fingerprint is computed once per clip per rebuild and then compared against
// candidates. It deliberately keeps only what a text signal can use: the original
// title and tags stay for presentation, the tokens are the comparison surface, and
// the model/age fields are the two non-text signals the user can weight.

#include <string>
#include <string_view>
#include <vector>

#include "SunoModels.hpp"

namespace vc::suno::lineage {

struct Fingerprint {
    std::string id;
    std::string title;
    std::string tagsRaw;

    /// `normalize(title)` — punctuation-free, lowercased.
    std::string normalizedTitle;
    std::vector<std::string> titleTokens;
    /// From `metadata.tags`, which is where Suno keeps the style/genre sentence.
    std::vector<std::string> styleTokens;
    /// From `metadata.prompt` (the free-form description), never lyrics: lyrics
    /// are the work, not the recipe, and two songs with the same chorus are not
    /// necessarily related.
    std::vector<std::string> promptTokens;
    std::string language;

    int modelMajor{0};
    i64 createdAt{0};
    i64 durationSeconds{0};
    i64 playCount{0};
    bool instrumental{false};

    /// False for a clip with no id: nothing can be indexed or rendered for it.
    bool valid{false};
};

/// `"chirp-v3-5"` -> 3, `"v4"` -> 4, `"3.5"` -> 3, unparseable -> 0.
int parseModelMajor(std::string_view modelName);

/// `"02:31"` -> 151, `"151.4"` -> 151, empty/unparseable -> 0.
i64 parseDurationSeconds(std::string_view duration);

Fingerprint fingerprint(const SunoClip& clip);

} // namespace vc::suno::lineage
