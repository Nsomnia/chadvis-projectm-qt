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
    hasMorePages_ = true;
    emit hasMorePagesChanged();
  }

  currentSyncPage_ = page;

  std::string msg = "Syncing Suno library (Page " + std::to_string(page) + ")";
  if (!accumulatedClips_.empty()) {
    msg += " - " + std::to_string(accumulatedClips_.size()) + " clips found so far...";
  }
  emit statusMessage(msg);
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

  const bool hadMore = hasMorePages_;
  hasMorePages_ = clips.size() >= 20;
  if (hadMore != hasMorePages_) {
    emit hasMorePagesChanged();
  }

  // Emit incremental update after every page so UI can render progressively
  emit libraryUpdated(accumulatedClips_);

  if (hasMorePages_) {
    // More pages available — but don't auto-fetch; let QML trigger next page
    emit statusMessage("Suno library: " + std::to_string(accumulatedClips_.size()) + " clips loaded (more available)");
  } else {
    isSyncing_ = false;
    currentSyncPage_ = 1;
    LOG_INFO("SunoLibraryManager: Sync complete. Total clips: {}", accumulatedClips_.size());
    emit statusMessage("Suno library sync complete (" + std::to_string(accumulatedClips_.size()) + " clips)");
  }
}

} // namespace vc::suno
