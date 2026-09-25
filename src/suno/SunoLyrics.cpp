#include "SunoLyrics.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include "core/Logger.hpp"

namespace vc::suno {

namespace {

bool validTiming(f32 start, f32 end) {
    return std::isfinite(start) && std::isfinite(end) && start >= 0.0f && end >= start;
}

std::vector<LyricsWord> collectWords(const LyricsData& data) {
    std::vector<LyricsWord> words;
    for (const auto& line : data.lines) {
        words.insert(words.end(), line.words.begin(), line.words.end());
    }
    return words;
}

bool sameWord(const LyricsWord& lhs, const LyricsWord& rhs) {
    return lhs.text == rhs.text &&
           std::abs(lhs.startTime - rhs.startTime) <= 0.001f &&
           std::abs(lhs.endTime - rhs.endTime) <= 0.001f;
}

bool validCapturedWords(const std::string& json) {
    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (document.isNull()) {
        return false;
    }

    QJsonArray words;
    if (document.isArray()) {
        words = document.array();
    } else if (document.isObject()) {
        const auto object = document.object();
        const auto aligned = object.value(QStringLiteral("aligned_words"));
        if (!aligned.isArray()) {
            return false;
        }
        words = aligned.toArray();
    } else {
        return false;
    }

    if (words.isEmpty()) {
        return false;
    }

    for (const auto& value : words) {
        if (!value.isObject()) {
            return false;
        }
        const auto word = value.toObject();
        if (!word.value(QStringLiteral("word")).isString() ||
            word.value(QStringLiteral("word")).toString().trimmed().isEmpty()) {
            return false;
        }

        const auto start = word.contains(QStringLiteral("start_s"))
                               ? word.value(QStringLiteral("start_s"))
                               : word.value(QStringLiteral("start"));
        const auto end = word.contains(QStringLiteral("end_s"))
                             ? word.value(QStringLiteral("end_s"))
                             : word.value(QStringLiteral("end"));
        if (!start.isDouble() || !end.isDouble()) {
            return false;
        }

        const auto startSeconds = start.toDouble();
        const auto endSeconds = end.toDouble();
        if (!validTiming(static_cast<f32>(startSeconds),
                         static_cast<f32>(endSeconds))) {
            return false;
        }

        if (word.contains(QStringLiteral("p_align")) &&
            !word.value(QStringLiteral("p_align")).isDouble()) {
            return false;
        }
    }
    return true;
}

}

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

std::optional<LyricsData> LyricsAligner::parseCapturedSunoLyrics(
    const std::string& json, const SunoClip& clip) {
    if (json.empty() || !validCapturedWords(json)) {
        return std::nullopt;
    }

    const auto flat = vc::LyricsFactory::fromSunoJson(json, {});
    if (flat.empty()) {
        return std::nullopt;
    }

    auto data = vc::LyricsFactory::fromSunoJson(json, clip.metadata.lyrics);
    if (data.empty()) {
        return std::nullopt;
    }

    const auto sourceWords = collectWords(flat);
    std::vector<LyricsWord> alignedWords;
    bool hasTextLine = false;
    for (const auto& line : data.lines) {
        if (line.isInstrumental) {
            continue;
        }
        if (!line.isSynced || line.words.empty() ||
            !validTiming(line.startTime, line.endTime)) {
            return std::nullopt;
        }
        for (const auto& word : line.words) {
            if (!validTiming(word.startTime, word.endTime) ||
                !std::isfinite(word.confidence)) {
                return std::nullopt;
            }
            alignedWords.push_back(word);
        }
        hasTextLine = true;
    }

    if (!hasTextLine || alignedWords.size() != sourceWords.size()) {
        return std::nullopt;
    }

    for (size_t i = 0; i < alignedWords.size(); ++i) {
        if (!sameWord(alignedWords[i], sourceWords[i])) {
            return std::nullopt;
        }
    }

    data.songId = clip.id;
    data.title = clip.title;
    data.artist = clip.display_name;
    data.source = "suno";
    data.isSynced = true;
    return data;
}

std::optional<LyricsData> LyricsAligner::parseCapturedSunoLyrics(
    const SunoClip& clip, std::string_view json) {
    return parseCapturedSunoLyrics(std::string(json), clip);
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
