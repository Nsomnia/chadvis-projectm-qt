#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "suno/SunoLyrics.hpp"
#include "ui/SunoCookieDialog.hpp"
#include "util/FileUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>
#include <regex>

namespace vc::suno {

SunoController::SunoController(AudioEngine* audioEngine,
                               OverlayEngine* overlayEngine,
                               MainWindow* window)
    : QObject(nullptr),
      audioEngine_(audioEngine),
      overlayEngine_(overlayEngine),
      window_(window),
      client_(std::make_unique<SunoClient>(nullptr)),
      networkManager_(new QNetworkAccessManager(this)) {
    // client_ is a unique_ptr, do NOT set parent to avoid double-free
    // but we can connect signals safely because client_ is a QObject

    // Connect client signals
    client_->libraryFetched.connect(
            [this](const auto& clips) { onLibraryFetched(clips); });
    client_->alignedLyricsFetched.connect(
            [this](const auto& id, const auto& json) {
                onAlignedLyricsFetched(id, json);
            });
    client_->tokenChanged.connect([this](const auto& token) {
        LOG_INFO("SunoController: Token updated, saving to config");
        CONFIG.suno().token = token;
        CONFIG.save(CONFIG.configPath());
    });
    client_->errorOccurred.connect([this](const auto& msg) { onError(msg); });

    // Initialize database
    fs::path dataDir = file::dataDir();
    file::ensureDir(dataDir);
    fs::path dbPath = dataDir / "suno_library.db";
    db_.init(dbPath.string());

    // Load cached clips from database
    auto cachedClips = db_.getAllClips();
    if (cachedClips.isOk() && !cachedClips.value().empty()) {
        LOG_INFO("SunoController: Loaded {} cached clips from database", cachedClips.value().size());
        accumulatedClips_ = cachedClips.value();
    }

    // Load token/cookie from config
    if (!CONFIG.suno().token.empty()) {
        LOG_INFO("SunoController: Loaded token from config (length: {})", CONFIG.suno().token.length());
        client_->setToken(CONFIG.suno().token);
    } else {
        LOG_INFO("SunoController: No token in config");
    }

    if (!CONFIG.suno().cookie.empty()) {
        LOG_INFO("SunoController: Loaded cookie from config (length: {})", CONFIG.suno().cookie.length());
        client_->setCookie(CONFIG.suno().cookie);
    } else {
        LOG_INFO("SunoController: No cookie in config");
    }

    downloadDir_ = CONFIG.suno().downloadPath;
    if (downloadDir_.empty()) {
        downloadDir_ = file::dataDir() / "suno_downloads";
    }
    file::ensureDir(downloadDir_);

    if (client_->isAuthenticated()) {
        QTimer::singleShot(2000, this, [this]() {
            refreshLibrary(1);
        });
    }
    
    // Connect to track changes to update lyrics overlay
    audioEngine_->playlist().currentChanged.connect([this](size_t) {
        onTrackChanged();
    });
}

SunoController::~SunoController() = default;

void SunoController::refreshLibrary(int page) {
    if (!client_->isAuthenticated()) {
        if (!CONFIG.suno().cookie.empty()) {
            client_->setCookie(CONFIG.suno().cookie);
        }
        if (!CONFIG.suno().token.empty()) {
            client_->setToken(CONFIG.suno().token);
        }
    }

    if (!client_->isAuthenticated()) {
        showCookieDialog();
        return;
    }
    
    // Clear accumulated clips when starting new sync from page 1
    if (page == 1) {
        accumulatedClips_.clear();
        isSyncing_ = true;
    }
    
    statusMessage.emitSignal("Syncing Suno library (Page " + std::to_string(page) + ")...");
    client_->fetchLibrary(page);
}

void SunoController::syncDatabase(bool forceAuth) {
    if (forceAuth) {
        showCookieDialog();
    } else {
        refreshLibrary(1);
    }
}

void SunoController::showCookieDialog() {
    auto* dialog = new ui::SunoCookieDialog();
    if (dialog->exec() == QDialog::Accepted) {
        std::string cookie = dialog->getCookie().toStdString();
        client_->setCookie(cookie);
        // Save to config
        CONFIG.suno().cookie = cookie;
        CONFIG.save(CONFIG.configPath());
        // Start fresh sync
        accumulatedClips_.clear();
        isSyncing_ = true;
        client_->fetchLibrary(1);
    }
    dialog->deleteLater();
}

void SunoController::onLibraryFetched(const std::vector<SunoClip>& clips) {
    LOG_INFO("SunoController: Fetched {} clips", clips.size());
    
    // Accumulate clips for this sync session
    for (const auto& clip : clips) {
        accumulatedClips_.push_back(clip);
    }
    
    // Save to database
    db_.saveClips(clips);
    
    if (clips.size() >= 20) {
        // More pages to fetch
        currentSyncPage_++;
        refreshLibrary(currentSyncPage_);
    } else {
        // Sync complete - emit ALL accumulated clips
        isSyncing_ = false;
        currentSyncPage_ = 1;
        LOG_INFO("SunoController: Sync complete. Total clips: {}", accumulatedClips_.size());
        libraryUpdated.emitSignal(accumulatedClips_);
        statusMessage.emitSignal("Suno library sync complete (" + std::to_string(accumulatedClips_.size()) + " clips)");

        for (const auto& clip : accumulatedClips_) {
            // Force re-fetch of aligned lyrics if JSON is missing or empty
            auto lyricsRes = db_.getAlignedLyrics(clip.id);
            if (lyricsRes.isErr() || lyricsRes.value().empty()) {
                lyricsQueue_.push_back(clip.id);
            }
        }
        processLyricsQueue();
    }
}

void SunoController::processLyricsQueue() {
    // Limit concurrent requests to 3 to be nicer to API and avoid rate limits
    while (activeLyricsRequests_ < 3 && !lyricsQueue_.empty()) {
        std::string id = lyricsQueue_.front();
        lyricsQueue_.pop_front();
        activeLyricsRequests_++;
        
        // Add random jitter delay (50-250ms) to avoid hammering
        int jitter = 50 + (rand() % 200);
        QTimer::singleShot(jitter, this, [this, id]() {
            client_->fetchAlignedLyrics(id);
        });
    }
}

void SunoController::onAlignedLyricsFetched(const std::string& clipId,
                                            const std::string& json) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    processLyricsQueue();

    LOG_INFO("SunoController: Fetched aligned lyrics for {}", clipId);
    db_.saveAlignedLyrics(clipId, json);
    clipUpdated.emitSignal(clipId);

    // Parse JSON first to get lyrics object
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    
    // Check if this is the currently playing song before updating overlay
    bool isCurrent = false;
    if (auto item = audioEngine_->playlist().currentItem()) {
        if (item->isRemote) {
            // Check if URL contains ID (Suno CDN URLs usually do)
            if (item->url.find(clipId) != std::string::npos) {
                isCurrent = true;
            }
        } else {
             // For local files, check filename or if we can match ID from metadata?
             // Simplest is to assume if we are just fetching it, user might want to see it?
             // No, background sync is happening.
             // If local file path contains ID?
             if (item->path.string().find(clipId) != std::string::npos) {
                 isCurrent = true;
             }
        }
    }
    
    // Only update overlay if it's the current song
    if (!isCurrent) {
        return;
    }

    if (doc.isArray() || (doc.isObject() && doc.object().contains("words")) || (doc.isObject() && doc.object().contains("aligned_words"))) {
        std::vector<AlignedWord> words;

        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("aligned_words") && obj["aligned_words"].isArray()) {
                arr = obj["aligned_words"].toArray();
            } else if (obj.contains("words") && obj["words"].isArray()) {
                arr = obj["words"].toArray();
            } else if (obj.contains("lyrics") && obj["lyrics"].isArray()) { // Some payloads use "lyrics"
                arr = obj["lyrics"].toArray();
            }
        }

        if (arr.isEmpty()) {
            LOG_WARN("SunoController: Parsed JSON for {} but found no words array", clipId);
            return;
        }

        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            AlignedWord w;
            w.word = obj["word"].toString().toStdString();
            
            // Handle both "start" and "start_s"
            if (obj.contains("start_s")) w.start_s = obj["start_s"].toDouble();
            else if (obj.contains("start")) w.start_s = obj["start"].toDouble();
            
            if (obj.contains("end_s")) w.end_s = obj["end_s"].toDouble();
            else if (obj.contains("end")) w.end_s = obj["end"].toDouble();
            
            // Handle score
            if (obj.contains("p_align")) w.score = obj["p_align"].toDouble();
            else if (obj.contains("score")) w.score = obj["score"].toDouble();
            else w.score = 1.0;

            words.push_back(w);
        }
        
        LOG_INFO("SunoController: Parsed {} aligned words for {}", words.size(), clipId);

        // Find prompt for alignment
        std::string prompt;
        for (const auto& clip : accumulatedClips_) {
            if (clip.id == clipId) {
                prompt = clip.metadata.prompt;
                break;
            }
        }

        if (prompt.empty()) {
            // Try database
            auto clipOpt = db_.getClip(clipId);
            if (clipOpt.isOk() && clipOpt.value()) {
                prompt = clipOpt.value()->metadata.prompt;
            }
        }

        AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
        lyrics.songId = clipId;
        overlayEngine_->setAlignedLyrics(lyrics);
        
        // Notify any widgets listening (KaraokeWidget)
        clipUpdated.emitSignal(clipId);
    } else {
        LOG_WARN("SunoController: Unknown lyrics JSON format for {}", clipId);
        // Log a snippet for debugging
        std::string snippet = json.substr(0, 200);
        LOG_DEBUG("JSON snippet: {}", snippet);
    }
}


void SunoController::onError(const std::string& message) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    // Check if it's a "processing" error -> Re-queue at the back
    if (message.rfind("Lyrics processing:", 0) == 0) {
        std::string id = message.substr(18); // len("Lyrics processing:") + 1? No 18
        if (!id.empty()) {
            LOG_INFO("SunoController: Re-queueing processing lyrics for {}", id);
            lyricsQueue_.push_back(id);
        }
    }
    
    processLyricsQueue();

    LOG_ERROR("SunoController: {}", message);
    statusMessage.emitSignal(message);
}

void SunoController::downloadAndPlay(const SunoClip& clip) {
    if (clip.audio_url.empty()) {
        LOG_ERROR("SunoController: No audio URL for {}", clip.title);
        return;
    }

    if (CONFIG.suno().autoDownload) {
        downloadAudio(clip);
    } else {
        audioEngine_->playlist().addUrl(clip.audio_url, clip.title);
        audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
    }
}

Result<AlignedLyrics> SunoController::getLyrics(const std::string& clipId) {
    auto jsonRes = db_.getAlignedLyrics(clipId);
    if (!jsonRes.isOk()) {
        return Result<AlignedLyrics>::err("No lyrics found");
    }

    std::string json = jsonRes.value();
    std::string prompt;

    for (const auto& clip : accumulatedClips_) {
        if (clip.id == clipId) {
            prompt = clip.metadata.prompt;
            break;
        }
    }

    if (prompt.empty()) {
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
            prompt = clipOpt.value()->metadata.prompt;
        }
    }

    if (prompt.empty()) {
         return Result<AlignedLyrics>::err("Prompt not found for alignment");
    }

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact); 
    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
    
    if (words.empty()) {
         return Result<AlignedLyrics>::err("Failed to parse words from JSON");
    }

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    return Result<AlignedLyrics>::ok(lyrics);
}

void SunoController::downloadAudio(const SunoClip& clip) {
    LOG_INFO("SunoController: Downloading {}", clip.title);

    QUrl url(QString::fromStdString(clip.audio_url));
    QNetworkRequest request(url);

    QNetworkReply* reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, clip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoController: Download failed: {}",
                      reply->errorString().toStdString());
            return;
        }

        QString fileName =
                QString::fromStdString(clip.title).replace(" ", "_") + ".mp3";
        fs::path filePath = downloadDir_ / fileName.toStdString();

        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            LOG_INFO("SunoController: Saved to {}", filePath.string());
            processDownloadedFile(clip, filePath);
        } else {
            LOG_ERROR("SunoController: Failed to open file for writing: {}",
                      filePath.string());
        }
    });
}

void SunoController::processDownloadedFile(const SunoClip& clip,
                                           const fs::path& path) {
    audioEngine_->playlist().addFile(path);

    // If we have aligned lyrics, prepare them for overlay
    auto lyricsRes = db_.getAlignedLyrics(clip.id);
    if (lyricsRes.isOk()) {
        LOG_INFO("SunoController: Loaded aligned lyrics for {}", clip.title);
        // TODO: Pass to overlay engine
    }
}

void SunoController::onTrackChanged() {
    auto item = audioEngine_->playlist().currentItem();
    if (!item) {
        overlayEngine_->setAlignedLyrics({}); // Clear
        return;
    }

    std::string clipId;
    static const std::regex uuidRegex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", std::regex::icase);
    std::smatch match;

    if (item->isRemote) {
        if (std::regex_search(item->url, match, uuidRegex)) {
            clipId = match.str();
        }
    } else {
        std::string filename = item->path.filename().string();
        if (std::regex_search(filename, match, uuidRegex)) {
            clipId = match.str();
        }
    }

    if (!clipId.empty()) {
        auto res = getLyrics(clipId);
        if (res.isOk()) {
            LOG_INFO("SunoController: Displaying lyrics for {}", clipId);
            overlayEngine_->setAlignedLyrics(res.value());
            return;
        }
    }
    
    // Clear if no lyrics found
    overlayEngine_->setAlignedLyrics({});
}

} // namespace vc::suno
