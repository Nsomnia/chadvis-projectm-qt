#include "SunoLyrics.hpp"
#include <algorithm>
#include <sstream>
#include "core/Logger.hpp"

namespace vc::suno {

// ============================================================================
// LyricsAligner - Delegates to canonical LyricsFactory (src/lyrics/LyricsData)
// ============================================================================

std::vector<AlignedWord> LyricsAligner::parseJson(const QByteArray& json, f32 duration) {
	// Delegate entirely to canonical LyricsFactory
	vc::LyricsData data = vc::LyricsFactory::fromSunoJson(json.toStdString());

	std::vector<AlignedWord> result;
	for (const auto& line : data.lines) {
		for (const auto& w : line.words) {
			result.push_back(AlignedWord::fromLyricsWord(w));
		}
	}

	return result;
}

std::vector<AlignedWord> LyricsAligner::estimateTimings(const std::string& text, f32 duration) {
  // Delegate to LyricsFactory::fromText + manual word splitting
  std::vector<AlignedWord> result;
  if (text.empty()) return result;

  if (duration <= 0.0f) duration = 180.0f;

  std::stringstream ss(text);
  std::string wordStr;
  std::vector<std::string> words;

  while (ss >> wordStr) {
    if (wordStr.size() >= 2 && wordStr.front() == '[' && wordStr.back() == ']') {
      continue;
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

AlignedLyrics LyricsAligner::align(const std::string& prompt, const std::vector<AlignedWord>& words) {
	// Convert to canonical types
	std::vector<vc::LyricsWord> lyricsWords;
	for (const auto& w : words) {
		lyricsWords.push_back(w.toLyricsWord());
	}

	// Use canonical alignWordsToLines directly — no JSON round-trip
	auto lines = vc::LyricsFactory::alignWordsToLines(lyricsWords, prompt);

	// Build AlignedLyrics from the result
	AlignedLyrics result;
	for (const auto& line : lines) {
		result.lines.push_back(AlignedLine::fromLyricsLine(line));
		for (const auto& w : line.words) {
			result.words.push_back(AlignedWord::fromLyricsWord(w));
		}
	}

	return result;
}

AlignedLyrics LyricsAligner::parseLrc(const std::string& content) {
  vc::LyricsData data = vc::LyricsFactory::fromLrc(content);
  return AlignedLyrics::fromLyricsData(data);
}

AlignedLyrics LyricsAligner::parseSrt(const std::string& content) {
  vc::LyricsData data = vc::LyricsFactory::fromSrt(content);
  return AlignedLyrics::fromLyricsData(data);
}

} // namespace vc::suno
