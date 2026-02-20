#include "SunoLyricsManager.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoLyrics.hpp"
#include "util/FileUtils.hpp"

#include <QByteArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <regex>
#include <fstream>

namespace vc::suno {

SunoLyricsManager::SunoLyricsManager(SunoClient* client, SunoDatabase& db,
                                     AudioEngine* audioEngine, OverlayEngine* overlayEngine)
    : client_(client)
    , db_(db)
    , audioEngine_(audioEngine)
    , overlayEngine_(overlayEngine) {}

Result<AlignedLyrics> SunoLyricsManager::getLyrics(const std::string& clipId) {
    auto jsonRes = db_.getAlignedLyrics(clipId);
    if (!jsonRes.isOk()) {
        return Result<AlignedLyrics>::err("No lyrics found");
    }

    std::string json = jsonRes.value();
    std::string prompt;
    f32 duration = 0.0f;

    if (clipsRef_) {
        for (const auto& clip : *clipsRef_) {
            if (clip.id == clipId) {
                prompt = clip.metadata.prompt;
                auto durOpt = file::parseDuration(clip.metadata.duration);
                if (durOpt) duration = durOpt->count() / 1000.0f;
                break;
            }
        }
    }

    if (prompt.empty()) {
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
            prompt = clipOpt.value()->metadata.prompt;
            auto durOpt = file::parseDuration(clipOpt.value()->metadata.duration);
            if (durOpt) duration = durOpt->count() / 1000.0f;
        }
    }

    if (prompt.empty()) {
        return Result<AlignedLyrics>::err("Prompt not found for alignment");
    }

    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json), duration);
    
    if (words.empty()) {
        LOG_WARN("SunoLyricsManager: JSON parsed empty, falling back to estimated timings");
        words = LyricsAligner::estimateTimings(prompt, duration);
    }

    if (words.empty()) {
        return Result<AlignedLyrics>::err("Failed to parse words from JSON and fallback failed");
    }

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    return Result<AlignedLyrics>::ok(lyrics);
}

void SunoLyricsManager::cacheLyrics(const std::string& clipId, const AlignedLyrics& lyrics) {
    directLyricsCache_[clipId] = lyrics;
}

void SunoLyricsManager::onTrackChanged() {
    if (debugMode_) {
        LOG_DEBUG("SunoLyricsManager: Ignoring track change (debug mode)");
        return;
    }

    auto item = audioEngine_->playlist().currentItem();
    if (!item) {
        overlayEngine_->setAlignedLyrics({});
        return;
    }

    std::string clipId = extractClipIdFromTrack();
    
    if (clipId.empty() && !lastRequestedClipId_.empty()) {
        LOG_DEBUG("SunoLyricsManager: Using last requested ID {} as fallback", lastRequestedClipId_);
        clipId = lastRequestedClipId_;
    }
    
    if (!clipId.empty()) {
        lastRequestedClipId_ = clipId;
    }

    if (clipId.empty()) {
        LOG_DEBUG("SunoLyricsManager: Could not extract clip ID, clearing lyrics");
        overlayEngine_->setAlignedLyrics({});
        return;
    }

    LOG_INFO("SunoLyricsManager: Track changed, looking up lyrics for {}", clipId);

    auto cacheIt = directLyricsCache_.find(clipId);
    if (cacheIt != directLyricsCache_.end()) {
        LOG_INFO("SunoLyricsManager: Using cached lyrics for {}", clipId);
        overlayEngine_->setAlignedLyrics(cacheIt->second);
        return;
    }

    auto res = getLyrics(clipId);
    if (res.isOk()) {
        LOG_INFO("SunoLyricsManager: Displaying lyrics from database for {}", clipId);
        overlayEngine_->setAlignedLyrics(res.value());
        directLyricsCache_[clipId] = res.value();
        return;
    }

    fs::path trackPath = item->isRemote ? fs::path() : item->path;
    if (!trackPath.empty()) {
        fs::path dir = trackPath.parent_path();
        std::string stem = trackPath.stem().string();
        
        fs::path srtPath = dir / (stem + ".srt");
        if (fs::exists(srtPath)) {
            std::ifstream file(srtPath);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                auto lyrics = LyricsAligner::parseSrt(content);
                if (!lyrics.empty()) {
                    LOG_INFO("SunoLyricsManager: Loaded lyrics from SRT for {}", clipId);
                    lyrics.songId = clipId;
                    overlayEngine_->setAlignedLyrics(lyrics);
                    directLyricsCache_[clipId] = lyrics;
                    return;
                }
            }
        }
        
        fs::path jsonPath = dir / (stem + ".json");
        if (fs::exists(jsonPath)) {
            std::ifstream file(jsonPath);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                auto words = LyricsAligner::parseJson(QByteArray::fromStdString(content));
                if (!words.empty()) {
                    LOG_INFO("SunoLyricsManager: Loaded lyrics from JSON for {}", clipId);
                    AlignedLyrics lyrics;
                    lyrics.songId = clipId;
                    lyrics.words = words;
                    AlignedLine line;
                    line.start_s = words.front().start_s;
                    line.end_s = words.back().end_s;
                    for (const auto& w : words) line.text += w.word + " ";
                    lyrics.lines.push_back(line);
                    overlayEngine_->setAlignedLyrics(lyrics);
                    directLyricsCache_[clipId] = lyrics;
                    return;
                }
            }
        }
    }

    if (client_->isAuthenticated()) {
        LOG_INFO("SunoLyricsManager: Fetching lyrics from API for {}", clipId);
        client_->fetchAlignedLyrics(clipId);
    } else {
        LOG_DEBUG("SunoLyricsManager: No lyrics for {} and not authenticated", clipId);
    }
    
    overlayEngine_->setAlignedLyrics({});
}

void SunoLyricsManager::onLyricsFetched(const std::string& clipId, const std::string& json) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    bool isCurrent = isCurrentlyPlaying(clipId);
    
    if (isCurrent && !debugMode_) {
        auto lyricsOpt = parseAndDisplayLyrics(clipId, json, doc);
        if (lyricsOpt) {
            directLyricsCache_[clipId] = *lyricsOpt;
            LOG_INFO("SunoLyricsManager: Displayed lyrics for current track {}", clipId);
        }
    }
    
    if (CONFIG.suno().saveLyrics) {
        saveLyricsSidecar(clipId, json, doc);
    }
    
    lyricsReady.emitSignal(clipId);
}

bool SunoLyricsManager::isCurrentlyPlaying(const std::string& clipId) const {
    if (auto item = audioEngine_->playlist().currentItem()) {
        if (item->isRemote) {
            if (item->url.find(clipId) != std::string::npos) return true;
        } else {
            if (item->path.string().find(clipId) != std::string::npos) return true;
            if (item->title().find(clipId) != std::string::npos) return true;
            
            if (clipsRef_) {
                for (const auto& clip : *clipsRef_) {
                    if (clip.id == clipId) {
                        std::string itemTitle = item->title();
                        if (!itemTitle.empty() && !clip.title.empty()) {
                            if (itemTitle.find(clip.title) != std::string::npos ||
                                clip.title.find(itemTitle) != std::string::npos) {
                                return true;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    return false;
}

std::string SunoLyricsManager::extractClipIdFromTrack() const {
    auto item = audioEngine_->playlist().currentItem();
    if (!item) return "";

    if (!item->metadata.sunoClipId.empty()) {
        return item->metadata.sunoClipId;
    }

    static const std::regex uuidRegex(
        "([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})",
        std::regex::icase);
    std::smatch match;

    if (item->isRemote) {
        if (std::regex_search(item->url, match, uuidRegex)) return match[1].str();
    } else {
        std::string filename = item->path.filename().string();
        if (std::regex_search(filename, match, uuidRegex)) return match[1].str();
        
        std::string fullPath = item->path.string();
        if (std::regex_search(fullPath, match, uuidRegex)) return match[1].str();
        
        if (item->path.has_parent_path()) {
            std::string parentName = item->path.parent_path().filename().string();
            if (std::regex_search(parentName, match, uuidRegex)) return match[1].str();
        }
    }

    std::string currentTitle = item->title();
    if (!currentTitle.empty() && clipsRef_) {
        for (const auto& clip : *clipsRef_) {
            if (clip.title == currentTitle) return clip.id;
        }
        for (const auto& clip : *clipsRef_) {
            if (!clip.title.empty()) {
                if (currentTitle.find(clip.title) != std::string::npos ||
                    clip.title.find(currentTitle) != std::string::npos) {
                    LOG_DEBUG("SunoLyricsManager: Fuzzy matched '{}' to clip {}", currentTitle, clip.id);
                    return clip.id;
                }
            }
        }
    }

    return "";
}

std::optional<AlignedLyrics> SunoLyricsManager::parseAndDisplayLyrics(
    const std::string& clipId, const std::string& json, const QJsonDocument& doc) {
    
    if (!doc.isArray() && !doc.isObject()) {
        LOG_WARN("SunoLyricsManager: Unknown lyrics JSON format for {}", clipId);
        return std::nullopt;
    }

    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
    
    if (words.empty()) {
        LOG_WARN("SunoLyricsManager: Parsed JSON but found no words for {}", clipId);
        return std::nullopt;
    }
    
    LOG_INFO("SunoLyricsManager: Parsed {} words for {}", words.size(), clipId);

    std::string prompt;
    if (clipsRef_) {
        for (const auto& clip : *clipsRef_) {
            if (clip.id == clipId) {
                prompt = clip.metadata.prompt;
                break;
            }
        }
    }

    if (prompt.empty()) {
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
            prompt = clipOpt.value()->metadata.prompt;
        }
    }

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    
    overlayEngine_->setAlignedLyrics(lyrics);
    
    return lyrics;
}

void SunoLyricsManager::saveLyricsSidecar(const std::string& clipId, 
                                          const std::string& json,
                                          const QJsonDocument& doc) {
    fs::path saveDir = CONFIG.suno().downloadPath;
    if (saveDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        saveDir = fs::path(musicLoc.toStdString());
    }

    std::string safeTitle = clipId;
    if (clipsRef_) {
        for (const auto& clip : *clipsRef_) {
            if (clip.id == clipId) {
                safeTitle = clip.title;
                break;
            }
        }
    }
    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    
    fs::path audioPath = saveDir / (safeTitle + ".mp3");
    if (fs::exists(audioPath)) {
        fs::path jsonPath = saveDir / (safeTitle + ".json");
        fs::path srtPath = saveDir / (safeTitle + ".srt");
        
        std::ofstream jf(jsonPath);
        if (jf) {
            jf << json;
            LOG_INFO("SunoLyricsManager: Saved JSON to {}", jsonPath.string());
        }
        
        auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
        if (!words.empty() && clipsRef_) {
            std::string prompt;
            for (const auto& clip : *clipsRef_) {
                if (clip.id == clipId) {
                    prompt = clip.metadata.prompt;
                    break;
                }
            }
            
            AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
            
            if (!lyrics.lines.empty()) {
                std::ofstream sf(srtPath);
                if (sf) {
                    int index = 1;
                    for (const auto& line : lyrics.lines) {
                        auto fmtTime = [](double s) {
                            int ms = (int)((s - (int)s) * 1000);
                            int totSec = (int)s;
                            int hr = totSec / 3600;
                            int mn = (totSec % 3600) / 60;
                            int sc = totSec % 60;
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", hr, mn, sc, ms);
                            return std::string(buf);
                        };
                        
                        sf << index++ << "\n";
                        sf << fmtTime(line.start_s) << " --> " << fmtTime(line.end_s) << "\n";
                        sf << line.text << "\n\n";
                    }
                    LOG_INFO("SunoLyricsManager: Saved SRT to {}", srtPath.string());
                }
            }
        }
    }
}

void SunoLyricsManager::setDebugLyrics(const AlignedLyrics& lyrics) {
    LOG_INFO("SunoLyricsManager: Forcing debug lyrics ({} lines)", lyrics.lines.size());
    overlayEngine_->setAlignedLyrics(lyrics);
}

} // namespace vc::suno
