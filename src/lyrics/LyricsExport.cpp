/**
 * @file LyricsExport.cpp
 * @brief LyricsData serialisation -- database JSON and Advanced SubStation.
 *
 * Part three of three, and the only file here that is a leaf: it reads
 * `LyricsData` and writes bytes, and calls nothing of its own kind. Two
 * consumers exist and neither is QML -- `LyricsBridge` for the sidecar the
 * user saves, and `VideoRecorderFFmpeg` for the subtitle stream it muxes --
 * which is why the writer sits in `src/lyrics/` rather than up in the bridge.
 *
 * `toJson` and the ASS path look like two unrelated things in one file. They
 * are one namespace because they share `toTimeUnits`, the seconds-to-whole-
 * units conversion, and because `toJson` has no other owner -- see the header
 * for why SRT and LRC formatters are deliberately *not* here.
 */

#include "LyricsData.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vc {

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
    return assField(centiseconds / 360000) + ":" + assField((centiseconds / 6000) % 60) + ":" +
           assField((centiseconds / 100) % 60) + "." + assField(centiseconds % 100);
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
