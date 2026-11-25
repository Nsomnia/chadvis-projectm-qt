#pragma once
// LineageText.hpp - the text primitives the lineage heuristic is built from.
//
// Header-only and Qt-free on purpose: every function here is a pure transform, so
// the scoring rules can be argued about and tested without a database, a network
// or a Qt event loop. The three things that matter:
//
//   * tokenize() is deterministic and locale-independent. Unicode text is folded
//     to a byte-safe classification rather than lowercased through a locale, so a
//     Turkish "I" cannot change a graph edge.
//   * languageHint() classifies the SCRIPT of the descriptive text. It is a hint,
//     not a language detector: "same script, different language" scores as a
//     match, which is deliberate — the corpus is overwhelmingly Latin-script and a
//     real detector would be a dependency this feature does not need.
//   * isoEpochSeconds() parses the wire's `created_at` without <chrono>'s calendar
//     machinery (days_from_civil, the published constant-time algorithm).

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/Types.hpp"

namespace vc::suno::lineage::text {

inline bool isAlnumByte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

inline char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// English function words only. The corpus is English-tagged; a word list per
/// language would be a dependency and an unbounded maintenance bill, and the
/// tokens dropped here are the ones that made every pair of songs look related.
inline bool isStopWord(std::string_view token) {
    static constexpr std::string_view kStop[] = {
            "the", "a",  "an",  "and", "or",  "of",  "to",  "in",  "on",  "for",
            "with", "at", "by",  "is",  "it",  "my",  "me",  "we",  "you", "your",
            "i",   "be", "that", "this", "as", "from", "into", "song", "music"};
    return std::ranges::any_of(kStop, [token](std::string_view word) { return word == token; });
}

/// Split on anything that is not ASCII alphanumeric, lowercase, drop single
/// characters and function words, and de-duplicate while keeping first-seen order.
///
/// `maxTokens` bounds the work per clip: a sizeable description contributes its
/// opening vocabulary and stops, which is stable across library syncs because it
/// is a prefix rather than a sample.
inline std::vector<std::string> tokenize(std::string_view text, std::size_t maxTokens = 64) {
    std::vector<std::string> tokens;
    tokens.reserve(std::min<std::size_t>(maxTokens, 32));
    std::string current;
    const auto flush = [&tokens](std::string& word) {
        if (word.size() >= 2 && !isStopWord(word) &&
            std::ranges::find(tokens, word) == tokens.end()) {
            tokens.push_back(std::move(word));
        }
        word.clear();
    };
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        if (isAlnumByte(byte)) {
            current.push_back(asciiLower(static_cast<char>(byte)));
        } else {
            flush(current);
            if (tokens.size() >= maxTokens) return tokens;
        }
    }
    flush(current);
    if (tokens.size() > maxTokens) tokens.resize(maxTokens);
    return tokens;
}

/// Lowercased, single-spaced, punctuation-free copy — the form used for exact and
/// containment comparisons ("Song (Cover)" -> "song cover").
inline std::string normalize(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        if (isAlnumByte(byte)) {
            if (pendingSpace && !out.empty()) out.push_back(' ');
            pendingSpace = false;
            out.push_back(asciiLower(static_cast<char>(byte)));
        } else {
            pendingSpace = true;
        }
    }
    return out;
}

/// Jaccard similarity of two already-de-duplicated token lists. 0.0 when either
/// side is empty: no evidence is not "perfect agreement".
inline double jaccard(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0;
    std::size_t shared = 0;
    for (const auto& token : a) {
        if (std::ranges::find(b, token) != b.end()) ++shared;
    }
    const std::size_t unionSize = a.size() + b.size() - shared;
    return unionSize == 0 ? 0.0 : static_cast<double>(shared) / static_cast<double>(unionSize);
}

/// The dominant writing system of `text`, by UTF-8 lead byte.
///
/// Returns "none" for text with no classified code point, "mixed" when no script
/// holds at least 70% of them. Diacritics inside Latin text (CJK is 3 bytes,
/// Cyrillic 2) are counted by lead byte only, which is why U+00E9-style Latin-1
/// supplements (lead 0xC3) are folded into "latin" rather than starting a class.
inline std::string languageHint(std::string_view text) {
    static constexpr std::string_view kLatin = "latin";
    static constexpr std::string_view kCyrillic = "cyrillic";
    static constexpr std::string_view kCjk = "cjk";
    static constexpr std::string_view kHangul = "hangul";
    static constexpr std::string_view kArabic = "arabic";
    static constexpr std::string_view kDevanagari = "devanagari";
    static constexpr std::string_view kOther = "other";

    std::size_t latin = 0, cyrillic = 0, cjk = 0, hangul = 0, arabic = 0, devanagari = 0,
                other = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x80) {
            if (isAlnumByte(byte)) ++latin;
            continue;
        }
        // Count the lead byte once and skip its continuation bytes.
        std::size_t extra = 0;
        if (byte >= 0xF0) {
            extra = 3;
        } else if (byte >= 0xE0) {
            extra = 2;
        } else if (byte >= 0xC0) {
            extra = 1;
        } else {
            continue; // stray continuation byte: the string is not valid UTF-8 here
        }
        i = std::min(i + extra, text.size() - 1);
        // Script buckets by UTF-8 lead byte:
        //   0xD0-0xD1  Cyrillic            U+0400-U+04FF
        //   0xD8-0xDB  Arabic              U+0600-U+06FF
        //   0xE0       Devanagari          U+0900-U+097F
        //   0xE3-0xE9  CJK + kana          U+3000-U+9FFF
        //   0xEA-0xED  Hangul              U+A960-U+D7FF
        //   0xC3-0xCC  Latin-1 supplement onwards (diacritics)
        if (byte == 0xD0 || byte == 0xD1) {
            ++cyrillic;
        } else if (byte >= 0xE3 && byte <= 0xE9) {
            ++cjk;
        } else if (byte >= 0xEA && byte <= 0xED) {
            ++hangul;
        } else if (byte >= 0xD8 && byte <= 0xDB) {
            ++arabic;
        } else if (byte == 0xE0) {
            ++devanagari;
        } else if (byte >= 0xC3 && byte <= 0xCC) {
            ++latin;
        } else {
            ++other;
        }
    }

    const std::size_t total = latin + cyrillic + cjk + hangul + arabic + devanagari + other;
    if (total == 0) return std::string("none");
    const auto share = [total](std::size_t count) {
        return static_cast<double>(count) / static_cast<double>(total);
    };
    struct Class {
        std::size_t count;
        std::string_view name;
    };
    const Class classes[] = {{latin, kLatin},   {cyrillic, kCyrillic}, {cjk, kCjk},
                             {hangul, kHangul}, {arabic, kArabic},     {devanagari, kDevanagari},
                             {other, kOther}};
    const Class* best = &classes[0];
    for (const auto& candidate : classes) {
        if (candidate.count > best->count) best = &candidate;
    }
    if (best->count == 0 || share(best->count) < 0.70) return std::string("mixed");
    return std::string(best->name);
}

/// Days from 1970-01-01 to `y-m-d`, proleptic Gregorian. Hinnant's days_from_civil.
inline i64 daysFromCivil(i64 y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const i64 era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);           // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;       // [0, 146096]
    return era * 146097 + static_cast<i64>(doe) - 719468;
}

/// Parse the wire's ISO-8601 `created_at` ("2026-09-03T10:00:00.123456Z") into
/// epoch seconds. Returns 0 when the string is not that shape; 0 is also "no
/// timestamp", and every caller treats it as "unknown, no age evidence".
inline i64 isoEpochSeconds(std::string_view stamp) {
    if (stamp.size() < 19) return 0;
    const auto digits = [stamp](std::size_t pos, std::size_t count) -> i64 {
        if (pos + count > stamp.size()) return -1;
        i64 value = 0;
        for (std::size_t i = pos; i < pos + count; ++i) {
            const char c = stamp[i];
            if (c < '0' || c > '9') return -1;
            value = value * 10 + (c - '0');
        }
        return value;
    };
    const i64 year = digits(0, 4);
    const i64 month = digits(5, 2);
    const i64 day = digits(8, 2);
    const i64 hour = digits(11, 2);
    const i64 minute = digits(14, 2);
    const i64 second = digits(17, 2);
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60) {
        return 0;
    }
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           hour * 3600 + minute * 60 + second;
}

} // namespace vc::suno::lineage::text
