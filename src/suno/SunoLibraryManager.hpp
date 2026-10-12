/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "util/Result.hpp"

namespace vc::suno {

class SunoLibraryManager : public QObject {
    Q_OBJECT

public:
    explicit SunoLibraryManager(SunoClient* client, SunoDatabase& db, QObject* parent = nullptr);
    ~SunoLibraryManager() override;

    void refreshLibrary(int page = 1);
    /// Fetch the next feed page using the client's next_cursor (no-op when
    /// the current page reports exhaustion or a fetch is in flight).
    void requestNextPage();
    /// Set the local filter used by the bridge; it is not sent to the feed.
    void setSearchText(const QString& text);
    void syncDatabase(bool forceAuth);

    // Accessors
    const std::vector<SunoClip>& accumulatedClips() const { return accumulatedClips_; }
    void clearAccumulatedClips() { accumulatedClips_.clear(); }
    bool hasMorePages() const { return hasMorePages_; }
    int currentPage() const { return pagesLoaded_; }

signals:
    void statusMessage(const std::string& message);
    void libraryUpdated(const std::vector<SunoClip>& clips);
    void clipUpdated(const std::string& clipId);
    void authenticationRequired();
    void hasMorePagesChanged();
    void libraryFetchFailed(const QString& reason);
    /// One sync session ended, and this is the only place that says whether it
    /// ended COMPLETELY (T0167).
    ///
    /// `libraryUpdated` fires after every page with the accumulated list, so by
    /// the time a sync dies mid-way the UI has already received a partial
    /// library that looks identical to a finished one -- the failure arrives on
    /// a different channel and nothing connects the two. This fires ONCE per
    /// sync, on every exit path: `complete` true only when the feed itself said
    /// the cursor was exhausted; false for a transport failure or an
    /// unparseable page, with `reason` carrying which.
    ///
    /// `expectedTotal` is deliberately absent. The captured /api/feed/v3
    /// contract carries no total (ENDPOINT-INVENTORY.md 5.2: clips, has_more,
    /// next_cursor -- nothing else), so any number here would be invented, and
    /// an invented total that agrees with a truncated sync is precisely the
    /// lie this signal exists to stop telling. `elapsedMs` and the page count
    /// are the only honest measurements available.
    void librarySyncSettled(bool complete, int pagesFetched, int clipCount,
                            qint64 elapsedMs, const QString& reason);

private:
    SunoClient* client_;
    SunoDatabase& db_;

    /// Retained per the vc::Signal contract (Signal.hpp): the signal keeps no
    /// lifetime link to its subscribers, so these ids are disconnected in the
    /// destructor -- otherwise a SunoClient outliving this manager calls a
    /// freed `this` on its next emit.
    std::size_t libraryFetchedConnectionId_{0};
    std::size_t errorOccurredConnectionId_{0};

    std::vector<SunoClip> accumulatedClips_;
    bool isSyncing_ = false;
    bool credentialRefreshPending_ = false;
    int pendingPage_ = 1;
    int pagesLoaded_ = 0; ///< Number of feed pages pulled this sync session
    bool hasMorePages_ = false;
    QString searchText_; ///< Local filter text; never sent to the feed
    /// T0167: one verdict per refresh attempt. Reset when an attempt starts,
    /// set by settleSync, so a page that trips two failure channels reports once.
    bool syncSettled_ = true;
    /// T0167: when the current sync started, so a settled report can say how
    /// long it ran. Invalid means no sync is or was in flight.
    QElapsedTimer syncClock_;
    /// Retained per the vc::Signal contract, like the two ids above: the
    /// library-page failure channel keeps no lifetime link to its subscriber.
    std::size_t libraryPageFailedConnectionId_{0};

    void onCredentialsRestored();
    void onLibraryFetched(const std::vector<SunoClip>& clips);
    /// T0167: the single exit from a sync session. Every end -- feed exhausted,
    /// transport failure, unparseable page, lost session -- goes through here,
    /// so completeness is reported exactly once per sync and never contradicted
    /// by a second, differing verdict.
    void settleSync(bool complete, const QString& reason);
};

} // namespace vc::suno
