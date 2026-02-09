/**
 * @file LyricsData.hpp
 * @brief Core data structures for lyrics system.
 *
 * Clean, simple data structures for representing lyrics with timing info.
 * Designed for 60fps karaoke rendering and efficient synchronization.
 *
 * @author ChadVis Agent
 * @version 11.0 (Balls to the Wall Edition)
 *
 * I use Arch btw.
 */

#pragma once
#include <string>
#include <vector>
#include "util/Types.hpp"

namespace vc {

/**
 * @brief Represents a single word with timing information
 *
 * Used for word-level karaoke highlighting. Timing is in seconds
 * with float precision for smooth animations.
 */
struct LyricsWord {
    std::string text;           ///< The word text
    f32 startTime{0.0f};        ///< Start time in seconds
    f32 endTime{0.0f};          ///< End time in seconds
    f32 confidence{1.0f};       ///< Confidence score (0.0-1.0, for Suno aligned lyrics)
    
    /**
     * @brief Check if a given time falls within this word's duration
     * @param time Current playback time in seconds
     * @return true if time is within [startTime, endTime]
     */
    bool containsTime(f32 time) const {
        return time >= startTime && time <= endTime;
    }
    
    /**
     * @brief Get progress through this word (0.0 = start, 1.0 = end)
     * @param time Current playback time in seconds
     * @return Progress ratio clamped to [0.0, 1.0]
     */
    f32 getProgress(f32 time) const {
        if (time <= startTime) return 0.0f;
        if (time >= endTime) return 1.0f;
        if (endTime <= startTime) return 0.0f;
        return (time - startTime) / (endTime - startTime);
    }
};

/**
 * @brief Represents a line of lyrics with word breakdown
 *
 * Lines are the primary display unit, but words enable
 * precise karaoke-style highlighting.
 */
struct LyricsLine {
    std::string text;                   ///< Full line text
    f32 startTime{0.0f};                ///< Line start time
    f32 endTime{0.0f};                  ///< Line end time
    std::vector<LyricsWord> words;      ///< Word-level timing (may be empty for unsynced)
    bool isInstrumental{false};         ///< True if this is an instrumental break
    bool isSynced{false};               ///< True if timing data is available
    
    /**
     * @brief Check if a given time falls within this line's duration
     */
    bool containsTime(f32 time) const {
        return time >= startTime && time <= endTime;
    }
    
    /**
     * @brief Get the active word at a given time
     * @return Index of active word, or -1 if none
     */
    int getActiveWordIndex(f32 time) const {
        for (size_t i = 0; i < words.size(); ++i) {
            if (words[i].containsTime(time)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

/**
 * @brief Complete lyrics data for a song
 *
 * This is the primary data structure passed between
 * loader, sync engine, and renderers.
 */
class LyricsData {
public:
    std::vector<LyricsLine> lines;      ///< All lyric lines
    std::string source;                 ///< Source type: "suno", "srt", "lrc", "txt"
    std::string songId;                 ///< Unique song identifier
    std::string title;                  ///< Song title
    std::string artist;                 ///< Artist name
    bool isSynced{false};               ///< True if any timing data exists
    f32 duration{0.0f};                 ///< Total song duration in seconds
    
    /**
     * @brief Check if lyrics are empty
     */
    bool empty() const {
        return lines.empty();
    }
    
    /**
     * @brief Get total line count
     */
    size_t lineCount() const {
        return lines.size();
    }
    
    /**
     * @brief Find the active line index for a given time
     * @param time Current playback time in seconds
     * @return Line index, or -1 if before first line or after last
     */
    int findLineIndex(f32 time) const;
    
    /**
     * @brief Get the active line for a given time
     * @return Pointer to line, or nullptr if none active
     */
    const LyricsLine* getLine(f32 time) const;
    
    /**
     * @brief Get the active word in a specific line
     * @param lineIndex Line to check
     * @param time Current playback time
     * @return Pointer to word, or nullptr if none active
     */
    const LyricsWord* getWord(size_t lineIndex, f32 time) const;
    
    /**
     * @brief Search for text within lyrics
     * @param query Search string (case-insensitive)
     * @return Vector of line indices containing the query
     */
    std::vector<size_t> search(const std::string& query) const;
    
    /**
     * @brief Get time range for a specific line and surrounding context
     * @param lineIndex Center line
     * @param contextLines Number of lines before/after to include
     * @return Pair of (startTime, endTime) for the range
     */
    std::pair<f32, f32> getTimeRange(size_t lineIndex, size_t contextLines = 2) const;
};

/**
 * @brief Factory functions for creating LyricsData from various formats
 */
namespace LyricsFactory {
    /**
     * @brief Create from Suno aligned lyrics JSON
     */
    LyricsData fromSunoJson(const std::string& json, const std::string& prompt = "");
    
    /**
     * @brief Create from SRT subtitle format
     */
    LyricsData fromSrt(const std::string& content);
    
    /**
     * @brief Create from LRC lyrics format
     */
    LyricsData fromLrc(const std::string& content);
    
    /**
     * @brief Create from plain text (no timing)
     */
    LyricsData fromText(const std::string& text);
    
    /**
     * @brief Create from database JSON storage
     */
    LyricsData fromDatabase(const std::string& json);
} // namespace LyricsFactory

/**
 * @brief Export functions for saving lyrics to various formats
 */
namespace LyricsExport {
    /**
     * @brief Export to SRT format
     */
    std::string toSrt(const LyricsData& lyrics);
    
    /**
     * @brief Export to LRC format
     */
    std::string toLrc(const LyricsData& lyrics);
    
    /**
     * @brief Export to JSON (for database storage)
     */
    std::string toJson(const LyricsData& lyrics);
} // namespace LyricsExport

} // namespace vc
