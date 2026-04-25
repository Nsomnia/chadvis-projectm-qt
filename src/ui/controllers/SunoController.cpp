#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

#include "suno/SunoAuthManager.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoLyricsManager.hpp"
#include "suno/SunoOrchestrator.hpp"
#include "util/FileUtils.hpp"

#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <regex>
#include <fstream>
#include <iterator>

namespace vc::suno {

SunoController::SunoController(AudioEngine* audioEngine,
	QObject* parent)
: QObject(parent),
	audioEngine_(audioEngine),
	client_(std::make_unique<SunoClient>(nullptr)) {
    
    // Initialize Database
    fs::path dataDir = file::dataDir();
    file::ensureDir(dataDir);
    fs::path dbPath = dataDir / "suno_library.db";
    db_.init(dbPath.string());

    // Initialize Managers
    authManager_ = std::make_unique<SunoAuthManager>(client_.get(), this);
    libraryManager_ = std::make_unique<SunoLibraryManager>(client_.get(), db_, this);
    
    auto networkManager = new QNetworkAccessManager(this); // Owned by SunoController (or QObject tree)
    downloader_ = std::make_unique<SunoDownloader>(client_.get(), db_, audioEngine_, networkManager, this);
    
    lyricsManager_ = std::make_unique<SunoLyricsManager>(client_.get(), db_, this);
    orchestrator_ = std::make_unique<vc::SunoOrchestrator>(client_.get(), this);

	// --- Connect Signals ---

	// Auth Manager
	connect(authManager_.get(), &SunoAuthManager::statusMessage,
		this, [this](const std::string& msg) {
			emit statusMessage(msg);
		});
	connect(authManager_.get(), &SunoAuthManager::authenticationRequired,
		this, [this]() {
			emit authenticationRequired();
		});
	// Re-emit auth initialization
	authManager_->initialize();

	// Library Manager
	connect(libraryManager_.get(), &SunoLibraryManager::statusMessage,
		this, [this](const std::string& msg) {
			emit statusMessage(msg);
		});
	connect(libraryManager_.get(), &SunoLibraryManager::libraryUpdated,
		this, [this](const std::vector<SunoClip>& clips) {
			emit libraryUpdated(clips);

			// Check for missing lyrics in newly fetched clips
			for (const auto& clip : clips) {
				auto lyricsRes = db_.getAlignedLyrics(clip.id);
				if (lyricsRes.isErr() || lyricsRes.value().empty()) {
					lyricsManager_->queueLyricsFetch(clip.id);
				}
			}
		});
	connect(libraryManager_.get(), &SunoLibraryManager::authenticationRequired,
		this, [this]() {
			emit authenticationRequired();
		});

	// Lyrics Manager
	connect(lyricsManager_.get(), &SunoLyricsManager::statusMessage,
		this, [this](const std::string& msg) {
			emit statusMessage(msg);
		});
	connect(lyricsManager_.get(), &SunoLyricsManager::lyricsFetched,
		this, [this](const std::string& id, const std::string& json) {
			// Immediate display logic
			QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));

			bool isCurrent = isCurrentlyPlaying(id);
			if (isCurrent && !CONFIG.suno().debugLyrics) {
				auto lyricsOpt = parseAndDisplayLyrics(id, json, doc);
				if (lyricsOpt) {
					directLyricsCache_[id] = *lyricsOpt;
					LOG_INFO("SunoController: Immediately displayed lyrics for current track {}", id);
				}
			}

			// Background: Save to DB and Sidecar
			db_.saveAlignedLyrics(id, json);
			emit clipUpdated(id);

			if (CONFIG.suno().saveLyrics) {
				downloader_->saveLyricsSidecar(id, json, doc, libraryManager_->accumulatedClips());
			}
		});

	// Orchestrator signals
	connect(orchestrator_.get(), &vc::SunoOrchestrator::messageReceived,
		this, [this](const QString& response, const QString& workspaceId) {
			emit statusMessage("Orchestrator: " + response.toStdString());
		});
	connect(orchestrator_.get(), &vc::SunoOrchestrator::errorOccurred,
		this, [this](const QString& error) {
			emit statusMessage("Orchestrator Error: " + error.toStdString());
		});

	// Connect to track changes
	audioEngine_->playlist().currentChanged.connect([this](size_t) {
		onTrackChanged();
	});
    
    // Initial Library Refresh if authenticated
    if (client_->isAuthenticated()) {
        QTimer::singleShot(2000, this, [this]() {
            refreshLibrary(1);
        });
    }

    // Handle Debug Lyrics
    if (CONFIG.suno().debugLyrics && !CONFIG.suno().debugLyricsFile.empty()) {
         fs::path p = CONFIG.suno().debugLyricsFile;
        if (fs::exists(p)) {
            LOG_INFO("SunoController: Loading debug lyrics from {}", p.string());
            QFile f(QString::fromStdString(p.string()));
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray data = f.readAll();
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".srt") {
                    auto lyrics = LyricsAligner::parseSrt(data.toStdString());
                    lyrics.songId = "debug-srt-id";
                    setDebugLyrics(lyrics);
                } else {
                    auto words = LyricsAligner::parseJson(data);
                    if (!words.empty()) {
                         std::string prompt;
                        for (size_t i = 0; i < words.size(); ++i) {
                            prompt += words[i].word;
                            if ((i + 1) % 5 == 0) prompt += "\n"; 
                        }
                        auto lyrics = LyricsAligner::align(prompt, words);
                        lyrics.songId = "debug-test-id";
                        setDebugLyrics(lyrics);
                    }
                }
            }
        }
    }
}

SunoController::~SunoController() = default;

void SunoController::downloadAndPlay(const SunoClip& clip) {
    downloader_->downloadAndPlay(clip);
}

Result<AlignedLyrics> SunoController::getLyrics(const std::string& clipId) {
    // Check DB
    auto jsonRes = db_.getAlignedLyrics(clipId);
    if (!jsonRes.isOk()) {
        return Result<AlignedLyrics>::err("No lyrics found");
    }
    
    std::string json = jsonRes.value();
    std::string prompt;
    f32 duration = 0.0f;

    // Try to find clip in library
    const auto& clips = libraryManager_->accumulatedClips();
    for (const auto& clip : clips) {
        if (clip.id == clipId) {
            prompt = clip.metadata.prompt;
             auto durOpt = file::parseDuration(clip.metadata.duration);
            if (durOpt) duration = durOpt->count() / 1000.0f;
            break;
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

    if (prompt.empty()) return Result<AlignedLyrics>::err("Prompt not found");

    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json), duration);
    if (words.empty()) words = LyricsAligner::estimateTimings(prompt, duration);
    
    if (words.empty()) return Result<AlignedLyrics>::err("Failed to parse words");

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    return Result<AlignedLyrics>::ok(lyrics);
}

void SunoController::refreshLibrary(int page) {
    libraryManager_->refreshLibrary(page);
}

void SunoController::syncDatabase(bool forceAuth) {
    libraryManager_->syncDatabase(forceAuth);
}

void SunoController::requestAuthentication() {
    authManager_->requestAuthentication();
}

void SunoController::startSystemBrowserAuth() {
    authManager_->startSystemBrowserAuth();
}

const std::vector<SunoClip>& SunoController::clips() const {
    return libraryManager_->accumulatedClips();
}

void SunoController::setDebugLyrics(const AlignedLyrics& lyrics) {
    // overlayEngine_->setAlignedLyrics(lyrics); // Legacy CPU overlay removed
}

void SunoController::onTrackChanged() {
    if (CONFIG.suno().debugLyrics) return;

    auto item = audioEngine_->playlist().currentItem();
    if (!item) {
        return;
    }

    std::string clipId = extractClipIdFromTrack();
    if (clipId.empty() && !lastRequestedClipId_.empty()) {
        clipId = lastRequestedClipId_;
    }
    if (!clipId.empty()) lastRequestedClipId_ = clipId;

    if (clipId.empty()) {
        return;
    }

    // 1. Direct Cache
    auto cacheIt = directLyricsCache_.find(clipId);
    if (cacheIt != directLyricsCache_.end()) {
        return;
    }

    // 2. Database
    auto res = getLyrics(clipId);
    if (res.isOk()) {
        directLyricsCache_[clipId] = res.value();
        return;
    }

    // 3. Sidecar Files
    // Logic from original controller
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
                    lyrics.songId = clipId;
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
                    AlignedLyrics lyrics;
                    lyrics.songId = clipId;
                    lyrics.words = words;
                    AlignedLine line;
                    line.start_s = words.front().start_s;
                    line.end_s = words.back().end_s;
                    for (const auto& w : words) line.text += w.word + " ";
                    lyrics.lines.push_back(line);
                    directLyricsCache_[clipId] = lyrics;
                    return;
                }
            }
        }
    }

    // 4. API
    if (client_->isAuthenticated()) {
        lyricsManager_->queueLyricsFetch(clipId);
    }
}

std::optional<AlignedLyrics> SunoController::parseAndDisplayLyrics(
    const std::string& clipId,
    const std::string& json,
    const QJsonDocument& doc) {
    
    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
    if (words.empty()) return std::nullopt;

    std::string prompt;
    for (const auto& clip : libraryManager_->accumulatedClips()) {
        if (clip.id == clipId) {
            prompt = clip.metadata.prompt;
            break;
        }
    }
    
    if (prompt.empty()) {
         auto clipOpt = db_.getClip(clipId);
         if (clipOpt.isOk() && clipOpt.value()) prompt = clipOpt.value()->metadata.prompt;
    }

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    return lyrics;
}

bool SunoController::isCurrentlyPlaying(const std::string& clipId) const {
    if (auto item = audioEngine_->playlist().currentItem()) {
        if (item->isRemote) {
            if (item->url.find(clipId) != std::string::npos) return true;
        } else {
            if (item->path.string().find(clipId) != std::string::npos) return true;
            else if (item->title().find(clipId) != std::string::npos) return true;
        }
    }
    return false;
}

std::string SunoController::extractClipIdFromTrack() const {
     auto item = audioEngine_->playlist().currentItem();
    if (!item) return "";
    if (!item->metadata.sunoClipId.empty()) return item->metadata.sunoClipId;
    
    static const std::regex uuidRegex("([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})", std::regex::icase);
    std::smatch match;
    
    if (item->isRemote) {
        if (std::regex_search(item->url, match, uuidRegex)) return match[1].str();
    } else {
        std::string filename = item->path.filename().string();
        if (std::regex_search(filename, match, uuidRegex)) return match[1].str();
        std::string fullPath = item->path.string();
        if (std::regex_search(fullPath, match, uuidRegex)) return match[1].str();
    }
    
    std::string currentTitle = item->title();
    if (!currentTitle.empty()) {
        for (const auto& clip : libraryManager_->accumulatedClips()) {
            if (clip.title == currentTitle) return clip.id;
        }
    }
    return "";
}

} // namespace vc::suno
