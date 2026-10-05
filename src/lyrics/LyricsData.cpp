/**
 * @file LyricsData.cpp
 * @brief LyricsData timeline lookup methods.
 *
 * Part one of three. The type itself, its word/line accessors and the
 * context-window range are here; `LyricsFactory.cpp` builds instances of it
 * and `LyricsExport.cpp` serialises them. Nothing in this file constructs or
 * serialises anything -- it is the read side only, which is why it carries no
 * Qt include and no regex: a caller looking up "what is playing at t" should
 * not drag in a JSON parser or a subtitle assembler to do it.
 */

#include "LyricsData.hpp"

#include <limits>

namespace vc {

int LyricsData::findLineIndex(f32 time) const {
    if (lines.empty() || time < 0) return -1;

    // Binary search for efficiency
    int left = 0;
    int right = static_cast<int>(lines.size()) - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        const auto& line = lines[mid];

        if (time >= line.startTime && time <= line.endTime) {
            return mid;
        } else if (time < line.startTime) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    // Return closest line before time
    if (right >= 0 && right < static_cast<int>(lines.size())) {
        return right;
    }
    return -1;
}

const LyricsLine* LyricsData::getLine(f32 time) const {
    int idx = findLineIndex(time);
    if (idx >= 0 && idx < static_cast<int>(lines.size())) {
        return &lines[idx];
    }
    return nullptr;
}

const LyricsWord* LyricsData::getWord(size_t lineIndex, f32 time) const {
    if (lineIndex >= lines.size()) return nullptr;

    const auto& line = lines[lineIndex];
    int wordIdx = line.getActiveWordIndex(time);
    if (wordIdx >= 0 && wordIdx < static_cast<int>(line.words.size())) {
        return &line.words[wordIdx];
    }
    return nullptr;
}

std::pair<f32, f32> LyricsData::getTimeRange(size_t lineIndex, size_t contextLines) const {
    if (lines.empty()) return {0.0f, 0.0f};

    // `lineIndex` is caller-supplied unsigned arithmetic, so the old
    // `lineIndex + contextLines + 1` / `endIdx - 1` pair was reachable two ways
    // the compiler cannot see: a sum that wrapped to 0 turned `endIdx - 1` into
    // SIZE_MAX, and a low-end-only clamp left startIdx past the end for any
    // index beyond lines.size() + contextLines. Both indices are therefore
    // saturated against the real bounds here and never wrapped, rather than
    // being wrapped and then defended against afterwards.
    //
    // A song with more lines than an int can address is not reachable through
    // the QML bridge at all, so report the whole song instead of guessing.
    if (lines.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return {lines.front().startTime, lines.back().endTime};
    }

    const size_t last = lines.size() - 1;

    // A line index past the end is a caller mistake, not a range to report.
    // The whole song is the one span that every line legitimately lies in; a
    // degenerate {0, 0} would instead read as "playback is at the start".
    if (lineIndex > last) {
        return {lines.front().startTime, lines.back().endTime};
    }

    const size_t center = lineIndex;
    // `center - contextLines` cannot underflow: the subtraction only happens
    // when center is the larger of the two.
    const size_t startIdx = center > contextLines ? center - contextLines : 0;
    // `endIdx` is inclusive, so it saturates at `last` rather than at size().
    // Comparing against the remaining distance first is what keeps
    // `center + contextLines` from overflowing for a huge contextLines.
    const size_t endIdx = contextLines >= last - center ? last : center + contextLines;

    const auto start = checkedIndex(lines, static_cast<int>(startIdx));
    const auto end = checkedIndex(lines, static_cast<int>(endIdx));
    if (!start || !end) return {0.0f, 0.0f};

    return {lines[*start].startTime, lines[*end].endTime};
}

} // namespace vc
