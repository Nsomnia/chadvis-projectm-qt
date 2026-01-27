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
        if (!CONFIG.suno().email.empty() && !CONFIG.suno().password.empty()) {
            LOG_INFO("SunoController: Attempting automated login...");
            client_->login(CONFIG.suno().email, CONFIG.suno().password, [this, page](bool success) {
                if (success) {
                    LOG_INFO("SunoController: Automated login successful");
                    CONFIG.suno().cookie = client_->getCookie();
                    CONFIG.save(CONFIG.configPath());
                    refreshLibrary(page);
                } else {
                    LOG_ERROR("SunoController: Automated login failed");
                    showCookieDialog();
                }
            });
            return;
        }
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
        if (dialog->isLoginMode()) {
            std::string email = dialog->getEmail().toStdString();
            std::string password = dialog->getPassword().toStdString();
            
            CONFIG.suno().email = email;
            CONFIG.suno().password = password;
            CONFIG.save(CONFIG.configPath());
            
            LOG_INFO("SunoController: Attempting login from dialog...");
            client_->login(email, password, [this](bool success) {
                if (success) {
                    LOG_INFO("SunoController: Login from dialog successful");
                    CONFIG.suno().cookie = client_->getCookie();
                    CONFIG.save(CONFIG.configPath());
                    // Start fresh sync
                    accumulatedClips_.clear();
                    isSyncing_ = true;
                    client_->fetchLibrary(1);
                } else {
                    LOG_ERROR("SunoController: Login from dialog failed");
                    statusMessage.emitSignal("Login failed. Please check your credentials.");
                    showCookieDialog(); // Show again
                }
            });
        } else {
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
            double rate = static_cast<double>(processed) / elapsed; // items per second
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
    db_.saveAlignedLyrics(clipId, json);
    clipUpdated.emitSignal(clipId);

    // Parse JSON first to get lyrics object
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    
    // Check if this is the currently playing song before updating overlay
    bool isCurrent = false;
    
    // Robust check for local files using metadata if possible
    if (auto item = audioEngine_->playlist().currentItem()) {
        if (item->isRemote) {
            if (item->url.find(clipId) != std::string::npos) {
                isCurrent = true;
            }
        } else {
             // For local files, check filename OR metadata tag matching
             // 1. Filename contains UUID
             if (item->path.string().find(clipId) != std::string::npos) {
                 isCurrent = true;
             } 
             // 2. Title match (if available from playlist item)
             else if (item->title().find(clipId) != std::string::npos) {
                 isCurrent = true;
             }
             // 3. Fallback: If title matches the title in our accumulated clips for this ID
             else {
                 for (const auto& clip : accumulatedClips_) {
                     if (clip.id == clipId) {
                         // Fuzzy title match? Or exact?
                         // Local file might be "Song Title.mp3", clip.title is "Song Title"
                         // Simple substring check
                         std::string itemTitle = item->title();
                         if (!itemTitle.empty() && !clip.title.empty()) {
                             if (itemTitle.find(clip.title) != std::string::npos || 
                                 clip.title.find(itemTitle) != std::string::npos) {
                                 isCurrent = true;
                             }
                         }
                         break;
                     }
                 }
             }
        }
    }
    
    // Only update overlay if it's the current song
    if (!isCurrent) {
        LOG_DEBUG("SunoController: Fetched lyrics for {} but it's not playing (ignored)", clipId);
        return;
    }
    
    if (CONFIG.suno().debugLyrics) {
        LOG_DEBUG("SunoController: Ignoring fetched lyrics for {} because debugLyrics is active", clipId);
        return;
    }

    if (doc.isArray() || doc.isObject()) {
        std::vector<AlignedWord> words;

        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else {
            QJsonObject obj = doc.object();
            if (obj.contains("aligned_words") && obj["aligned_words"].isArray()) {
                arr = obj["aligned_words"].toArray();
            } else if (obj.contains("words") && obj["words"].isArray()) {
                arr = obj["words"].toArray();
            } else if (obj.contains("lyrics") && obj["lyrics"].isArray()) {
                arr = obj["lyrics"].toArray();
            } else if (obj.contains("aligned_lyrics") && obj["aligned_lyrics"].isArray()) {
                arr = obj["aligned_lyrics"].toArray();
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
        
        // Check if we should save lyrics to disk
        if (CONFIG.suno().saveLyrics) {
            fs::path downloadDir = CONFIG.suno().downloadPath;
            if (downloadDir.empty()) {
                QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
                if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
                downloadDir = fs::path(musicLoc.toStdString());
            }

            std::string safeTitle = clipId;
            for (const auto& clip : accumulatedClips_) {
                if (clip.id == clipId) {
                    safeTitle = clip.title;
                    break;
                }
            }
            std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
            std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
            
            fs::path jsonPath = downloadDir / (safeTitle + ".json");
            fs::path srtPath = downloadDir / (safeTitle + ".srt");
            
            if (fs::exists(downloadDir)) {
                 // Check if audio exists to confirm we should save
                 fs::path audioPath = downloadDir / (safeTitle + ".mp3");
                 if (CONFIG.suno().autoDownload || fs::exists(audioPath)) {
                     std::ofstream jf(jsonPath);
                     if (jf) jf << json;
                     
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

    if (clip.audio_url.empty()) {
        LOG_INFO("SunoController: Resolving clip details for ID {}", clip.id);
        
        SunoClip resolvedClip = clip;
        resolvedClip.audio_url = "https://cdn1.suno.ai/" + clip.id + ".mp3";
        resolvedClip.title = clip.id;
        
        LOG_INFO("SunoController: Queueing lyrics fetch for resolved ID {}", clip.id);
        client_->fetchAlignedLyrics(clip.id);
        
        downloadAudio(resolvedClip);
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

    fs::path targetPath = downloadDir / (safeTitle + ".mp3");

    // If exists, play local
    if (fs::exists(targetPath)) {
        LOG_INFO("SunoController: Playing local file: {}", targetPath.string());
        audioEngine_->playlist().addFile(targetPath);
        audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
        return;
    }

    downloadAudio(clip);
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
        overlayEngine_->setAlignedLyrics({}); // Clear
        return;
    }

    std::string clipId;
    
    if (!item->metadata.sunoClipId.empty()) {
        clipId = item->metadata.sunoClipId;
    } 
    else {
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
    }
    
    if (clipId.empty()) {
        std::string currentTitle = item->title();
        if (!currentTitle.empty()) {
            for (const auto& clip : accumulatedClips_) {
                if (clip.title == currentTitle) {
                    clipId = clip.id;
                    break;
                }
            }
        }
    }

    if (!clipId.empty()) {
        auto res = getLyrics(clipId);
        if (res.isOk()) {
            LOG_INFO("SunoController: Displaying lyrics for {}", clipId);
            overlayEngine_->setAlignedLyrics(res.value());
            return;
        } else {
            // Auto-fetch missing lyrics when track plays
            if (client_->isAuthenticated()) {
                LOG_INFO("SunoController: Lyrics missing for playing track {}, fetching...", clipId);
                client_->fetchAlignedLyrics(clipId);
            }
        }
    }
    
    // Clear if no lyrics found
    overlayEngine_->setAlignedLyrics({});
}

void SunoController::setDebugLyrics(const AlignedLyrics& lyrics) {
    LOG_INFO("SunoController: Forcing debug lyrics ({} lines)", lyrics.lines.size());
    overlayEngine_->setAlignedLyrics(lyrics);
}

} // namespace vc::suno
