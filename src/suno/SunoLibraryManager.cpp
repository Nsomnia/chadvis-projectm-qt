#include "suno/SunoLibraryManager.hpp"
#include "core/Logger.hpp"

#include <QString>

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
  // Pick up credentials pasted/changed via settings before each sync.
  client_->reloadStoredCredentials();

  if (!client_->isAuthenticated()) {
    emit authenticationRequired();
    return;
  }

  // Page 1 (or any fresh sync) resets accumulation; higher page numbers are
  // legacy "load more" calls and now just continue from the cursor.
  if (page <= 1) {
    accumulatedClips_.clear();
    pagesLoaded_ = 0;
    isSyncing_ = true;
    hasMorePages_ = true;
    emit hasMorePagesChanged();

    std::string msg = "Syncing Suno library";
    emit statusMessage(msg);
  } else {
    // Legacy page-number callers just continue from the cursor.
    requestNextPage();
    return;
  }

  client_->fetchLibraryPage(std::nullopt, 20, searchText_);
}

void SunoLibraryManager::requestNextPage() {
  if (isSyncing_ || !hasMorePages_) {
    return;
  }
  if (client_->nextCursor().isEmpty()) {
    // No cursor means the feed is exhausted even if has_more was stale.
    hasMorePages_ = false;
    emit hasMorePagesChanged();
    return;
  }

  isSyncing_ = true;
  emit statusMessage("Fetching more Suno clips...");
  client_->fetchLibraryPage(client_->nextCursor());
}

void SunoLibraryManager::setSearchText(const QString& text) {
  if (searchText_ == text) return;
  searchText_ = text;
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

  pagesLoaded_++;
  // Page complete: allow the next requestNextPage() through.
  isSyncing_ = false;

  db_.saveClips(clips);

  // Pagination truth comes straight from the feed/v3 envelope now.
  const bool hadMore = hasMorePages_;
  hasMorePages_ = client_->hasMorePages() && !client_->nextCursor().isEmpty();
  if (hadMore != hasMorePages_) {
    emit hasMorePagesChanged();
  }

  // Emit incremental update after every page so UI can render progressively
  emit libraryUpdated(accumulatedClips_);

  if (hasMorePages_) {
    // More pages available — but don't auto-fetch; let QML trigger next page
    emit statusMessage("Suno library: " + std::to_string(accumulatedClips_.size()) + " clips loaded (more available)");
  } else {
    LOG_INFO("SunoLibraryManager: Sync complete after {} page(s). Total clips: {}",
             pagesLoaded_, accumulatedClips_.size());
    emit statusMessage("Suno library sync complete (" + std::to_string(accumulatedClips_.size()) + " clips)");
  }
}

} // namespace vc::suno
