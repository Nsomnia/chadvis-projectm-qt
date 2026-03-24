#include "suno/SunoLibraryManager.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc::suno {

SunoLibraryManager::SunoLibraryManager(SunoClient* client, SunoDatabase& db, QObject* parent)
    : QObject(parent), client_(client), db_(db) {
    
    // Connect client signals
    client_->libraryFetched.connect([this](const auto& clips) { 
        onLibraryFetched(clips); 
    });
}

SunoLibraryManager::~SunoLibraryManager() = default;

void SunoLibraryManager::refreshLibrary(int page) {
    if (!client_->isAuthenticated()) {
        if (!CONFIG.suno().cookie.empty()) {
            client_->setCookie(CONFIG.suno().cookie);
        }
        if (!CONFIG.suno().token.empty()) {
            client_->setToken(CONFIG.suno().token);
        }
    }

    if (!client_->isAuthenticated()) {
        emit authenticationRequired();
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

void SunoLibraryManager::syncDatabase(bool forceAuth) {
    if (forceAuth) {
        emit authenticationRequired();
    } else {
        refreshLibrary(1);
    }
}

void SunoLibraryManager::onLibraryFetched(const std::vector<SunoClip>& clips) {
    LOG_INFO("SunoLibraryManager: Fetched {} clips", clips.size());
    
    // Accumulate clips for this sync session
    for (const auto& clip : clips) {
        accumulatedClips_.push_back(clip);
    }
    
    db_.saveClips(clips);
    
    if (clips.size() >= 20) {
        // More pages to fetch
        currentSyncPage_++;
        refreshLibrary(currentSyncPage_);
    } else {
        isSyncing_ = false;
        currentSyncPage_ = 1;
        LOG_INFO("SunoLibraryManager: Sync complete. Total clips: {}", accumulatedClips_.size());
        
        libraryUpdated.emitSignal(accumulatedClips_);
        statusMessage.emitSignal("Suno library sync complete (" + std::to_string(accumulatedClips_.size()) + " clips)");
    }
}

} // namespace vc::suno
