#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "suno/SunoLyrics.hpp"
#include "ui/SunoCookieDialog.hpp"
#include "util/FileUtils.hpp"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>
#include <regex>
#include <fstream>

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
    client_->wavConversionReady.connect(
            [this](const auto& id, const auto& url) {
                onWavConversionReady(id, url);
            });
    client_->tokenChanged.connect([this](const auto& token) {
        LOG_INFO("SunoController: Token updated, saving to config");
        CONFIG.suno().token = token;
        CONFIG.save(CONFIG.configPath());
        
        if (isRefreshingToken_) {
            isRefreshingToken_ = false;
            // Verify we actually have a valid token before resuming
            if (!CONFIG.suno().token.empty()) {
                LOG_INFO("SunoController: Resuming lyrics queue after token refresh");
                processLyricsQueue();
            } else {
                LOG_WARN("SunoController: Token cleared during refresh? Halting queue.");
            }
        }
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

    // Handle Debug Lyrics (Test Mode)
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
                    // SRT Parsing
                    auto lyrics = LyricsAligner::parseSrt(data.toStdString());
                    lyrics.songId = "debug-srt-id";
                    setDebugLyrics(lyrics);
                } else {
                    // JSON Parsing (Suno format)
                    auto words = LyricsAligner::parseJson(data);
                    if (!words.empty()) {
                        // Construct a dummy prompt to allow line alignment
                        std::string prompt;
                        for (size_t i = 0; i < words.size(); ++i) {
                            prompt += words[i].word;
                            // Add artificial line breaks every 5 words for testing
                            if ((i + 1) % 5 == 0) prompt += "\n"; 
                        }
                        
                        auto lyrics = LyricsAligner::align(prompt, words);
                        lyrics.songId = "debug-test-id";
                        setDebugLyrics(lyrics);
                    } else {
                        LOG_ERROR("SunoController: Failed to parse debug lyrics JSON");
                    }
                }
            } else {
                LOG_ERROR("SunoController: Could not open debug lyrics file");
            }
        } else {
            LOG_WARN("SunoController: Debug lyrics file not found: {}", p.string());
        }
    }
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
    
    std::string msg = "Syncing Suno library (Page " + std::to_string(page) + ")";
    if (!accumulatedClips_.empty()) {
        msg += " - " + std::to_string(accumulatedClips_.size()) + " clips found so far...";
    }
    statusMessage.emitSignal(msg);
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
        if (!cookie.empty()) {
            client_->setCookie(cookie);
            CONFIG.suno().cookie = cookie;
            CONFIG.save(CONFIG.configPath());
            accumulatedClips_.clear();
            isSyncing_ = true;
            client_->fetchLibrary(1);
        }
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
        
        // Initialize stats for UI feedback
        totalLyricsToFetch_ = lyricsQueue_.size();
        lyricsSyncStartTime_ = std::chrono::steady_clock::now();
        
        processLyricsQueue();
    }
}

void SunoController::processLyricsQueue() {
    if (isRefreshingToken_) return;

    // Limit concurrent requests to 3 to be nicer to API and avoid rate limits
    while (activeLyricsRequests_ < 3 && !lyricsQueue_.empty()) {
        std::string id = lyricsQueue_.front();
        lyricsQueue_.pop_front();
        activeLyricsRequests_++;
        
        // Add random jitter delay (50-250ms) to avoid hammering
        int jitter = 50 + (rand() % 200);
        
        // Capture queue size by value for the log
        size_t remaining = lyricsQueue_.size();
        
        QTimer::singleShot(jitter, this, [this, id, remaining]() {
            LOG_INFO("SunoController: Fetching lyrics for {} (Queue: {})", id, remaining);
            client_->fetchAlignedLyrics(id);
        });
    }
}

void SunoController::onAlignedLyricsFetched(const std::string& clipId,
                                            const std::string& json) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    // Update progress status
    if (totalLyricsToFetch_ > 0) {
        size_t processed = totalLyricsToFetch_ - lyricsQueue_.size();
        size_t remaining = lyricsQueue_.size();
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lyricsSyncStartTime_).count();
        
        std::string etaStr = "";
        if (processed > 0 && elapsed > 0) {
            double rate = static_cast<double>(processed) / elapsed;
            if (rate > 0) {
                int etaSec = static_cast<int>(remaining / rate);
                int etaMin = etaSec / 60;
                etaStr = " - ETA: " + std::to_string(etaMin) + "m " + std::to_string(etaSec % 60) + "s";
            }
        }
        
        std::string status = "Syncing lyrics: " + std::to_string(processed) + "/" + 
                             std::to_string(totalLyricsToFetch_) + 
                             " (" + std::to_string(processed * 100 / totalLyricsToFetch_) + "%)" + etaStr;
        statusMessage.emitSignal(status);
    }

    processLyricsQueue();

    LOG_INFO("SunoController: Fetched aligned lyrics for {}", clipId);
    
    // IMMEDIATE DISPLAY: Parse and display lyrics BEFORE database save
    // This ensures lyrics appear as soon as API response returns
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    
    // Check if this is the currently playing song
    bool isCurrent = isCurrentlyPlaying(clipId);
    
    if (isCurrent && !CONFIG.suno().debugLyrics) {
        // Parse and display immediately for current track
        auto lyricsOpt = parseAndDisplayLyrics(clipId, json, doc);
        if (lyricsOpt) {
            // Cache for track restarts during this session
            directLyricsCache_[clipId] = *lyricsOpt;
            LOG_INFO("SunoController: Immediately displayed lyrics for current track {}", clipId);
        }
    } else if (!isCurrent) {
        LOG_DEBUG("SunoController: Fetched lyrics for {} but it's not playing (will cache)", clipId);
    }
    
    // Background: Save to database (non-blocking for display)
    db_.saveAlignedLyrics(clipId, json);
    clipUpdated.emitSignal(clipId);
    
    // Background: Save sidecar files if configured
    if (CONFIG.suno().saveLyrics) {
        saveLyricsSidecar(clipId, json, doc);
    }
}

bool SunoController::isCurrentlyPlaying(const std::string& clipId) const {
    if (auto item = audioEngine_->playlist().currentItem()) {
        if (item->isRemote) {
            if (item->url.find(clipId) != std::string::npos) {
                return true;
            }
        } else {
            // For local files, check filename OR metadata tag matching
            if (item->path.string().find(clipId) != std::string::npos) {
                return true;
            } 
            else if (item->title().find(clipId) != std::string::npos) {
                return true;
            }
            else {
                // Fallback: fuzzy title match with accumulated clips
                for (const auto& clip : accumulatedClips_) {
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

std::optional<AlignedLyrics> SunoController::parseAndDisplayLyrics(
    const std::string& clipId, 
    const std::string& json,
    const QJsonDocument& doc) {
    
    if (!doc.isArray() && !doc.isObject()) {
        LOG_WARN("SunoController: Unknown lyrics JSON format for {}", clipId);
        return std::nullopt;
    }

    // Extract words from JSON using the same logic as LyricsAligner
    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
    
    if (words.empty()) {
        LOG_WARN("SunoController: Parsed JSON for {} but found no words", clipId);
        return std::nullopt;
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
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
            prompt = clipOpt.value()->metadata.prompt;
        }
    }

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    
    // IMMEDIATE: Send to overlay engine
    overlayEngine_->setAlignedLyrics(lyrics);
    
    return lyrics;
}

void SunoController::saveLyricsSidecar(const std::string& clipId, 
                                       const std::string& json,
                                       const QJsonDocument& doc) {
    // Determine save location
    fs::path saveDir = CONFIG.suno().downloadPath;
    if (saveDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        saveDir = fs::path(musicLoc.toStdString());
    }

    // Get title for filename
    std::string safeTitle = clipId;
    for (const auto& clip : accumulatedClips_) {
        if (clip.id == clipId) {
            safeTitle = clip.title;
            break;
        }
    }
    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    
    // Also try to find local MP3 and save alongside it
    fs::path audioPath = saveDir / (safeTitle + ".mp3");
    if (fs::exists(audioPath)) {
        fs::path jsonPath = saveDir / (safeTitle + ".json");
        fs::path srtPath = saveDir / (safeTitle + ".srt");
        
        // Save raw JSON
        std::ofstream jf(jsonPath);
        if (jf) {
            jf << json;
            LOG_INFO("SunoController: Saved JSON lyrics to {}", jsonPath.string());
        }
        
        // Generate and save SRT
        auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
        if (!words.empty()) {
            std::string prompt;
            for (const auto& clip : accumulatedClips_) {
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
                    LOG_INFO("SunoController: Saved SRT lyrics to {}", srtPath.string());
                }
            }
        }
    }
}


void SunoController::onError(const std::string& message) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    // Update progress status on error too (skipped item)
    if (totalLyricsToFetch_ > 0) {
        size_t processed = totalLyricsToFetch_ - lyricsQueue_.size();
        std::string status = "Syncing lyrics: " + std::to_string(processed) + "/" + 
                             std::to_string(totalLyricsToFetch_) + " (Retrying...)";
        statusMessage.emitSignal(status);
    }
    
    // Check if it's a "processing" error -> Re-queue at the back
    if (message.rfind("Lyrics processing:", 0) == 0) {
        std::string id = message.substr(18); 
        if (!id.empty()) {
            LOG_INFO("SunoController: Re-queueing processing lyrics for {}", id);
            lyricsQueue_.push_back(id);
        }
    }
    // Check for Unauthorized/401 -> Re-queue and pause for refresh
    else if (message.find("Unauthorized") != std::string::npos || 
             message.find("401") != std::string::npos) {
        
        // Extract ID if possible? SunoClient doesn't send ID in error string for 401 usually
        // But activeLyricsRequests_ is decremented, so we lost that slot.
        // We should just ensure we pause.
        
        LOG_WARN("SunoController: Auth error detected. Pausing queue for refresh.");
        
        if (!isRefreshingToken_) {
            isRefreshingToken_ = true;
            // The NEXT request would trigger refresh, but we paused queue.
            // So we must manually trigger refresh here.
            client_->refreshAuthToken([this](bool success) {
                if (!success) {
                    LOG_ERROR("SunoController: Token refresh failed. Session likely expired.");
                    isRefreshingToken_ = false;
                    // Reset token/cookie to force full re-auth flow next time or now
                    CONFIG.suno().token = "";
                    CONFIG.save(CONFIG.configPath());
                    
                    statusMessage.emitSignal("Suno session expired. Please re-authenticate.");
                    // Flush queue to prevent infinite retry loops on dead session
                    lyricsQueue_.clear();
                    activeLyricsRequests_ = 0;
                }
                // Success case handled by tokenChanged signal
            });
        }
        
        // Re-queueing the *specific* failed ID is hard because onError signature doesn't include it.
        // For now, we accept dropping this one item from the sync batch, or we'd need to change SunoClient to pass ID.
        // Assuming user can just re-sync later.
        // OR: SunoClient could emit error with ID prefix like "Unauthorized: {id}"
        // But handleNetworkError is generic.
        
    }
    
    processLyricsQueue();

    LOG_ERROR("SunoController: {}", message);
    statusMessage.emitSignal(message);
}

void SunoController::downloadAndPlay(const SunoClip& clip) {
    if (clip.id.empty()) return;

    // Determine file extension based on download format
    std::string extension = ".mp3";
    bool useWav = (CONFIG.suno().downloadFormat == vc::SunoDownloadFormat::WAV);
    if (useWav) {
        extension = ".wav";
    }

    if (clip.audio_url.empty()) {
        LOG_INFO("SunoController: Resolving clip details for ID {}", clip.id);
        
        SunoClip resolvedClip = clip;
        resolvedClip.audio_url = "https://cdn1.suno.ai/" + clip.id + ".mp3";
        resolvedClip.title = clip.id;
        
        LOG_INFO("SunoController: Queueing lyrics fetch for resolved ID {}", clip.id);
        client_->fetchAlignedLyrics(clip.id);
        
        if (useWav) {
            LOG_INFO("SunoController: Initiating WAV conversion for {}", clip.id);
            client_->initiateWavConversion(clip.id);
        } else {
            downloadAudio(resolvedClip);
        }
        return;
    }

    // Sanitize filename
    std::string safeTitle = clip.title;
    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    if (safeTitle.empty()) safeTitle = clip.id;

    fs::path downloadDir = CONFIG.suno().downloadPath;
    if (downloadDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        downloadDir = fs::path(musicLoc.toStdString());
    }
    file::ensureDir(downloadDir);

    fs::path targetPath = downloadDir / (safeTitle + extension);

    // If exists, play local
    if (fs::exists(targetPath)) {
        LOG_INFO("SunoController: Playing local file: {}", targetPath.string());
        audioEngine_->playlist().addFile(targetPath);
        audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
        return;
    }

    if (useWav) {
        LOG_INFO("SunoController: Initiating WAV conversion for {}", clip.id);
        client_->initiateWavConversion(clip.id);
    } else {
        downloadAudio(clip);
    }
}

vc::Result<AlignedLyrics> SunoController::getLyrics(const std::string& clipId) {
    auto jsonRes = db_.getAlignedLyrics(clipId);
    if (!jsonRes.isOk()) {
        return vc::Result<AlignedLyrics>::err("No lyrics found");
    }

    std::string json = jsonRes.value();
    std::string prompt;
    f32 duration = 0.0f;

    for (const auto& clip : accumulatedClips_) {
        if (clip.id == clipId) {
            prompt = clip.metadata.prompt;
            auto durOpt = file::parseDuration(clip.metadata.duration);
            if (durOpt) {
                duration = durOpt->count() / 1000.0f;
            }
            break;
        }
    }

    if (prompt.empty()) {
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
            prompt = clipOpt.value()->metadata.prompt;
            auto durOpt = file::parseDuration(clipOpt.value()->metadata.duration);
            if (durOpt) {
                duration = durOpt->count() / 1000.0f;
            }
        }
    }

    if (prompt.empty()) {
         return vc::Result<AlignedLyrics>::err("Prompt not found for alignment");
    }

    // Parsing logic moved to LyricsAligner, but we need raw words first
    auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json), duration);
    
    if (words.empty()) {
         LOG_WARN("SunoController: JSON parsed empty/invalid. Falling back to estimated timings from prompt.");
         words = LyricsAligner::estimateTimings(prompt, duration);
    }

    if (words.empty()) {
         return vc::Result<AlignedLyrics>::err("Failed to parse words from JSON and fallback failed");
    }

    AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
    lyrics.songId = clipId;
    return vc::Result<AlignedLyrics>::ok(lyrics);
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

        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        vc::file::ensureDir(downloadDir);

        std::string safeTitle = clip.title;
        std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
        if (safeTitle.empty()) safeTitle = clip.id;

        QString fileName = QString::fromStdString(safeTitle) + ".mp3";
        fs::path filePath = downloadDir / fileName.toStdString();

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
    // Auto-play the downloaded file
    audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);

    // If we have aligned lyrics, prepare them for overlay
    auto lyricsRes = db_.getAlignedLyrics(clip.id);
    if (lyricsRes.isOk()) {
        LOG_INFO("SunoController: Loaded aligned lyrics for {}", clip.title);
        // Force track update to refresh overlay
        onTrackChanged();
    }
}

void SunoController::onTrackChanged() {
    if (CONFIG.suno().debugLyrics) {
         LOG_DEBUG("SunoController: Ignoring track change for lyrics update because debugLyrics is active.");
         return;
    }

    auto item = audioEngine_->playlist().currentItem();
    if (!item) {
        overlayEngine_->setAlignedLyrics({});
        return;
    }

    // Extract clip ID with aggressive UUID detection
    std::string clipId = extractClipIdFromTrack();
    
    // Fallback to last requested ID if extraction fails and playlist is in transition
    if (clipId.empty() && !lastRequestedClipId_.empty()) {
        LOG_DEBUG("SunoController: Using last requested ID {} as fallback", lastRequestedClipId_);
        clipId = lastRequestedClipId_;
    }
    
    // Update last requested ID for future fallback
    if (!clipId.empty()) {
        lastRequestedClipId_ = clipId;
    }

    if (clipId.empty()) {
        LOG_DEBUG("SunoController: Could not extract clip ID from track, clearing lyrics");
        overlayEngine_->setAlignedLyrics({});
        return;
    }

    LOG_INFO("SunoController: Track changed, looking up lyrics for clip ID: {}", clipId);

    // PRIORITY 1: Check direct cache first (survives track restarts during session)
    auto cacheIt = directLyricsCache_.find(clipId);
    if (cacheIt != directLyricsCache_.end()) {
        LOG_INFO("SunoController: Using cached lyrics for {}", clipId);
        overlayEngine_->setAlignedLyrics(cacheIt->second);
        return;
    }

    // PRIORITY 2: Try database via getLyrics
    auto res = getLyrics(clipId);
    if (res.isOk()) {
        LOG_INFO("SunoController: Displaying lyrics from database for {}", clipId);
        overlayEngine_->setAlignedLyrics(res.value());
        // Cache for future restarts
        directLyricsCache_[clipId] = res.value();
        return;
    }

    // PRIORITY 3: Try loading from local sidecar files
    fs::path trackPath = item->isRemote ? fs::path() : item->path;
    if (!trackPath.empty()) {
        fs::path dir = trackPath.parent_path();
        std::string stem = trackPath.stem().string();
        
        // Try loading .srt file
        fs::path srtPath = dir / (stem + ".srt");
        if (fs::exists(srtPath)) {
            std::ifstream file(srtPath);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                auto lyrics = LyricsAligner::parseSrt(content);
                if (!lyrics.empty()) {
                    LOG_INFO("SunoController: Loaded lyrics from SRT file for {}", clipId);
                    lyrics.songId = clipId;
                    overlayEngine_->setAlignedLyrics(lyrics);
                    directLyricsCache_[clipId] = lyrics;
                    return;
                }
            }
        }
        
        // Try loading .json file
        fs::path jsonPath = dir / (stem + ".json");
        if (fs::exists(jsonPath)) {
            std::ifstream file(jsonPath);
            if (file) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                auto words = LyricsAligner::parseJson(QByteArray::fromStdString(content));
                if (!words.empty()) {
                    LOG_INFO("SunoController: Loaded lyrics from JSON file for {}", clipId);
                    AlignedLyrics lyrics;
                    lyrics.songId = clipId;
                    lyrics.words = words;
                    // Create simple line structure from words
                    AlignedLine line;
                    line.start_s = words.front().start_s;
                    line.end_s = words.back().end_s;
                    for (const auto& w : words) {
                        line.text += w.word + " ";
                    }
                    lyrics.lines.push_back(line);
                    overlayEngine_->setAlignedLyrics(lyrics);
                    directLyricsCache_[clipId] = lyrics;
                    return;
                }
            }
        }
    }

    // PRIORITY 4: Auto-fetch missing lyrics from API
    if (client_->isAuthenticated()) {
        LOG_INFO("SunoController: Lyrics missing for playing track {}, fetching from API...", clipId);
        client_->fetchAlignedLyrics(clipId);
    } else {
        LOG_DEBUG("SunoController: No lyrics available for {} and not authenticated", clipId);
    }
    
    // Clear overlay since we don't have lyrics yet
    overlayEngine_->setAlignedLyrics({});
}

std::string SunoController::extractClipIdFromTrack() const {
    auto item = audioEngine_->playlist().currentItem();
    if (!item) return "";

    // PRIORITY 1: Check metadata tag if available
    if (!item->metadata.sunoClipId.empty()) {
        return item->metadata.sunoClipId;
    }

    // PRIORITY 2: UUID extraction with multiple patterns
    static const std::regex uuidRegex(
        "([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})",
        std::regex::icase);
    std::smatch match;

    if (item->isRemote) {
        // Check URL for UUID
        if (std::regex_search(item->url, match, uuidRegex)) {
            return match[1].str();
        }
    } else {
        // AGGRESSIVE: Check multiple sources for local files
        
        // 2a: Filename contains UUID
        std::string filename = item->path.filename().string();
        if (std::regex_search(filename, match, uuidRegex)) {
            return match[1].str();
        }
        
        // 2b: Full path contains UUID (some downloaders include ID in folder)
        std::string fullPath = item->path.string();
        if (std::regex_search(fullPath, match, uuidRegex)) {
            return match[1].str();
        }
        
        // 2c: Parent directory name contains UUID
        if (item->path.has_parent_path()) {
            std::string parentName = item->path.parent_path().filename().string();
            if (std::regex_search(parentName, match, uuidRegex)) {
                return match[1].str();
            }
        }
    }

    // PRIORITY 3: Title-based fuzzy matching against accumulated clips
    std::string currentTitle = item->title();
    if (!currentTitle.empty()) {
        // Exact match first
        for (const auto& clip : accumulatedClips_) {
            if (clip.title == currentTitle) {
                return clip.id;
            }
        }
        
        // Fuzzy match: substring comparison
        for (const auto& clip : accumulatedClips_) {
            if (!clip.title.empty()) {
                if (currentTitle.find(clip.title) != std::string::npos ||
                    clip.title.find(currentTitle) != std::string::npos) {
                    LOG_DEBUG("SunoController: Fuzzy matched title '{}' to clip {}", 
                              currentTitle, clip.id);
                    return clip.id;
                }
            }
        }
    }

    // No ID found
    return "";
}

void SunoController::setDebugLyrics(const AlignedLyrics& lyrics) {
    LOG_INFO("SunoController: Forcing debug lyrics ({} lines)", lyrics.lines.size());
    overlayEngine_->setAlignedLyrics(lyrics);
}

void SunoController::onWavConversionReady(const std::string& clipId,
                                           const std::string& wavUrl) {
    LOG_INFO("SunoController: WAV conversion ready for {} at {}", clipId, wavUrl);
    
    // Find the clip in accumulated clips
    for (const auto& clip : accumulatedClips_) {
        if (clip.id == clipId) {
            downloadAudioFromUrl(clipId, wavUrl, ".wav");
            return;
        }
    }
    
    // If not found in accumulated clips, try to get from database
    auto clipOpt = db_.getClip(clipId);
    if (clipOpt.isOk() && clipOpt.value()) {
        downloadAudioFromUrl(clipId, wavUrl, ".wav");
    } else {
        LOG_ERROR("SunoController: Cannot download WAV - clip {} not found", clipId);
    }
}

void SunoController::downloadAudioFromUrl(const std::string& clipId,
                                          const std::string& url,
                                          const std::string& extension) {
    LOG_INFO("SunoController: Downloading audio from {} with extension {}", url, extension);
    
    QUrl qurl(QString::fromStdString(url));
    QNetworkRequest request(qurl);
    
    QNetworkReply* reply = networkManager_->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, clipId, extension]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoController: Audio download failed: {}",
                      reply->errorString().toStdString());
            return;
        }
        
        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        vc::file::ensureDir(downloadDir);
        
        // Get clip info for filename
        std::string safeTitle = clipId;
        for (const auto& clip : accumulatedClips_) {
            if (clip.id == clipId) {
                safeTitle = clip.title;
                break;
            }
        }
        
        std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
        if (safeTitle.empty()) safeTitle = clipId;
        
        QString fileName = QString::fromStdString(safeTitle) + QString::fromStdString(extension);
        fs::path filePath = downloadDir / fileName.toStdString();
        
        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            LOG_INFO("SunoController: Saved audio to {}", filePath.string());
            
            // Find the clip and process it
            for (const auto& clip : accumulatedClips_) {
                if (clip.id == clipId) {
                    processDownloadedFile(clip, filePath);
                    saveMetadataSidecar(clip);
                    break;
                }
            }
        } else {
            LOG_ERROR("SunoController: Failed to open file for writing: {}",
                      filePath.string());
        }
    });
}

void SunoController::saveMetadataSidecar(const SunoClip& clip) {
    fs::path downloadDir = CONFIG.suno().downloadPath;
    if (downloadDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        downloadDir = fs::path(musicLoc.toStdString());
    }
    
    std::string safeTitle = clip.title;
    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    if (safeTitle.empty()) safeTitle = clip.id;
    
    fs::path txtPath = downloadDir / (safeTitle + ".txt");
    
    std::ofstream file(txtPath);
    if (!file) {
        LOG_ERROR("SunoController: Failed to create metadata sidecar: {}", txtPath.string());
        return;
    }
    
    file << "Title: " << clip.title << "\n";
    file << "Artist: " << clip.display_name << "\n";
    file << "Track ID: " << clip.id << "\n";
    file << "Duration: " << clip.metadata.duration << "\n";
    file << "BPM: " << clip.metadata.bpm << "\n";
    file << "Key: " << clip.metadata.key << "\n";
    file << "Model: " << clip.model_name << " " << clip.major_model_version << "\n";
    file << "Created: " << clip.created_at << "\n";
    file << "\n";
    file << "Tags/Styles: " << clip.metadata.tags << "\n";
    file << "\n";
    file << "Prompt:\n" << clip.metadata.prompt << "\n";
    file << "\n";
    file << "Lyrics:\n" << clip.metadata.lyrics << "\n";
    file << "\n";
    file << "Cover Art URL: " << clip.image_url << "\n";
    file << "Audio URL: " << clip.audio_url << "\n";
    
    file.close();
    LOG_INFO("SunoController: Saved metadata sidecar to {}", txtPath.string());
}

} // namespace vc::suno
