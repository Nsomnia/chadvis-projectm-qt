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

std::vector<AlignedWord> LyricsAligner::parseJson(const QByteArray& json, f32 duration) {
    std::vector<AlignedWord> result;
    QJsonDocument doc = QJsonDocument::fromJson(json);

    // Case 1: Plain Text (doc is null or invalid JSON) or Empty/Whitespace
    if (doc.isNull() || (doc.isArray() && doc.array().isEmpty()) || (doc.isObject() && doc.object().isEmpty())) {
        std::string text = json.toStdString();
        
        if (text.empty()) return result;

        if (duration <= 0.0f) duration = 180.0f;
        
        std::stringstream ss(text);
        std::string wordStr;
        std::vector<std::string> words;
        while (ss >> wordStr) {
             if (wordStr.size() >= 2 && wordStr.front() == '"' && wordStr.back() == '"') {
                 wordStr = wordStr.substr(1, wordStr.size() - 2);
             }
             words.push_back(wordStr);
        }

        if (!words.empty()) {
            f32 wordDur = duration / words.size();
            for (size_t i = 0; i < words.size(); ++i) {
                AlignedWord aw;
                aw.word = words[i];
                aw.start_s = i * wordDur;
                aw.end_s = (i + 1) * wordDur;
                aw.score = 0.5f;
                result.push_back(aw);
            }
        }
        return result;
    }

    QJsonArray rawWords;
    bool foundWords = false;

    if (doc.isArray()) {
        rawWords = doc.array();
        foundWords = true;
    } 
    else if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QStringList keys = {"aligned_words", "alligned_words", "words", "lyrics", "aligned_lyrics"};
        
        for (const auto& key : keys) {
            if (obj.contains(key) && obj[key].isArray()) {
                rawWords = obj[key].toArray();
                foundWords = true;
                break;
            }
        }

        if (!foundWords) {
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (it.value().isArray()) {
                    QJsonArray arr = it.value().toArray();
                    if (!arr.isEmpty() && arr[0].isObject()) {
                        QJsonObject first = arr[0].toObject();
                        if (first.contains("word") && (first.contains("start") || first.contains("start_s"))) {
                            rawWords = arr;
                            foundWords = true;
                            break;
                        }
                    }
                }
            }
        }
        
        if (!foundWords) {
             QStringList textKeys = {"text", "content", "prompt", "lyrics", "value"};
             for (const auto& key : textKeys) {
                 if (obj.contains(key) && obj[key].isString()) {
                     return parseJson(obj[key].toString().toUtf8(), duration);
                 }
             }
        }
    }

    if (foundWords) {
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
        
        std::sort(result.begin(), result.end(), [](const AlignedWord& a, const AlignedWord& b) {
            return a.start_s < b.start_s;
        });
    }

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

AlignedLyrics LyricsAligner::parseLrc(const std::string& content) {
    AlignedLyrics result;
    std::stringstream ss(content);
    std::string line;
    // Regex for [mm:ss.xx] or [mm:ss]
    std::regex timeRegex(R"(\[(\d+):(\d+(?:\.\d+)?)\])"); 

    struct LrcLine {
        double time;
        std::string text;
    };
    std::vector<LrcLine> rawLines;

    while (std::getline(ss, line)) {
        std::smatch match;
        if (std::regex_search(line, match, timeRegex)) {
            double min = std::stod(match[1]);
            double sec = std::stod(match[2]);
            double time = min * 60.0 + sec;
            
            std::string text = match.suffix().str();
            // Trim whitespace
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            
            if (!text.empty()) {
                rawLines.push_back({time, text});
            }
        }
    }

    // Convert to AlignedLyrics with interpolated word timings
    for (size_t i = 0; i < rawLines.size(); ++i) {
        AlignedLine al;
        al.text = rawLines[i].text;
        al.start_s = static_cast<f32>(rawLines[i].time);
        
        // Estimate end time based on next line or arbitrary duration
        if (i + 1 < rawLines.size()) {
            al.end_s = static_cast<f32>(rawLines[i+1].time);
        } else {
            al.end_s = al.start_s + 5.0f; // Default 5s for last line
        }
        
        // Don't let lines persist too long if there's a large gap? 
        // For LRC, gaps usually mean instrumental, but we'll stick to next-line-start for now.
        // Or cap it at say 10 seconds?
        if (al.end_s - al.start_s > 10.0f) al.end_s = al.start_s + 10.0f;

        // Split words and interpolate
        std::stringstream textSs(al.text);
        std::string wordStr;
        std::vector<std::string> tokenizedWords;
        while (textSs >> wordStr) {
            tokenizedWords.push_back(wordStr);
        }

        if (!tokenizedWords.empty()) {
            f32 duration = al.end_s - al.start_s;
            f32 wordDur = duration / tokenizedWords.size();
            
            for (size_t w = 0; w < tokenizedWords.size(); ++w) {
                AlignedWord aw;
                aw.word = tokenizedWords[w];
                aw.start_s = al.start_s + (w * wordDur);
                aw.end_s = aw.start_s + wordDur;
                aw.score = 1.0f;
                
                al.words.push_back(aw);
                result.words.push_back(aw); // Flattened list
            }
        }
        
        result.lines.push_back(al);
    }
    
    return result;
}

AlignedLyrics LyricsAligner::parseSrt(const std::string& content) {
    AlignedLyrics result;
    std::stringstream ss(content);
    std::string line;
    
    // SRT format:
    // 1
    // 00:00:01,000 --> 00:00:04,000
    // Text line
    // (blank line)
    
    // Regex for 00:00:01,000 --> 00:00:04,000
    std::regex timeRegex(R"((\d+):(\d+):(\d+)[,\.](\d+)\s+-->\s+(\d+):(\d+):(\d+)[,\.](\d+))");
    
    enum State { Index, Time, Text };
    State state = Index;
    
    AlignedLine currentLine;
    bool hasTime = false;
    
    auto parseTime = [](const std::smatch& m, int offset) -> f32 {
        double h = std::stod(m[offset + 1]);
        double m_ = std::stod(m[offset + 2]);
        double s = std::stod(m[offset + 3]);
        double ms = std::stod(m[offset + 4]);
        return static_cast<f32>(h * 3600.0 + m_ * 60.0 + s + ms / 1000.0);
    };

    while (std::getline(ss, line)) {
        // Trim
        line.erase(line.find_last_not_of("\r") + 1); // Handle Windows line endings
        
        if (line.empty()) {
            if (hasTime && !currentLine.text.empty()) {
                // Process completed line
                // Interpolate words
                std::stringstream textSs(currentLine.text);
                std::string wordStr;
                std::vector<std::string> tokenizedWords;
                while (textSs >> wordStr) {
                    tokenizedWords.push_back(wordStr);
                }

                if (!tokenizedWords.empty()) {
                    f32 duration = currentLine.end_s - currentLine.start_s;
                    // Clamp duration to avoid div/0 or huge logic
                    if (duration <= 0.001f) duration = 1.0f; 
                    
                    f32 wordDur = duration / tokenizedWords.size();
                    for (size_t w = 0; w < tokenizedWords.size(); ++w) {
                        AlignedWord aw;
                        aw.word = tokenizedWords[w];
                        aw.start_s = currentLine.start_s + (w * wordDur);
                        aw.end_s = aw.start_s + wordDur;
                        aw.score = 1.0f;
                        
                        currentLine.words.push_back(aw);
                        result.words.push_back(aw);
                    }
                }
                
                result.lines.push_back(currentLine);
                currentLine = AlignedLine();
                hasTime = false;
            }
            state = Index;
            continue;
        }
        
        std::smatch match;
        if (std::regex_search(line, match, timeRegex)) {
            currentLine.start_s = parseTime(match, 0);
            currentLine.end_s = parseTime(match, 4);
            hasTime = true;
            state = Text;
            continue;
        }
        
        if (state == Text) {
            if (!currentLine.text.empty()) currentLine.text += " ";
            currentLine.text += line;
        }
    }
    
    // Handle last line if no trailing newline
    if (hasTime && !currentLine.text.empty()) {
        std::stringstream textSs(currentLine.text);
        std::string wordStr;
        std::vector<std::string> tokenizedWords;
        while (textSs >> wordStr) tokenizedWords.push_back(wordStr);

        if (!tokenizedWords.empty()) {
            f32 duration = currentLine.end_s - currentLine.start_s;
            if (duration <= 0.001f) duration = 1.0f; 
            f32 wordDur = duration / tokenizedWords.size();
            for (size_t w = 0; w < tokenizedWords.size(); ++w) {
                AlignedWord aw;
                aw.word = tokenizedWords[w];
                aw.start_s = currentLine.start_s + (w * wordDur);
                aw.end_s = aw.start_s + wordDur;
                aw.score = 1.0f;
                currentLine.words.push_back(aw);
                result.words.push_back(aw);
            }
        }
        result.lines.push_back(currentLine);
    }

    return result;
}

} // namespace vc::suno
