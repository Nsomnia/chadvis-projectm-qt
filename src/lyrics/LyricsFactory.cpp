/**
 * @file LyricsFactory.cpp
 * @brief LyricsData construction from the wire formats we actually receive.
 *
 * Part two of three. Every function here answers one question -- how do I turn
 * this payload into a `LyricsData`? -- and the four of them that parse
 * (Suno JSON, SRT, LRC, plain text) are independent of each other apart from
 * `alignWordsToLines`, which is the one piece `fromSunoJson` shares with
 * nothing else and is therefore the only free function in this namespace that
 * the header has to expose.
 *
 * This is the only one of the three files that is Qt-dependent for parsing
 * rather than for convenience: `QJsonDocument` is what Suno's aligned-lyrics
 * payload is, and hand-rolling that would buy nothing.
 */

#include "LyricsData.hpp"

#include "core/Logger.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>
#include <sstream>
#include <string>

namespace vc {

namespace LyricsFactory {

std::vector<LyricsLine> alignWordsToLines(const std::vector<LyricsWord>& words,
                                          const std::string& prompt) {
    std::vector<LyricsLine> lines;

    std::istringstream stream(prompt);
    std::string lineText;
    size_t wordIdx = 0;

    while (std::getline(stream, lineText)) {
        // Trim
        lineText.erase(0, lineText.find_first_not_of(" \t\r\n"));
        lineText.erase(lineText.find_last_not_of(" \t\r\n") + 1);

        if (lineText.empty()) continue;

        // Skip section tags like [Verse], [Chorus]
        if (lineText.front() == '[' && lineText.back() == ']') {
            LyricsLine line;
            line.text = lineText;
            line.isInstrumental = true;
            lines.push_back(line);
            continue;
        }

        LyricsLine line;
        line.text = lineText;
        line.isSynced = true;

        // Tokenize line to match words
        std::istringstream lineStream(lineText);
        std::string token;
        std::vector<std::string> tokens;
        while (lineStream >> token) {
            // Normalize token
            std::string norm;
            for (char c : token) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    norm += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            if (!norm.empty()) tokens.push_back(norm);
        }

        if (tokens.empty()) continue;

        // Find matching words
        bool foundMatch = false;
        for (size_t searchStart = wordIdx; searchStart < words.size() && searchStart < wordIdx + 50;
             ++searchStart) {
            // Normalize word
            std::string wordNorm;
            for (char c : words[searchStart].text) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    wordNorm += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }

            if (wordNorm == tokens[0]) {
                // Found first word match
                line.startTime = words[searchStart].startTime;
                wordIdx = searchStart;
                foundMatch = true;

                // Add words to line
                for (size_t t = 0; t < tokens.size() && wordIdx < words.size(); ++t) {
                    line.words.push_back(words[wordIdx]);
                    line.endTime = words[wordIdx].endTime;
                    ++wordIdx;
                }
                break;
            }
        }

        if (!foundMatch) {
            // No match - use estimated timing
            if (!lines.empty()) {
                line.startTime = lines.back().endTime;
                line.endTime = line.startTime + 3.0f; // Estimate 3 seconds
            }
        }

        lines.push_back(line);
    }

    return lines;
}

LyricsData fromSunoJson(const std::string& json, const std::string& prompt) {
    LyricsData data;
    data.source = "suno";

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (doc.isNull()) {
        LOG_WARN("LyricsFactory: Failed to parse Suno JSON");
        return data;
    }

    // Extract words from JSON
    std::vector<LyricsWord> words;
    QJsonArray wordArray;

    if (doc.isArray()) {
        wordArray = doc.array();
    } else if (doc.isObject()) {
        QJsonObject obj = doc.object();
        // Try various keys that Suno might use
        QStringList keys = {"aligned_words", "alligned_words", "words", "lyrics"};
        for (const auto& key : keys) {
            if (obj.contains(key) && obj[key].isArray()) {
                wordArray = obj[key].toArray();
                break;
            }
        }
    }

    // Recursive Deep Search for any array containing words
    // Handles nested objects and non-standard JSON structures from Suno API
    if (wordArray.isEmpty() && doc.isObject()) {
        std::function<QJsonArray(const QJsonObject&)> findArray;

        findArray = [&](const QJsonObject& obj) -> QJsonArray {
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (it.value().isArray()) {
                    QJsonArray arr = it.value().toArray();
                    if (!arr.isEmpty() && arr[0].isObject()) {
                        QJsonObject first = arr[0].toObject();
                        // Check for word-like structure (duck typing)
                        if (first.contains("word") &&
                            (first.contains("start") || first.contains("start_s"))) {
                            return arr;
                        }
                    }
                } else if (it.value().isObject()) {
                    QJsonArray res = findArray(it.value().toObject());
                    if (!res.isEmpty()) return res;
                }
            }
            return QJsonArray();
        };

        QJsonArray deepResult = findArray(doc.object());
        if (!deepResult.isEmpty()) {
            wordArray = deepResult;
        }
    }

    // Parse words
    for (const auto& val : wordArray) {
        if (!val.isObject()) continue;
        QJsonObject w = val.toObject();

        LyricsWord word;
        word.text = w["word"].toString().toStdString();

        // Clean up Suno's formatting
        static const std::regex bracketRegex(R"(\[[^\]]*\])");
        word.text = std::regex_replace(word.text, bracketRegex, "");

        // Trim whitespace
        word.text.erase(0, word.text.find_first_not_of(" \t\r\n"));
        word.text.erase(word.text.find_last_not_of(" \t\r\n") + 1);

        if (word.text.empty()) {
            // If the word was JUST a tag (e.g. "[Verse]", "[Instrumental]"), check for instrumental
            std::string lowerRaw = w["word"].toString().toStdString();
            std::transform(lowerRaw.begin(), lowerRaw.end(), lowerRaw.begin(), ::tolower);
            if (lowerRaw.find("instrumental") != std::string::npos) {
                word.text = "\xF0\x9F\x8E\xB5"; // 🎵 placeholder for instrumental sections
            } else {
                continue; // Skip other tags like [Verse], [Chorus]
            }
        }

        // Parse timing (strict: skip words without timestamps)
        if (w.contains("start"))
            word.startTime = w["start"].toDouble();
        else if (w.contains("start_s"))
            word.startTime = w["start_s"].toDouble();
        else
            continue;

        if (w.contains("end"))
            word.endTime = w["end"].toDouble();
        else if (w.contains("end_s"))
            word.endTime = w["end_s"].toDouble();
        else
            continue;

        if (w.contains("score"))
            word.confidence = w["score"].toDouble();
        else if (w.contains("p_align"))
            word.confidence = w["p_align"].toDouble();
        else
            word.confidence = 1.0f;

        words.push_back(word);
    }

    // Sort by start time
    std::sort(words.begin(), words.end(),
              [](const LyricsWord& a, const LyricsWord& b) { return a.startTime < b.startTime; });

    // Align words to lines using prompt
    if (!prompt.empty() && !words.empty()) {
        data.lines = alignWordsToLines(words, prompt);
        data.isSynced = true;
    } else if (!words.empty()) {
        // No prompt - create single line with all words
        LyricsLine line;
        line.text = "";
        for (const auto& w : words) {
            if (!line.text.empty()) line.text += " ";
            line.text += w.text;
        }
        line.words = words;
        if (!words.empty()) {
            line.startTime = words.front().startTime;
            line.endTime = words.back().endTime;
        }
        line.isSynced = true;
        data.lines.push_back(line);
        data.isSynced = true;
    }

    if (words.empty()) {
        LOG_WARN("LyricsFactory: Parsed Suno JSON but found no valid words");
    }

    return data;
}

LyricsData fromSrt(const std::string& content) {
    LyricsData data;
    data.source = "srt";
    data.isSynced = true;

    std::istringstream stream(content);
    std::string line;

    // SRT format: index, time line, text lines, blank line
    std::regex timeRegex(R"((\d+):(\d+):(\d+)[,\.](\d+)\s+-->\s+(\d+):(\d+):(\d+)[,\.](\d+))");

    LyricsLine currentLine;
    bool inEntry = false;

    auto parseTime = [](const std::smatch& m, int offset) -> f32 {
        int h = std::stoi(m[offset + 1]);
        int mn = std::stoi(m[offset + 2]);
        int s = std::stoi(m[offset + 3]);
        int ms = std::stoi(m[offset + 4]);
        return h * 3600.0f + mn * 60.0f + s + ms / 1000.0f;
    };

    while (std::getline(stream, line)) {
        // Remove carriage return
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::smatch match;
        if (std::regex_search(line, match, timeRegex)) {
            // New entry
            if (inEntry && !currentLine.text.empty()) {
                data.lines.push_back(currentLine);
            }

            currentLine = LyricsLine();
            currentLine.startTime = parseTime(match, 0);
            currentLine.endTime = parseTime(match, 4);
            currentLine.isSynced = true;
            inEntry = true;

            // Check for remaining text on same line
            std::string remaining = match.suffix();
            if (!remaining.empty()) {
                currentLine.text = remaining;
            }
        } else if (inEntry && !line.empty()) {
            // Text line
            if (!currentLine.text.empty()) currentLine.text += " ";
            currentLine.text += line;
        } else if (line.empty() && inEntry) {
            // End of entry
            if (!currentLine.text.empty()) {
                data.lines.push_back(currentLine);
            }
            inEntry = false;
        }
    }

    // Don't forget last entry
    if (inEntry && !currentLine.text.empty()) {
        data.lines.push_back(currentLine);
    }

    return data;
}

LyricsData fromLrc(const std::string& content) {
    LyricsData data;
    data.source = "lrc";
    data.isSynced = true;

    std::istringstream stream(content);
    std::string line;

    // LRC format: [mm:ss.xx]text
    std::regex timeRegex(R"(\[(\d+):(\d+(?:\.\d+)?)\])");

    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, timeRegex)) {
            int min = std::stoi(match[1]);
            float sec = std::stof(match[2]);

            LyricsLine lrcLine;
            lrcLine.startTime = min * 60.0f + sec;
            lrcLine.text = match.suffix();
            lrcLine.isSynced = true;

            // Trim
            lrcLine.text.erase(0, lrcLine.text.find_first_not_of(" \t\r\n"));
            lrcLine.text.erase(lrcLine.text.find_last_not_of(" \t\r\n") + 1);

            if (!lrcLine.text.empty()) {
                data.lines.push_back(lrcLine);
            }
        }
    }

    // Sort by time
    std::sort(data.lines.begin(), data.lines.end(),
              [](const LyricsLine& a, const LyricsLine& b) { return a.startTime < b.startTime; });

    // Set end times based on next line
    for (size_t i = 0; i < data.lines.size(); ++i) {
        if (i + 1 < data.lines.size()) {
            data.lines[i].endTime = data.lines[i + 1].startTime;
        } else {
            data.lines[i].endTime = data.lines[i].startTime + 5.0f;
        }
    }

    return data;
}

LyricsData fromText(const std::string& text) {
    LyricsData data;
    data.source = "txt";
    data.isSynced = false;

    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        // Trim
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (!line.empty()) {
            LyricsLine lyricsLine;
            lyricsLine.text = line;
            lyricsLine.isSynced = false;
            data.lines.push_back(lyricsLine);
        }
    }

    return data;
}

LyricsData fromDatabase(const std::string& json) {
    // Database stores in same format as Suno JSON
    return fromSunoJson(json, "");
}

} // namespace LyricsFactory

} // namespace vc
