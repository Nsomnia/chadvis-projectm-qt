#include "SunoLyrics.hpp"
#include <algorithm>
#include <sstream>
#include <regex>

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
        bool firstWord = true;

        while (lineSs >> promptWord) {
            std::string normPrompt = normalize(promptWord);
            if (normPrompt.empty()) continue;

            int matchIndex = -1;
            
            size_t searchLimit = 50; 
            
            for (size_t w = wordIdx; w < words.size() && w < wordIdx + searchLimit; ++w) {
                std::string audioWord = normalize(words[w].word);
                
                if (audioWord == normPrompt) {
                     matchIndex = static_cast<int>(w);
                     break; 
                }
            }
            
            if (matchIndex != -1) {
                wordIdx = matchIndex;
                if (firstWord) {
                    al.start_s = words[wordIdx].start_s;
                    firstWord = false;
                }
                al.end_s = words[wordIdx].end_s;
                al.words.push_back(words[wordIdx]);
                wordIdx++; 
                break; 
            }
        }

        if (!al.words.empty()) {
            result.lines.push_back(al);
        }
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
