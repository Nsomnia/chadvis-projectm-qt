/**
 * @file LyricsData.cpp
 * @brief Implementation of LyricsData methods and factory functions.
 */

#include "LyricsData.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/Logger.hpp"

namespace vc {

// LyricsData methods

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

// Factory implementations

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
        for (size_t searchStart = wordIdx; searchStart < words.size() && searchStart < wordIdx + 50; ++searchStart) {
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
    if (w.contains("start")) word.startTime = w["start"].toDouble();
    else if (w.contains("start_s")) word.startTime = w["start_s"].toDouble();
    else continue;

    if (w.contains("end")) word.endTime = w["end"].toDouble();
    else if (w.contains("end_s")) word.endTime = w["end_s"].toDouble();
    else continue;

    if (w.contains("score")) word.confidence = w["score"].toDouble();
    else if (w.contains("p_align")) word.confidence = w["p_align"].toDouble();
    else word.confidence = 1.0f;
        
        words.push_back(word);
    }
    
    // Sort by start time
    std::sort(words.begin(), words.end(), 
              [](const LyricsWord& a, const LyricsWord& b) {
                  return a.startTime < b.startTime;
              });
    
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
              [](const LyricsLine& a, const LyricsLine& b) {
                  return a.startTime < b.startTime;
              });
    
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

// Export implementations

namespace LyricsExport {

std::string toJson(const LyricsData& lyrics) {
    QJsonArray array;
    
    for (const auto& line : lyrics.lines) {
        for (const auto& word : line.words) {
            QJsonObject obj;
            obj["word"] = QString::fromStdString(word.text);
            obj["start"] = word.startTime;
            obj["end"] = word.endTime;
            obj["score"] = word.confidence;
            array.append(obj);
        }
    }
    
    QJsonDocument doc(array);
    return doc.toJson(QJsonDocument::Compact).toStdString();
}

// ── ASS ──────────────────────────────────────────────────────────────────────
//
// Everything below operates on UTF-8 *bytes* rather than on code points, which
// is what makes this layer free of Qt. It is also exactly equivalent to the
// QChar-wise version it replaced, and the reason is worth stating because it is
// the whole justification for the port: the four characters the ASS escaper
// cares about -- '\\', '{', '}', '\n' -- and the two the LRC writer cares about,
// '\n' and '\r', are all below 0x80, and a UTF-8 continuation byte is by
// definition >= 0x80. So none of them can occur inside a multi-byte sequence,
// and a byte-wise pass can never split one or invent a match inside one. The
// one place this would *not* hold is a caller passing a non-UTF-8 std::string,
// which the QString version silently repaired into U+FFFD and this one passes
// through unchanged. For ASS that is the better answer -- the format is defined
// as UTF-8 and the muxer wants the raw bytes -- but it is a real difference and
// it is the only one.

namespace {

/// LRC and ASS both count in centiseconds; SRT counts in milliseconds.
constexpr f32 kCentisecondsPerSecond = 100.0f;

/// The prefix that marks a line as a cue rather than as script.
constexpr std::string_view kAssDialoguePrefix = "Dialogue: ";

std::int64_t toAssCentiseconds(const f32 seconds) {
  return LyricsExport::toTimeUnits(seconds, kCentisecondsPerSecond);
}

/// A zero-padded decimal field, minimum width two.
///
/// The same thing `QString::arg(value, 2, 10, '0')` did, including the
/// "minimum": 100 prints as `100` rather than being truncated. An hour count
/// past 99 is not a case anyone hits, and truncating it would be worse.
std::string assField(const std::int64_t value) {
  return std::string(value < 10 ? "0" : "") + std::to_string(value);
}

/// `H:MM:SS.cc` -- ASS counts centiseconds, and unlike LRC it has an hours
/// field, which is the whole reason the two formatters cannot be one function.
///
/// Four placeholders, not three. Qt substitutes the first three, finds no `%4`
/// for the fourth, warns "Argument missing", and returns the string *unchanged*,
/// so the centiseconds were discarded and every event collapsed to `00:00.00`.
std::string formatAssTime(const std::int64_t centiseconds) {
  return assField(centiseconds / 360000) + ":" + assField((centiseconds / 6000) % 60) +
         ":" + assField((centiseconds / 100) % 60) + "." + assField(centiseconds % 100);
}

/// A `[Script Info]` value. Only the one that can be attacker-influenced: title
/// and artist come from a remote payload, and a newline in either would forge a
/// second header key and break the section for every reader.
std::string assHeaderValue(const std::string_view value) {
  std::string cleaned(value);
  for (char& ch : cleaned) {
    if (ch == '\n' || ch == '\r') {
      ch = ' ';
    }
  }
  return cleaned;
}

/// Neutralise the characters that would otherwise break the tag stream.
///
/// `{` and `}` open and close an override block, and a raw newline terminates
/// the Dialogue event -- so an unescaped brace does not just look wrong, it
/// silently eats the brace and everything after it (verified against libass
/// 0.17.5: the text `X{Y` renders as `XY`). Escaping both is the difference
/// between a lyric containing braces and a lyric with a hole in it.
///
/// A backslash is a deliberate pass-through, not an oversight: libass renders
/// `\\` as *two* glyphs, so doubling it is measurably lossy, and a lone one is
/// already literal unless it forms `\N`, `\n` or `\h` -- the irreducible case.
std::string escapeAssText(const std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() + 8);
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        escaped += ch;
        break;
      case '{':
      case '}':
        escaped += '\\';
        escaped += ch;
        break;
      case '\n':
      case '\r':
        escaped += ' ';
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

/// One event's text: the line's words, each behind its own karaoke tag.
///
/// `\kf` rather than `\k`. Word timings carry boundaries and nothing finer, but
/// a karaoke renderer still has to fill *across* the word while it is sung;
/// `\kf` interpolates that fill over the word's own span, where `\k` would make
/// the highlight jump per word. It is also what the application already draws --
/// KaraokeMaster.qml sweeps the line continuously off lineProgress -- so the
/// sidecar, the muxed track and the live view agree.
std::string assEventText(const LyricsLine& line) {
  if (line.words.empty()) {
    // Nothing to sweep, but the words still have to reach the player: a line
    // that was timed without a word list is an untagged caption, not a line to
    // drop.
    return escapeAssText(line.text);
  }

  std::string text;
  for (std::size_t i = 0; i < line.words.size(); ++i) {
    const auto& word = line.words[i];

    // A minimum of one centisecond, because `\kf0` is a degenerate tag that
    // makes renderers flash the syllable rather than show it, and a word whose
    // end precedes its start yields a negative span. The line's own End field is
    // written from the line's span, so widening a duration here cannot push the
    // event past the line.
    const std::int64_t duration = std::max<std::int64_t>(
        1, toAssCentiseconds(word.endTime) - toAssCentiseconds(word.startTime));
    text += "{\\kf";
    text += std::to_string(duration);
    text += "}";

    // Word timings carry bare tokens, so the separator is the writer's
    // responsibility; trimming a token's tail stops a payload that already
    // spaced its words from rendering a double space. The rebuilt sentence is
    // then byte-identical to line.text for any well-formed alignment.
    std::string_view token = word.text;
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
      token.remove_suffix(1);
    }
    text += escapeAssText(token);
    if (i + 1 < line.words.size()) {
      text += ' ';
    }
  }
  return text;
}

/// A run of decimal digits. Six is a sanity cap rather than a format rule: an
/// hour field of six digits is 694 days, so no real timestamp is rejected, while
/// a field that is accidentally a word of lyric cannot be read as a number of
/// hours.
std::optional<std::int64_t> parseAssDigits(const std::string_view digits) {
  if (digits.empty() || digits.size() > 6) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  for (const char ch : digits) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    value = value * 10 + (ch - '0');
  }
  return value;
}

/// `H:MM:SS.cc` to centiseconds, or nullopt if the field is not that shape.
///
/// Stricter than FFmpeg's own ASS demuxer on one point: that one uses `%*c` for
/// the fraction separator, so it accepts `0:00:00,00` as well as `0:00:00.00`.
/// Rejecting the comma form is the safe direction -- it sends the line to the
/// header, where a renderer ignores it, instead of inventing a cue from a field
/// this writer would never produce.
std::optional<std::int64_t> parseAssTimestamp(const std::string_view field) {
  const auto hourSep = field.find(':');
  if (hourSep == std::string_view::npos) {
    return std::nullopt;
  }
  const auto minuteSep = field.find(':', hourSep + 1);
  if (minuteSep == std::string_view::npos) {
    return std::nullopt;
  }
  const auto fractionSep = field.find('.', minuteSep + 1);
  if (fractionSep == std::string_view::npos) {
    return std::nullopt;
  }
  // One decimal point, and nothing after it but digits.
  if (field.find('.', fractionSep + 1) != std::string_view::npos) {
    return std::nullopt;
  }

  const auto hours = parseAssDigits(field.substr(0, hourSep));
  const auto minutes = parseAssDigits(field.substr(hourSep + 1, minuteSep - hourSep - 1));
  const auto seconds = parseAssDigits(field.substr(minuteSep + 1, fractionSep - minuteSep - 1));
  const auto centis = parseAssDigits(field.substr(fractionSep + 1));
  if (!hours || !minutes || !seconds || !centis) {
    return std::nullopt;
  }
  return ((*hours * 60 + *minutes) * 60 + *seconds) * 100 + *centis;
}

/// One line as a cue, or nullopt to keep it in the header.
///
/// The nullopt cases are the ones `libavformat/assdec.c`'s `read_dialogue` also
/// declines: a line that is not a Dialogue at all, one whose field count or
/// timestamps do not parse, and one whose duration is not positive. The last is
/// deliberate on FFmpeg's part and worth keeping -- "we cannot output such
/// events as actual packets since this would cause their duration to be guessed
/// by later processing like compute_pkt_fields()". A packet with a zero
/// duration is worse than a line the renderer skips.
std::optional<LyricsExport::AssEvent> parseAssEventLine(const std::string_view line,
                                                        std::uint32_t& readOrder) {
  if (!line.starts_with(kAssDialoguePrefix)) {
    return std::nullopt;
  }

  // Fields after the prefix: layer, start, end, then text-and-effects.
  const std::string_view fields = line.substr(kAssDialoguePrefix.size());
  const auto afterLayer = fields.find(',');
  if (afterLayer == std::string_view::npos) {
    return std::nullopt;
  }
  const auto afterStart = fields.find(',', afterLayer + 1);
  if (afterStart == std::string_view::npos) {
    return std::nullopt;
  }
  const auto afterEnd = fields.find(',', afterStart + 1);
  if (afterEnd == std::string_view::npos) {
    return std::nullopt;
  }

  const auto start =
      parseAssTimestamp(fields.substr(afterLayer + 1, afterStart - afterLayer - 1));
  const auto end = parseAssTimestamp(fields.substr(afterStart + 1, afterEnd - afterStart - 1));
  if (!start || !end || *end <= *start) {
    return std::nullopt;
  }

  LyricsExport::AssEvent event;
  event.startCentiseconds = *start;
  event.endCentiseconds = *end;
  event.body.reserve(fields.size() + 16);
  event.body = std::to_string(readOrder);
  event.body += ',';
  // The layer is echoed verbatim rather than re-rendered from a parsed integer:
  // assdec.c reads it with atoi, so `0` and `00` name the same event to a
  // player, and copying the source keeps the round trip exact.
  event.body += fields.substr(0, afterLayer);
  event.body += ',';
  event.body += fields.substr(afterEnd + 1);
  ++readOrder;
  return event;
}

} // namespace

std::int64_t toTimeUnits(const f32 seconds, const f32 unitsPerSecond) {
  if (!std::isfinite(seconds)) {
    return 0;
  }
  return std::max<std::int64_t>(0, std::llround(seconds * unitsPerSecond));
}

std::string toAssDocument(const LyricsData& lyrics) {
  std::string output = "[Script Info]\n";
  if (!lyrics.title.empty()) {
    output += "Title: ";
    output += assHeaderValue(lyrics.title);
    output += '\n';
  }

  // PlayResX/Y are the script's coordinate space, and they are 1920x1080
  // because that is what the recorder encodes by default: a subtitle authored
  // in the script resolution of the video it will be muxed into needs no
  // rescale, so Alignment 2 and MarginV mean the same thing here as they do on
  // the finished frame. Any other choice would be a guess that a player
  // silently corrects by scaling, which is how a bottom-centre caption ends up
  // somewhere else entirely.
  //
  // WrapStyle 0 is the only value that degrades safely. A lyric line longer
  // than the script width is expected -- the capture makes no attempt to wrap
  // prose -- and style 0 (smart wrapping) folds it back onto the frame, whereas
  // 1 and 2 break only on an explicit \N and simply run off the edge with the
  // text unreadable (verified against libass 0.17.5 at PlayResX 640: style 0
  // reflowed onto five lines, style 2 clipped both ends).
  //
  // The two Format lines are the field lists, in the exact order the spec fixes.
  // Order is not cosmetic: a renderer reads these positionally, so a transposed
  // field does not error, it just puts Outline where Alignment belongs and the
  // file plays with garbage geometry.
  output += "ScriptType: v4.00+\n"
            "WrapStyle: 0\n"
            "PlayResX: 1920\n"
            "PlayResY: 1080\n"
            "ScaledBorderAndShadow: yes\n"
            "\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
            "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
            "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
            "MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,60,&H00FFFFFF,&H00808080,&H00000000,&H00000000,"
            "0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
            "\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

  for (const auto& line : lyrics.lines) {
    if (line.text.empty()) {
      continue;
    }

    // The same widening the SRT writer applies, so a line whose endTime
    // precedes its startTime is never written backwards. The floor is stricter
    // than SRT's: a Dialogue whose End <= Start is discarded outright by most
    // players -- and by splitAssStream, which routes it to the header -- so
    // without it a sub-centisecond line would not merely look wrong, it would
    // vanish.
    const std::int64_t startCs = toAssCentiseconds(line.startTime);
    const std::int64_t endCs =
        line.endTime > line.startTime ? toAssCentiseconds(line.endTime) : startCs + 100;
    output += "Dialogue: 0,";
    output += formatAssTime(startCs);
    output += ',';
    output += formatAssTime(std::max(startCs + 1, endCs));
    output += ",Default,,0,0,0,,";
    output += assEventText(line);
    output += '\n';
  }

  return output;
}

AssStream splitAssStream(const std::string& document) {
  AssStream stream;
  // uint32_t because that is the width of FFmpeg's own counter
  // (libavformat/assdec.c's `unsigned readorder`); the two have to agree for a
  // cue's payload to survive a mux/demux round trip.
  std::uint32_t readOrder = 0;

  std::string_view rest(document);
  while (!rest.empty()) {
    const auto newline = rest.find('\n');
    // A document that does not end in a newline still contributes its last
    // line; consuming the empty remainder is what stops this looping forever.
    std::string_view line = rest.substr(0, newline);
    rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    if (auto event = parseAssEventLine(line, readOrder)) {
      stream.events.push_back(std::move(*event));
      continue;
    }
    stream.header += line;
    stream.header += '\n';
  }

  return stream;
}

} // namespace LyricsExport

} // namespace vc
