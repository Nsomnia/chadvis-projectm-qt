#include "SunoLyrics.hpp"
#include <algorithm>
#include <sstream>
#include <regex>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "core/Logger.hpp"

namespace vc::suno {

static std::string normalize(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

std::vector<AlignedWord> LyricsAligner::parseJson(const QByteArray& json) {
    std::vector<AlignedWord> result;
    QJsonDocument doc = QJsonDocument::fromJson(json);

    if (doc.isNull()) {
        LOG_ERROR("LyricsAligner: Failed to parse JSON");
        return result;
    }

    QJsonArray rawWords;

    if (doc.isArray()) {
        rawWords = doc.array();
    } else if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QStringList keys = {"aligned_words", "alligned_words", "words", "lyrics", "aligned_lyrics"};
        bool found = false;
        
        for (const auto& key : keys) {
            if (obj.contains(key) && obj[key].isArray()) {
                rawWords = obj[key].toArray();
                found = true;
                break;
            }
        }

        // Fallback: search for any array that looks like words
        if (!found) {
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (it.value().isArray()) {
                    QJsonArray arr = it.value().toArray();
                    if (!arr.isEmpty() && arr[0].isObject()) {
                        QJsonObject first = arr[0].toObject();
                        if (first.contains("word") && (first.contains("start") || first.contains("start_s"))) {
                            rawWords = arr;
                            found = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    for (const auto& val : rawWords) {
        if (!val.isObject()) continue;
        QJsonObject w = val.toObject();
        
        AlignedWord aw;
        aw.word = w["word"].toString().toStdString();
        
        if (w.contains("start")) aw.start_s = w["start"].toDouble();
        else if (w.contains("start_s")) aw.start_s = w["start_s"].toDouble();
        else continue;

        if (w.contains("end")) aw.end_s = w["end"].toDouble();
        else if (w.contains("end_s")) aw.end_s = w["end_s"].toDouble();
        else continue;

        if (w.contains("score")) aw.score = w["score"].toDouble();
        else if (w.contains("p_align")) aw.score = w["p_align"].toDouble();
        else aw.score = 1.0f;

        result.push_back(aw);
    }
    
    // Sort just in case
    std::sort(result.begin(), result.end(), [](const AlignedWord& a, const AlignedWord& b) {
        return a.start_s < b.start_s;
    });

    return result;
}

AlignedLyrics LyricsAligner::align(const std::string& prompt, const std::vector<AlignedWord>& words) {
    AlignedLyrics result;
    result.words = words;
    
    if (words.empty()) return result;

    std::stringstream ss(prompt);
    std::string line;
    size_t wordIdx = 0;

    while (std::getline(ss, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        // Skip metadata tags like [Verse], [Chorus]
        if (line.front() == '[' && line.back() == ']') {
            // Optional: add as a line with no words but we'll skip for now
            continue;
        }

        AlignedLine al;
        al.text = line;
        
        // Tokenize the line to match words
        std::stringstream lineSs(line);
        std::string promptWord;
        std::vector<std::string> lineTokens;
        
        while (lineSs >> promptWord) {
            std::string norm = normalize(promptWord);
            if (!norm.empty()) {
                lineTokens.push_back(norm);
            }
        }

        if (lineTokens.empty()) continue;

        int matchIndex = -1;
        size_t searchLimit = 50; 
        
        // Primary Strategy: Match first word
        for (size_t w = wordIdx; w < words.size() && w < wordIdx + searchLimit; ++w) {
            std::string audioWord = normalize(words[w].word);
            
            if (audioWord == lineTokens[0]) {
                 matchIndex = static_cast<int>(w);
                 
                 // Optimization: Confirm with second word if available
                 if (lineTokens.size() > 1 && w + 1 < words.size()) {
                     std::string nextAudioWord = normalize(words[w+1].word);
                     if (nextAudioWord == lineTokens[1]) {
                         matchIndex = static_cast<int>(w); // High confidence
                         break;
                     }
                     // If second word doesn't match, this might be a false positive "the", keep looking?
                     // For now, we take the first match, but loop could continue if we wanted strictness.
                 } else {
                     // Single word line or end of stream, accept it
                     break;
                 }
            }
        }

        // Fallback Strategy: Anchor on second word if first missing
        if (matchIndex == -1 && lineTokens.size() > 1) {
            for (size_t w = wordIdx; w < words.size() && w < wordIdx + searchLimit; ++w) {
                if (normalize(words[w].word) == lineTokens[1]) {
                     // Use timestamp of 2nd word as approximate start (or prev word if valid)
                     matchIndex = static_cast<int>(w); 
                     // Ideally we'd want the index of the missing first word, but it's missing from audio
                     // So we start from this word
                     break;
                }
            }
        }
        
        if (matchIndex != -1) {
            wordIdx = matchIndex;
            al.start_s = words[wordIdx].start_s;
            
            // Assign words to this line
            // We greedily consume words that match the tokens, but audio might have extra words or missing words
            // Simple approach: consume until line ends or we hit next line's start (handled by loop)
            
            // For now, just set start time. End time is handled by post-process
            wordIdx++; // Advance past the match
            
            // Try to match remaining tokens to advance wordIdx
            size_t tokenIdx = 1;
            while (tokenIdx < lineTokens.size() && wordIdx < words.size()) {
                 if (normalize(words[wordIdx].word) == lineTokens[tokenIdx]) {
                     tokenIdx++;
                 }
                 wordIdx++;
            }
        } else {
            // Line not found in audio (spoken? hallucinated?)
            // Inherit time from previous line end?
            if (!result.lines.empty()) {
                al.start_s = result.lines.back().end_s;
            } else {
                al.start_s = 0;
            }
        }
        
        // Ensure end time is at least start time
        al.end_s = al.start_s;
        result.lines.push_back(al);
    }

    // Post-process: ensure line end times don't overlap too much and cover gaps
    for (size_t i = 0; i < result.lines.size(); ++i) {
        if (i < result.lines.size() - 1) {
            // Set end of current line to start of next line
            result.lines[i].end_s = std::max(result.lines[i].end_s, result.lines[i+1].start_s);
        }
    }

    return result;
}

} // namespace vc::suno
