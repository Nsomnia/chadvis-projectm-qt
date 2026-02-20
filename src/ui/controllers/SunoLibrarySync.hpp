#pragma once
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "util/Signal.hpp"
#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace vc::suno {

class SunoLibrarySync : public QObject {
    Q_OBJECT

public:
    explicit SunoLibrarySync(SunoClient* client, SunoDatabase& db);
    ~SunoLibrarySync() override = default;

    void startSync(int page = 1);
    bool isSyncing() const { return isSyncing_; }
    size_t queueSize() const { return lyricsQueue_.size(); }
    void processQueue();

    const std::vector<SunoClip>& clips() const { return accumulatedClips_; }
    std::vector<SunoClip>& clips() { return accumulatedClips_; }
    void clearClips() { accumulatedClips_.clear(); }

    Signal<const std::vector<SunoClip>&> libraryUpdated;
    Signal<const std::string&> statusMessage;
    Signal<const std::string&> clipUpdated;

public slots:
    void onLibraryFetched(const std::vector<SunoClip>& clips);
    void onAlignedLyricsFetched(const std::string& clipId, const std::string& json);
    void onError(const std::string& message);

    void pauseForTokenRefresh() { isRefreshingToken_ = true; }
    void resumeAfterTokenRefresh() { isRefreshingToken_ = false; processQueue(); }

private:
    SunoClient* client_;
    SunoDatabase& db_;
    
    std::vector<SunoClip> accumulatedClips_;
    std::deque<std::string> lyricsQueue_;
    
    int activeLyricsRequests_{0};
    int currentSyncPage_{1};
    bool isSyncing_{false};
    bool isRefreshingToken_{false};
    size_t totalLyricsToFetch_{0};
    std::chrono::steady_clock::time_point lyricsSyncStartTime_;
};

} // namespace vc::suno
