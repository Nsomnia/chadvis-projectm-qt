#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "lyrics/LyricsData.hpp"

#include "suno/ClipResolver.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoLyricsManager.hpp"
#include "util/FileUtils.hpp"

#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <regex>

namespace vc::suno {

namespace {

/// Stable per-install Device-Id for the studio-api header set. Generated once
/// and persisted as a plain (non-secret) UUID in config.toml [suno].
QString resolveOrCreateDeviceId() {
    auto& cfg = CONFIG.suno();
    if (!cfg.deviceId.empty()) {
        return QString::fromStdString(cfg.deviceId);
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cfg.deviceId = id.toStdString();
    std::ignore = CONFIG.save(CONFIG.configPath());
    LOG_INFO("SunoController: generated new Device-Id {}", id.toStdString());
    return id;
}

} // namespace

SunoController::SunoController(AudioEngine* audioEngine,
	QObject* parent)
: QObject(parent),
	audioEngine_(audioEngine),
	client_(std::make_unique<SunoClient>(resolveOrCreateDeviceId())) {
    
    // Initialize Database
    fs::path dataDir = file::dataDir();
    (void)file::ensureDir(dataDir);
    fs::path dbPath = dataDir / "suno_library.db";
    db_.init(dbPath.string());

    // Initialize Managers
    accountManager_ = std::make_unique<SunoAccountManager>(client_.get(), this);
    libraryManager_ = std::make_unique<SunoLibraryManager>(client_.get(), db_, this);
    
    auto networkManager = new QNetworkAccessManager(this); // Owned by SunoController (or QObject tree)
    downloader_ = std::make_unique<SunoDownloader>(client_.get(), db_, audioEngine_, networkManager, this);

    // Forward queue progress so bridges/QML can consume it later.
    connect(downloader_.get(), &SunoDownloader::downloadStateChanged,
            this, &SunoController::downloadStateChanged);
    connect(downloader_.get(), &SunoDownloader::downloadQueueIdle,
            this, &SunoController::downloadQueueIdle);
    
    lyricsManager_ = std::make_unique<SunoLyricsManager>(client_.get(), db_, this);

    orchestrator_ = std::make_unique<SunoOrchestrator>(client_.get(), this);
    connect(orchestrator_.get(), &SunoOrchestrator::messageReceived, this, &SunoController::chatMessageReceived);
    connect(orchestrator_.get(), &SunoOrchestrator::historyFetched, this, &SunoController::chatHistoryFetched);
    connect(orchestrator_.get(), &SunoOrchestrator::errorOccurred, this, &SunoController::chatError);

	// --- Connect Signals ---

	// Auth state (client runs restore/migrate in its constructor)
	connect(client_.get(), &SunoClient::needsReauth, this, [this]() {
		emit authenticationRequired();
	});
	connect(client_.get(), &SunoClient::authStateChanged, this, [this]() {
		switch (client_->authState()) {
		case auth::AuthState::ActiveValid:
			emit statusMessage("Suno authentication active");
			emit authenticationSuccess();
			// Session catalog + billing bootstrap (once per activation).
			if (accountManager_) {
				accountManager_->refreshAll();
			}
			break;
		case auth::AuthState::NeedsReauth:
			emit authenticationFailed(
					"Suno session expired — paste a fresh cookie or token in settings");
			break;
		case auth::AuthState::Disconnected:
			break;
		}
	});

	// Cheap billing refresh after a generation kicks off (credits change as
	// the clip is submitted); delayed so the backend has settled its ledger.
	// generationStarted is a custom Signal<> (not Qt), so use .connect().
	client_->generationStarted.connect([this](const std::vector<SunoClip>&) {
		if (!accountManager_) return;
		QTimer::singleShot(std::chrono::seconds(15), this,
		                   [this]() { accountManager_->refreshBilling(); });
	});

	// Library Manager
	connect(libraryManager_.get(), &SunoLibraryManager::statusMessage,
		this, &SunoController::statusMessage);
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
		this, &SunoController::authenticationRequired);

	// Lyrics Manager
	connect(lyricsManager_.get(), &SunoLyricsManager::statusMessage,
		this, &SunoController::statusMessage);
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

    // Resolve clip from library cache, falling back to DB
    if (auto clipOpt = resolveClip(libraryManager_->accumulatedClips(), db_, clipId)) {
        prompt = clipOpt->metadata.prompt;
        auto durOpt = file::parseDuration(clipOpt->metadata.duration);
        if (durOpt) duration = durOpt->count() / 1000.0f;
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
    // No system-browser flow anymore: surface the requirement to QML, which
    // points users at the settings panel to paste fresh credentials.
    emit authenticationRequired();
}

void SunoController::sendChatMessage(const QString& message, const QString& workspaceId) {
    if (orchestrator_) orchestrator_->sendMessage(message, workspaceId);
}

void SunoController::fetchChatHistory() {
    if (orchestrator_) orchestrator_->fetchHistory();
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

	// 3. Sidecar Files (.srt then .json share one load path)
	fs::path trackPath = item->isRemote ? fs::path() : item->path;
	if (!trackPath.empty()) {
		fs::path dir = trackPath.parent_path();
		std::string stem = trackPath.stem().string();

		auto tryLoadSidecar = [&](const std::string& ext, auto&& parser) -> bool {
			auto content = file::readText(dir / (stem + ext));
			if (content.isErr()) return false;
			auto data = parser(content.value());
			if (data.empty()) return false;
			AlignedLyrics lyrics = AlignedLyrics::fromLyricsData(data);
			lyrics.songId = clipId;
			directLyricsCache_[clipId] = lyrics;
			return true;
		};

		if (tryLoadSidecar(".srt", [](const std::string& s) { return vc::LyricsFactory::fromSrt(s); })) return;
		if (tryLoadSidecar(".json", [](const std::string& s) { return vc::LyricsFactory::fromSunoJson(s); })) return;
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
    if (auto clipOpt = resolveClip(libraryManager_->accumulatedClips(), db_, clipId)) {
        prompt = clipOpt->metadata.prompt;
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
