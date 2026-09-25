#include "suno/SunoLibraryManager.hpp"
#include "core/Logger.hpp"

#include <QTimer>
#include <QString>

namespace vc::suno {

SunoLibraryManager::SunoLibraryManager(SunoClient* client, SunoDatabase& db, QObject* parent)
    : QObject(parent), client_(client), db_(db) {
    
    // Connect client signals
    client_->libraryFetched.connect([this](const auto& clips) { 
        onLibraryFetched(clips); 
    });

    connect(client_, &SunoClient::credentialRestoreCompleted,
            this, &SunoLibraryManager::onCredentialsRestored);

    // Any terminal fetch failure must clear the "syncing" gate and
    // surface a libraryFetchFailed so Bridge can clear its spinner.
    client_->errorOccurred.connect([this](const std::string& err) {
        if (isSyncing_) {
            isSyncing_ = false;
            const bool hadMore = hasMorePages_;
            hasMorePages_ = false;
            if (hadMore) emit hasMorePagesChanged();
            emit libraryFetchFailed(QString::fromStdString(err));
        }
    });
    connect(client_, &SunoClient::needsReauth, this, [this]() {
        if (isSyncing_) {
            isSyncing_ = false;
            const bool hadMore = hasMorePages_;
            hasMorePages_ = false;
            if (hadMore) emit hasMorePagesChanged();
            emit libraryFetchFailed(QStringLiteral("needsReauth"));
        }
    });
    connect(client_, &SunoClient::authStateChanged, this, [this]() {
        // If we were syncing and auth just went to NeedsReauth/Disconnected,
        // treat as a failure so the spinner does not stick.
        if (isSyncing_ && client_->authState() != auth::AuthState::ActiveValid) {
            isSyncing_ = false;
            const bool hadMore = hasMorePages_;
            hasMorePages_ = false;
            if (hadMore) emit hasMorePagesChanged();
            emit libraryFetchFailed(QStringLiteral("authStateChanged"));
        }
    });
}

SunoLibraryManager::~SunoLibraryManager() = default;

void SunoLibraryManager::refreshLibrary(int page) {
  if (isSyncing_ || credentialRefreshPending_) {
    return;
  }

  pendingPage_ = page;
  credentialRefreshPending_ = true;
  client_->reloadStoredCredentials();
}

void SunoLibraryManager::onCredentialsRestored() {
  if (!credentialRefreshPending_) {
    return;
  }

  credentialRefreshPending_ = false;
  const int page = pendingPage_;

  if (!client_->isAuthenticated()) {
    emit authenticationRequired();
    {
        const bool hadMore = hasMorePages_;
        hasMorePages_ = false;
        if (hadMore) emit hasMorePagesChanged();
    }
    isSyncing_ = false;
    emit libraryFetchFailed(QStringLiteral("Not authenticated"));
    return;
  }

  if (page <= 1) {
    accumulatedClips_.clear();
    pagesLoaded_ = 0;
    isSyncing_ = true;
    hasMorePages_ = true;
    emit hasMorePagesChanged();

    std::string msg = "Syncing Suno library";
    emit statusMessage(msg);
  } else {
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
  client_->fetchLibraryPage(client_->nextCursor(), 20, searchText_);
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
    emit statusMessage("Suno library: " + std::to_string(accumulatedClips_.size()) + " clips loaded (more available)");
    // Auto-page through the entire library (rate-limited). QML infinite
    // scroll remains as a fallback if auto-pagination is interrupted, but
    // the primary path now syncs the full library without manual scrolling.
    // Respect the SunoClient 1 Hz politeness limiter.
    QTimer::singleShot(1100, this, [this]() { requestNextPage(); });
  } else {
    LOG_INFO("SunoLibraryManager: Sync complete after {} page(s). Total clips: {}",
             pagesLoaded_, accumulatedClips_.size());
    emit statusMessage("Suno library sync complete (" + std::to_string(accumulatedClips_.size()) + " clips)");
  }
}

} // namespace vc::suno
