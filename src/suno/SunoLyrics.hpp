#pragma once
// SunoLyrics.hpp - Structures for synced lyrics
// Word-level timestamps from Suno AI
//
// AlignedWord/AlignedLine/AlignedLyrics are Suno-specific view types
// that delegate parsing to the canonical LyricsFactory (src/lyrics/LyricsData.hpp).
// Conversion helpers allow interop between the two representations.

#include <string>
#include <vector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "lyrics/LyricsData.hpp"
#include "util/Types.hpp"

namespace vc::suno {

struct AlignedWord {
  std::string word;
  f32 start_s;
  f32 end_s;
  f32 score; // p_align

  // Conversion from canonical LyricsWord
  static AlignedWord fromLyricsWord(const vc::LyricsWord& w) {
    return {w.text, w.startTime, w.endTime, w.confidence};
  }
  // Conversion to canonical LyricsWord
  vc::LyricsWord toLyricsWord() const {
    return {word, start_s, end_s, score};
  }
};

struct AlignedLine {
  std::string text;
  f32 start_s;
  f32 end_s;
  std::vector<AlignedWord> words;

  // Conversion from canonical LyricsLine
  static AlignedLine fromLyricsLine(const vc::LyricsLine& l) {
    AlignedLine al;
    al.text = l.text;
    al.start_s = l.startTime;
    al.end_s = l.endTime;
    for (const auto& w : l.words) al.words.push_back(AlignedWord::fromLyricsWord(w));
    return al;
  }
  // Conversion to canonical LyricsLine
  vc::LyricsLine toLyricsLine() const {
    vc::LyricsLine l;
    l.text = text;
    l.startTime = start_s;
    l.endTime = end_s;
    l.isSynced = true;
    for (const auto& w : words) l.words.push_back(w.toLyricsWord());
    return l;
  }
};

struct AlignedLyrics {
  std::vector<AlignedWord> words;
  std::vector<AlignedLine> lines;
  std::string songId;

  bool empty() const {
    return words.empty() && lines.empty();
  }

  // Conversion from canonical LyricsData
  static AlignedLyrics fromLyricsData(const vc::LyricsData& data) {
    AlignedLyrics result;
    result.songId = data.songId;
    for (const auto& line : data.lines) {
      AlignedLine al = AlignedLine::fromLyricsLine(line);
      result.lines.push_back(al);
      for (const auto& w : al.words) result.words.push_back(w);
    }
    return result;
  }
  // Conversion to canonical LyricsData
  vc::LyricsData toLyricsData() const {
    vc::LyricsData data;
    data.songId = songId;
    data.isSynced = true;
    for (const auto& line : lines) {
      data.lines.push_back(line.toLyricsLine());
    }
    return data;
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
