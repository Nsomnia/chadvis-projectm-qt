#pragma once
// SunoLyrics.hpp - Structures for synced lyrics
// Word-level timestamps from Suno AI

#include <string>
#include <vector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "util/Types.hpp"

namespace vc::suno {

struct AlignedWord {
    std::string word;
    f32 start_s;
    f32 end_s;
    f32 score; // p_align
};

struct AlignedLine {
    std::string text;
    f32 start_s;
    f32 end_s;
    std::vector<AlignedWord> words;
};

struct AlignedLyrics {
    std::vector<AlignedWord> words;
    std::vector<AlignedLine> lines;
    std::string songId;

    bool empty() const {
        return words.empty() && lines.empty();
    }
};

class LyricsAligner {
public:
    static AlignedLyrics align(const std::string& prompt, const std::vector<AlignedWord>& words);
    static std::vector<AlignedWord> parseJson(const QByteArray& json, f32 duration = 0.0f);
    static std::vector<AlignedWord> estimateTimings(const std::string& text, f32 duration);
    static AlignedLyrics parseLrc(const std::string& content);
    static AlignedLyrics parseSrt(const std::string& content);
};

} // namespace vc::suno
