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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "util/Types.hpp"

namespace vc {

/**
 * @brief Bounds-checked conversion from a signed index to a subscript
 *
 * The QML boundary speaks `int` (Q_INVOKABLE parameters, LyricsSyncPosition)
 * while every lyrics container is indexed with `size_t`. Mixing the two by hand
 * is what allowed unguarded subscripts to survive: a negative int becomes a
 * huge size_t, and a size_t close to SIZE_MAX wraps to a small int. Funnel
 * every such conversion through here so the check lives in one place.
 *
 * @param container Any container with size() and operator[](size_t)
 * @param index Signed index, typically from the QML bridge
 * @return The index as a subscript, or std::nullopt when it is negative or
 *         past the end of the container
 */
template <typename Container>
[[nodiscard]] constexpr std::optional<std::size_t> checkedIndex(const Container& container,
                                                                int index) noexcept {
    if (index < 0) {
        return std::nullopt;
    }
    if (static_cast<std::size_t>(index) >= container.size()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(index);
}

/**
 * @brief Represents a single word with timing information
 *
 * Used for word-level karaoke highlighting. Timing is in seconds
 * with float precision for smooth animations.
 */
struct LyricsWord {
  std::string text; ///< The word text
  f32 startTime{0.0f}; ///< Start time in seconds
  f32 endTime{0.0f}; ///< End time in seconds
  f32 confidence{1.0f}; ///< Confidence score (0.0-1.0, for Suno aligned lyrics)
    
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

/**
 * @brief Align flat words to lines using prompt text
 *
 * Splits the prompt into lines and matches words to each line by
 * normalizing and comparing tokens. Words without a prompt match
 * are grouped into estimated-timing lines.
 *
 * @param words  Flat list of timed words (e.g. from Suno JSON)
 * @param prompt Song lyrics text with line breaks
 * @return Lines with words assigned and timing set
 */
std::vector<LyricsLine> alignWordsToLines(const std::vector<LyricsWord>& words,
                                           const std::string& prompt);
} // namespace LyricsFactory

/**
 * @brief Export functions for saving lyrics to various formats
 *
 * SRT and LRC serialization deliberately live *only* in
 * `qml_bridge::LyricsBridge::exportToSrt` / `exportToLrc`. This used to carry
 * a second, byte-for-byte-different pair of formatters with no callers at all;
 * two time formatters drift, and the unused one is the one nothing tests.
 * Bridge is the owner because it is the reachable path -- it is the only thing
 * QML invokes, and it already has to resolve the file path and surface errors.
 *
 * ASS is the exception and lives here, for a reason that is about *who else*
 * needs the bytes rather than about the format. A subtitle track muxed into a
 * recording (see `recorder/VideoRecorderFFmpeg`) is built by the same code that
 * writes the sidecar, and `src/recorder/` must not reach up into
 * `src/qml_bridge/` to get it -- that inverts the dependency and couples a
 * worker thread to a QObject. So the writer sits one layer down, over
 * LyricsData, and both callers use it: the bridge for the file and the string
 * it hands to QML, the recorder for the stream it muxes.
 *
 * SRT and LRC are the two formats with a *single* consumer, so the "put it next
 * to the only thing that calls it" argument still holds for them; what they do
 * share -- the seconds-to-whole-units conversion -- is `toTimeUnits` below, so
 * the primitive is not triplicated either.
 */
namespace LyricsExport {
    /**
     * @brief Export to JSON (for database storage)
     */
    std::string toJson(const LyricsData& lyrics);

    /**
     * @brief Seconds to a whole number of `unitsPerSecond`, clamped at zero
     *
     * The one conversion every time formatter uses. SRT wants milliseconds, LRC
     * and ASS centiseconds, and routing one through the other's scale would
     * quantise twice and change bytes they have already shipped (1.4567s is
     * 1457ms scaled directly but 1460ms via 146cs), so the scale is a parameter
     * rather than being baked in.
     *
     * `f32` rather than an integer type so the multiply stays a float one,
     * bit-identical to what each format wrote before the helper existed -- an
     * integer scale would promote the product to double and quietly move the
     * rounding.
     *
     * The non-finite check is the defect this helper was introduced to fix:
     * `std::llround` on a NaN or infinity is undefined behaviour, and these
     * seconds come from a remote payload, so a malformed or hostile time could
     * take an export down rather than merely mis-render. A non-finite time names
     * no real instant, so it becomes zero -- the same answer the pre-existing
     * clamp already gave a negative one.
     */
    std::int64_t toTimeUnits(f32 seconds, f32 unitsPerSecond);

    /**
     * @brief The one and only Advanced SubStation Alpha assembly path
     *
     * UTF-8, byte for byte what `LyricsBridge::exportToAss` writes and what
     * `LyricsBridge::assDocument` returns. A complete document: the section
     * headers, the styles, and one `Dialogue:` event per LyricsLine with every
     * word behind a `\kf<centiseconds>` tag so a player sweeps the line in
     * time with the audio.
     *
     * The ASS format itself is settled -- probe files were rendered through
     * real libass 0.17.5 to fix the semantics that memory gets wrong: the
     * backslash is deliberately not escaped, `\kf` rather than `\k`, and
     * WrapStyle 0. Do not "correct" any of those without re-rendering.
     *
     * Takes the LyricsData rather than reading a sync engine, so it has no
     * opinion about where lyrics come from and can be reasoned about on its own.
     * A successful document is never empty: it always carries the three section
     * headers, even when every line was blank.
     */
    std::string toAssDocument(const LyricsData& lyrics);

    /**
     * @brief One cue, in the form an AV_CODEC_ID_ASS stream packet carries
     */
    struct AssEvent {
        std::int64_t startCentiseconds{0};
        std::int64_t endCentiseconds{0};
        /**
         * The packet payload: `readorder,layer,` followed by the `Dialogue`
         * line's text-and-effects fields verbatim.
         *
         * That shape is not a convention we get to choose. FFmpeg's own ASS
         * muxer writes exactly this (measured: a one-line `.ass` muxed to Matroska
         * came back as the 53 bytes `0,0,K,,0,0,0,,{\kf100}ka...` with a
         * 2 s duration and pts 0), and its demuxer regenerates it from
         * `libavformat/assdec.c`'s `av_bprintf(dst, "%u,%d,%s", readorder++,
         * layer, p + pos)`. A player that concatenates the track's CodecPrivate
         * with its packets would otherwise be handed a second `Dialogue: 0,`
         * prefix per cue, which is not a script.
         */
        std::string body;
    };

    /**
     * @brief A complete ASS document split the way a subtitle stream needs it
     *
     * The `ass` codec is not a whole-file codec: the *script* (everything that
     * is not a Dialogue event) is the stream's extradata -- Matroska's
     * CodecPrivate -- and each cue is a separate packet. `ff_ass_encoder`'s
     * `ass_encode_init` copies `subtitle_header` into the codec's extradata and
     * `ass_encode_frame` writes only `rects[0]->ass` into the packet, so a
     * document muxed whole would be either a script with no cues or cues with
     * no script.
     */
    struct AssStream {
        std::string header;
        std::vector<AssEvent> events;
    };

    /**
     * @brief Split an ASS document into a stream header and its cues
     *
     * Deliberately the *inverse* of what FFmpeg's ASS demuxer does, so the two
     * agree on every line: a well-formed `Dialogue:` event becomes a cue, and
     * everything else -- section headers, `Format:` lines, styles, `Comment:`
     * lines, and any `Dialogue:` line whose timestamps or field count do not
     * parse -- is retained in the header. A malformed line going into the
     * header rather than into a cue is what `assdec.c` does and is the safe
     * direction: it degrades to a line the renderer ignores rather than to a
     * cue with a garbage timestamp.
     *
     * `readorder` is assigned here, in document order from zero, because the
     * demuxer assigns it in packet order and the two have to match.
     */
    AssStream splitAssStream(const std::string& document);
} // namespace LyricsExport

} // namespace vc
