#include "suno/SunoLyricsManager.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <QTimer>

namespace vc::suno {

SunoLyricsManager::SunoLyricsManager(SunoClient* client, SunoDatabase& db, QObject* parent)
    : QObject(parent), client_(client), db_(db) {
    
    client_->alignedLyricsFetched.connect([this](const auto& id, const auto& json) {
        onAlignedLyricsFetched(id, json);
    });
    
    client_->errorOccurred.connect([this](const auto& msg) {
        onError(msg);
    });
    
    // Monitor token changes to resume queue
    client_->tokenChanged.connect([this](const auto& token) {
        if (isRefreshingToken_) {
            isRefreshingToken_ = false;
            if (!token.empty()) {
                LOG_INFO("SunoLyricsManager: Resuming lyrics queue after token refresh");
                processQueue();
            }
        }
    });
}

SunoLyricsManager::~SunoLyricsManager() = default;

void SunoLyricsManager::queueLyricsFetch(const std::string& clipId) {
    lyricsQueue_.push_back(clipId);
    totalLyricsToFetch_++;
    if (activeLyricsRequests_ == 0) {
        lyricsSyncStartTime_ = std::chrono::steady_clock::now();
    }
    processQueue();
}

void SunoLyricsManager::processQueue() {
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
            LOG_INFO("SunoLyricsManager: Fetching lyrics for {} (Queue: {})", id, remaining);
            client_->fetchAlignedLyrics(id);
        });
    }
}

void SunoLyricsManager::onAlignedLyricsFetched(const std::string& clipId, const std::string& json) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    // Update status
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

    processQueue();
    lyricsFetched.emitSignal(clipId, json);
}

void SunoLyricsManager::onError(const std::string& message) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    // Check for "Lyrics processing" -> Re-queue
    if (message.rfind("Lyrics processing:", 0) == 0) {
        std::string id = message.substr(18); 
        if (!id.empty()) {
            LOG_INFO("SunoLyricsManager: Re-queueing processing lyrics for {}", id);
            lyricsQueue_.push_back(id);
        }
    }
    // Check for Unauthorized/401 -> Pause
    else if (message.find("Unauthorized") != std::string::npos || 
             message.find("401") != std::string::npos) {
        
        LOG_WARN("SunoLyricsManager: Auth error detected. Pausing queue for refresh.");
        
        if (!isRefreshingToken_) {
            isRefreshingToken_ = true;
            client_->refreshAuthToken([this](bool success) {
                if (!success) {
                    LOG_ERROR("SunoLyricsManager: Token refresh failed.");
                    isRefreshingToken_ = false;
                    lyricsQueue_.clear();
                    activeLyricsRequests_ = 0;
                }
            });
        }
    }
    
    processQueue();
    errorOccurred.emitSignal(message);
}

} // namespace vc::suno
