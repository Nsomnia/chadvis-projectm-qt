#include "SunoLibrarySync.hpp"
#include "core/Logger.hpp"
#include <QTimer>

namespace vc::suno {

SunoLibrarySync::SunoLibrarySync(SunoClient* client, SunoDatabase& db)
    : client_(client), db_(db) {
    auto cachedClips = db_.getAllClips();
    if (cachedClips.isOk() && !cachedClips.value().empty()) {
        LOG_INFO("SunoLibrarySync: Loaded {} cached clips", cachedClips.value().size());
        accumulatedClips_ = cachedClips.value();
    }
}

void SunoLibrarySync::startSync(int page) {
    if (page == 1) {
        accumulatedClips_.clear();
        isSyncing_ = true;
        currentSyncPage_ = 1;
    }
    
    std::string msg = "Syncing Suno library (Page " + std::to_string(page) + ")";
    if (!accumulatedClips_.empty()) {
        msg += " - " + std::to_string(accumulatedClips_.size()) + " clips found...";
    }
    statusMessage.emitSignal(msg);
    client_->fetchLibrary(page);
}

void SunoLibrarySync::onLibraryFetched(const std::vector<SunoClip>& clips) {
    LOG_INFO("SunoLibrarySync: Fetched {} clips", clips.size());
    
    for (const auto& clip : clips) {
        accumulatedClips_.push_back(clip);
    }
    db_.saveClips(clips);
    
    if (clips.size() >= 20) {
        currentSyncPage_++;
        startSync(currentSyncPage_);
    } else {
        isSyncing_ = false;
        currentSyncPage_ = 1;
        LOG_INFO("SunoLibrarySync: Sync complete. Total: {}", accumulatedClips_.size());
        libraryUpdated.emitSignal(accumulatedClips_);
        statusMessage.emitSignal("Suno library sync complete (" + std::to_string(accumulatedClips_.size()) + " clips)");

        for (const auto& clip : accumulatedClips_) {
            auto lyricsRes = db_.getAlignedLyrics(clip.id);
            if (lyricsRes.isErr() || lyricsRes.value().empty()) {
                lyricsQueue_.push_back(clip.id);
            }
        }
        
        totalLyricsToFetch_ = lyricsQueue_.size();
        lyricsSyncStartTime_ = std::chrono::steady_clock::now();
        processQueue();
    }
}

void SunoLibrarySync::processQueue() {
    if (isRefreshingToken_) return;

    while (activeLyricsRequests_ < 3 && !lyricsQueue_.empty()) {
        std::string id = lyricsQueue_.front();
        lyricsQueue_.pop_front();
        activeLyricsRequests_++;
        
        int jitter = 50 + (rand() % 200);
        size_t remaining = lyricsQueue_.size();
        
        QTimer::singleShot(jitter, this, [this, id, remaining]() {
            LOG_INFO("SunoLibrarySync: Fetching lyrics for {} (Queue: {})", id, remaining);
            client_->fetchAlignedLyrics(id);
        });
    }
}

void SunoLibrarySync::onAlignedLyricsFetched(const std::string& clipId, const std::string& json) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    if (totalLyricsToFetch_ > 0) {
        size_t processed = totalLyricsToFetch_ - lyricsQueue_.size();
        size_t remaining = lyricsQueue_.size();
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lyricsSyncStartTime_).count();
        
        std::string etaStr;
        if (processed > 0 && elapsed > 0) {
            double rate = static_cast<double>(processed) / elapsed;
            if (rate > 0) {
                int etaSec = static_cast<int>(remaining / rate);
                etaStr = " - ETA: " + std::to_string(etaSec / 60) + "m " + std::to_string(etaSec % 60) + "s";
            }
        }
        
        std::string status = "Syncing lyrics: " + std::to_string(processed) + "/" + 
                             std::to_string(totalLyricsToFetch_) + 
                             " (" + std::to_string(processed * 100 / totalLyricsToFetch_) + "%)" + etaStr;
        statusMessage.emitSignal(status);
    }

    processQueue();
    LOG_INFO("SunoLibrarySync: Fetched lyrics for {}", clipId);
    db_.saveAlignedLyrics(clipId, json);
    clipUpdated.emitSignal(clipId);
}

void SunoLibrarySync::onError(const std::string& message) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);
    
    if (totalLyricsToFetch_ > 0) {
        size_t processed = totalLyricsToFetch_ - lyricsQueue_.size();
        std::string status = "Syncing lyrics: " + std::to_string(processed) + "/" + 
                             std::to_string(totalLyricsToFetch_) + " (Retrying...)";
        statusMessage.emitSignal(status);
    }
    
    if (message.rfind("Lyrics processing:", 0) == 0) {
        std::string id = message.substr(18);
        if (!id.empty()) {
            LOG_INFO("SunoLibrarySync: Re-queueing lyrics for {}", id);
            lyricsQueue_.push_back(id);
        }
    }
    else if (message.find("Unauthorized") != std::string::npos || message.find("401") != std::string::npos) {
        LOG_WARN("SunoLibrarySync: Auth error, pausing queue");
        isRefreshingToken_ = true;
    }
    
    processQueue();
    LOG_ERROR("SunoLibrarySync: {}", message);
    statusMessage.emitSignal(message);
}

} // namespace vc::suno
