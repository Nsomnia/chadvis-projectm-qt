#include "suno/SunoLibraryManager.hpp"
#include "core/Logger.hpp"

#include <QString>
#include <QTimer>

namespace vc::suno {

namespace {

/// Clips per feed request. The server honours `limit` up to 100 and rejects 200
/// with a 400 (measured 2026-10-25 with this app's own bearer, on both API
/// bases: 20/50/100 returned exactly that many clips, 200 answered
/// `400 Bad Request`); omitting it yields the wire's own default of 20.
///
/// The number matters because the first sync pages the WHOLE library: 2 147
/// clips is 108 round trips at 20 (each one delayed by the client's 1 Hz
/// politeness limiter, so ~6 minutes on every launch) and 22 at 100. It is a
/// request-size constant, not a contract: a server that starts capping the page
/// simply returns fewer clips, and `has_more` still decides when to stop.
constexpr int kLibraryPageSize = 100;

} // namespace

SunoLibraryManager::SunoLibraryManager(SunoClient* client, SunoDatabase& db, QObject* parent)
    : QObject(parent), client_(client), db_(db) {
    // Connect client signals. The vc::Signal ids are retained as members and
    // disconnected in the destructor; the signal itself tracks no lifetimes.
    libraryFetchedConnectionId_ =
            client_->libraryFetched.connect([this](const auto& clips) { onLibraryFetched(clips); });

    connect(client_, &SunoClient::credentialRestoreCompleted, this,
            &SunoLibraryManager::onCredentialsRestored);

    // Any terminal fetch failure must clear the "syncing" gate and
    // surface a libraryFetchFailed so Bridge can clear its spinner.
    errorOccurredConnectionId_ = client_->errorOccurred.connect([this](const std::string& err) {
        if (isSyncing_) {
            settleSync(false, QString::fromStdString(err));
        }
    });
    // T0167: the library-page failure channel. Before this existed a malformed
    // envelope was reported as an EMPTY SUCCESS, which left hasMorePages_ true
    // from stale client state and re-requested the same cursor forever. Both
    // failure channels converge on settleSync so a failed page cannot be read
    // as a finished sync.
    libraryPageFailedConnectionId_ =
            client_->libraryPageFailed.connect([this](const std::string& err) {
                if (isSyncing_) {
                    settleSync(false, QString::fromStdString(err));
                }
            });
    connect(client_, &SunoClient::needsReauth, this, [this]() {
        if (credentialRefreshPending_) {
            onCredentialsRestored();
        } else if (isSyncing_) {
            settleSync(false, QStringLiteral("needsReauth"));
        }
    });
    connect(client_, &SunoClient::authStateChanged, this, [this]() {
        if (credentialRefreshPending_) {
            if (client_->authState() == auth::AuthState::ActiveValid ||
                client_->authState() == auth::AuthState::Disconnected) {
                onCredentialsRestored();
            }
            return;
        }
        if (isSyncing_ && client_->authState() != auth::AuthState::ActiveValid) {
            settleSync(false, QStringLiteral("authStateChanged"));
        }
    });
}

SunoLibraryManager::~SunoLibraryManager() {
    // Safe only because the client outlives this manager: every construction
    // site (SunoController.cpp:61, test_SunoLibraryManager.cpp:69) declares
    // the SunoClient before constructing this class, so destruction -- the
    // reverse of declaration order -- reaches here while the Signals are
    // still alive. The inverse order would dangle these disconnects, which is
    // why the ownership graph, not this class, owns that guarantee. The Qt
    // connects on client_ are severed automatically by the QObject context
    // object; only the vc::Signal slots need this.
    client_->libraryFetched.disconnect(libraryFetchedConnectionId_);
    client_->errorOccurred.disconnect(errorOccurredConnectionId_);
    client_->libraryPageFailed.disconnect(libraryPageFailedConnectionId_);
}

void SunoLibraryManager::beginFreshSearchFeed() {
    pendingSearchRestart_ = false;
    pendingPage_ = 1;
    syncSettled_ = false;
    credentialRefreshPending_ = true;
    client_->reloadStoredCredentials();
}

void SunoLibraryManager::setSearchText(const QString& searchText) {
    const QString term = searchText.trimmed();
    if (term == searchText_) {
        return;
    }
    searchText_ = term;
    ++searchGeneration_;
    // A term change never appends: the previous accumulation is discarded
    // and the new feed starts from a null cursor.
    accumulatedClips_.clear();
    pagesLoaded_ = 0;
    if (credentialRefreshPending_) {
        // The imminent page-one fetch already picks up the newest term, so
        // no deferred restart is needed; just retarget it to page one and
        // block the auto-page timer from paging the old cursor meanwhile.
        pendingPage_ = 1;
        const bool hadMore = hasMorePages_;
        hasMorePages_ = false;
        if (hadMore != hasMorePages_) {
            emit hasMorePagesChanged();
        }
        return;
    }
    if (isSyncing_) {
        // The in-flight page was issued under the old term. Defer the new
        // feed until it settles; onLibraryFetched/settleSync discard the
        // stale result via the generation check and begin the fresh feed.
        // Block the auto-page timer from paging the old cursor meanwhile.
        pendingSearchRestart_ = true;
        pendingPage_ = 1;
        const bool hadMore = hasMorePages_;
        hasMorePages_ = false;
        if (hadMore != hasMorePages_) {
            emit hasMorePagesChanged();
        }
        return;
    }
    const bool hadMore = hasMorePages_;
    hasMorePages_ = false;
    if (hadMore != hasMorePages_) {
        emit hasMorePagesChanged();
    }
    beginFreshSearchFeed();
}

void SunoLibraryManager::refreshLibrary(int page) {
    if (isSyncing_ || credentialRefreshPending_) {
        return;
    }

    pendingPage_ = page;
    syncSettled_ = false;
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
        settleSync(false, QStringLiteral("Not authenticated"));
        return;
    }

    if (page <= 1) {
        // A deferred restart (if any) is satisfied by this fetch: it carries
        // the current searchText_ from page one.
        pendingSearchRestart_ = false;
        accumulatedClips_.clear();
        pagesLoaded_ = 0;
        isSyncing_ = true;
        hasMorePages_ = true;
        emit hasMorePagesChanged();
        syncClock_.start();

        std::string msg = "Syncing Suno library";
        emit statusMessage(msg);
    } else {
        requestNextPage();
        return;
    }

    fetchGeneration_ = searchGeneration_;
    fetchInFlight_ = true;
    client_->fetchLibraryPage(std::nullopt, kLibraryPageSize, searchText_);
}

void SunoLibraryManager::requestNextPage() {
    if (pendingSearchRestart_) {
        // The auto-page timer must not page the old term's cursor after a
        // term change; start the new term's feed from page one instead.
        beginFreshSearchFeed();
        return;
    }
    if (isSyncing_ || credentialRefreshPending_ || !hasMorePages_) {
        return;
    }
    if (client_->nextCursor().isEmpty()) {
        // No cursor means the feed is exhausted even if has_more was stale.
        hasMorePages_ = false;
        emit hasMorePagesChanged();
        return;
    }

    isSyncing_ = true;
    // Resuming after a mid-sync failure (T0334): re-arm the one-verdict guard
    // so the resumed leg reports its final verdict once. The clock keeps
    // running, so elapsed covers the whole session including the retry gap.
    syncSettled_ = false;
    emit statusMessage("Fetching more Suno clips (page " +
                       std::to_string(pagesLoaded_ + 1) + ")...");
    fetchGeneration_ = searchGeneration_;
    fetchInFlight_ = true;
    client_->fetchLibraryPage(client_->nextCursor(), kLibraryPageSize, searchText_);
}


void SunoLibraryManager::syncDatabase(bool forceAuth) {
    if (forceAuth) {
        emit authenticationRequired();
    } else {
        refreshLibrary(1);
    }
}

void SunoLibraryManager::onLibraryFetched(const std::vector<SunoClip>& clips) {
    // Stale page: issued under a previous search term (T0366). Discard it
    // without appending or emitting, and start the new term's feed. This is
    // what keeps a term change mid-sync from appending old results.
    if (fetchInFlight_ && fetchGeneration_ != searchGeneration_) {
        fetchInFlight_ = false;
        isSyncing_ = false;
        if (pendingSearchRestart_) {
            beginFreshSearchFeed();
        }
        return;
    }
    fetchInFlight_ = false;
    LOG_INFO("SunoLibraryManager: Fetched {} clips", clips.size());

    // Accumulate clips for this sync session
    for (const auto& clip : clips) {
        accumulatedClips_.push_back(clip);
    }

    pagesLoaded_++;
    // Page complete: allow the next requestNextPage() through.
    isSyncing_ = false;

    if (auto result = db_.saveClips(clips); !result) {
        LOG_ERROR("SunoLibraryManager: Failed to persist library page: {}", result.error().message);
    }

    // Pagination truth comes straight from the feed/v3 envelope now.
    const bool hadMore = hasMorePages_;
    hasMorePages_ = client_->hasMorePages() && !client_->nextCursor().isEmpty();
    if (hadMore != hasMorePages_) {
        emit hasMorePagesChanged();
    }

    // Emit incremental update after every page so UI can render progressively
    emit libraryUpdated(accumulatedClips_);

    if (hasMorePages_) {
        // T0334: surface the honest progress number -- pages fetched plus
        // clips so far. The feed carries no total, so no expectedTotal exists
        // to divide by; never invent one.
        emit statusMessage("Suno library: " + std::to_string(accumulatedClips_.size()) +
                           " clips across " + std::to_string(pagesLoaded_) +
                           " pages (more available)");
        // Auto-page through the entire library (rate-limited). QML infinite
        // scroll remains as a fallback if auto-pagination is interrupted, but
        // the primary path now syncs the full library without manual scrolling.
        // Respect the SunoClient 1 Hz politeness limiter.
        QTimer::singleShot(1100, this, [this]() { requestNextPage(); });
    } else {
        LOG_INFO("SunoLibraryManager: Sync complete after {} page(s). Total clips: {}",
                 pagesLoaded_, accumulatedClips_.size());
        emit statusMessage("Suno library sync complete (" +
                           std::to_string(accumulatedClips_.size()) + " clips across " +
                           std::to_string(pagesLoaded_) + " pages)");
        // T0167: the one exit that means what it says. The feed's own cursor
        // said exhausted, every requested page arrived, and nothing failed.
        settleSync(true, QStringLiteral());
    }
}

void SunoLibraryManager::settleSync(bool complete, const QString& reason) {
    // Stale failure: the page that failed was issued under a previous term.
    // Do not attribute its failure to the new term or mark the new term
    // resumable from the old cursor; discard and start the new feed.
    if (fetchInFlight_ && fetchGeneration_ != searchGeneration_) {
        fetchInFlight_ = false;
        isSyncing_ = false;
        if (pendingSearchRestart_) {
            beginFreshSearchFeed();
        }
        return;
    }
    fetchInFlight_ = false;
    // At most one verdict per refresh attempt: two failure channels can fire
    // for one page (a transport error followed by its parse failure), and a
    // second settle must not double-count or re-report a sync that already
    // ended. Keyed on an explicit flag rather than isSyncing_ because the
    // "not authenticated" path ends an attempt that never got far enough to
    // raise the syncing gate -- gating on isSyncing_ would swallow it, which a
    // test caught immediately.
    if (syncSettled_) {
        return;
    }
    syncSettled_ = true;
    isSyncing_ = false;
    // Resume, not restart (T0334): a failure after at least one page, with the
    // client still holding a next cursor, keeps hasMorePages_ true so
    // requestNextPage() retries the FAILED page from that cursor. pagesLoaded_
    // and the accumulated clips already survive this point, so nothing fetched
    // is discarded. A page-1 failure or an empty cursor has nothing to resume.
    const bool resumable = !complete && pagesLoaded_ > 0 && !client_->nextCursor().isEmpty();
    const bool hadMore = hasMorePages_;
    hasMorePages_ = resumable;
    if (hadMore != hasMorePages_) emit hasMorePagesChanged();
    if (!complete) {
        emit libraryFetchFailed(reason);
    }
    emit librarySyncSettled(complete, pagesLoaded_,
                            static_cast<int>(accumulatedClips_.size()),
                            syncClock_.isValid() ? syncClock_.elapsed() : qint64(0), reason);
}

} // namespace vc::suno
