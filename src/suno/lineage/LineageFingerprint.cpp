#include "LineageFingerprint.hpp"

#include <algorithm>
#include <cctype>

#include "LineageText.hpp"

namespace vc::suno::lineage {

namespace {

constexpr std::size_t kTitleTokens = 24;
constexpr std::size_t kStyleTokens = 32;
constexpr std::size_t kPromptTokens = 48;

} // namespace

int parseModelMajor(std::string_view modelName) {
    // The first digit run is the generation in every spelling observed:
    // "chirp-v3-5", "chirp-v3.5", "v4", "3.5", "chirp-v2", "v4.5". Leading text is
    // skipped rather than parsed, so an unfamiliar prefix still yields the number.
    for (std::size_t i = 0; i < modelName.size(); ++i) {
        const auto byte = static_cast<unsigned char>(modelName[i]);
        if (byte < '0' || byte > '9') continue;
        int value = 0;
        for (std::size_t j = i; j < modelName.size(); ++j) {
            const auto digit = static_cast<unsigned char>(modelName[j]);
            if (digit < '0' || digit > '9') break;
            if (value > 100) return 0; // absurd run: not a generation
            value = value * 10 + (digit - '0');
        }
        return value;
    }
    return 0;
}

i64 parseDurationSeconds(std::string_view duration) {
    if (duration.empty()) return 0;
    const auto allDigits = [](std::string_view text) {
        return !text.empty() &&
               std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
    };
    const std::size_t colon = duration.find(':');
    const std::string_view head = duration.substr(0, colon);
    if (colon == std::string_view::npos) {
        // Decimal seconds ("151.4"). A bare non-numeric string is unknown, never 0
        // seconds presented as a duration.
        std::string digits;
        digits.reserve(head.size());
        for (const char c : head) {
            if (c == '.') break;
            if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
            digits.push_back(c);
        }
        return digits.empty() ? 0 : static_cast<i64>(std::stoll(digits));
    }
    const std::string_view tail = duration.substr(colon + 1);
    if (!allDigits(head) || !allDigits(tail)) return 0;
    const i64 minutes = std::stoll(std::string(head));
    const i64 seconds = std::stoll(std::string(tail));
    if (seconds > 59) return 0;
    return minutes * 60 + seconds;
}

Fingerprint fingerprint(const SunoClip& clip) {
    Fingerprint print;
    print.id = clip.id;
    if (print.id.empty()) return print;

    print.title = clip.title;
    print.tagsRaw = clip.metadata.tags;
    print.normalizedTitle = text::normalize(clip.title);
    print.titleTokens = text::tokenize(clip.title, kTitleTokens);
    print.styleTokens = text::tokenize(clip.metadata.tags, kStyleTokens);
    print.promptTokens = text::tokenize(clip.metadata.prompt, kPromptTokens);

    // The language signal describes how the RECIPE is written, so it prefers the
    // free-form description, then the style sentence, then the title. Lyrics are
    // deliberately not read.
    const std::string_view descriptive = !clip.metadata.prompt.empty() ? clip.metadata.prompt
                                        : !clip.metadata.tags.empty() ? clip.metadata.tags
                                                                      : clip.title;
    print.language = text::languageHint(descriptive);

    print.modelMajor = parseModelMajor(!clip.major_model_version.empty()
                                               ? clip.major_model_version
                                               : clip.model_name);
    print.createdAt = text::isoEpochSeconds(clip.created_at);
    print.durationSeconds = parseDurationSeconds(clip.metadata.duration);
    print.playCount = clip.play_count;
    // The wire flag is the only source. Lyrics-empty looks like a second one and
    // is not: the v3 feed omits lyrics for perfectly vocal songs, so inferring
    // "instrumental" from a missing field would veto real pairs.
    print.instrumental = clip.metadata.make_instrumental;
    print.valid = true;
    return print;
}

} // namespace vc::suno::lineage
