#include "suno/SunoLyricsManager.hpp"
#include <QTimer>
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace vc::suno {

SunoLyricsManager::SunoLyricsManager(SunoClient* client, SunoDatabase& db, QObject* parent)
    : QObject(parent), client_(client), db_(db) {
    client_->alignedLyricsFetched.connect(
            [this](const auto& id, const auto& json) { onAlignedLyricsFetched(id, json); });
    // The concurrency counter decrements only on lyrics-scoped outcomes. The
    // global errorOccurred broadcast also fires for unrelated failures —
    // explore, notifications, uploads — and riding it let any error free a
    // lyrics slot, overshooting the cap.
    client_->lyricsFetchFailed.connect(
            [this](const auto& id, const auto& msg) { onLyricsFetchFailed(id, msg); });

    // A dead session stops the lyrics queue through the real channel.
    // SunoClient classifies auth failures from the HTTP status alone and
    // emits needsReauth exactly when the touch/retry chain is exhausted
    // ("user must supply fresh credentials" — SunoClient.hpp). The old path
    // substring-matched "401"/"Unauthorized" inside the global errorOccurred
    // broadcast, so a byte count or clip id containing those digits cleared
    // the queue.
    connect(client_, &SunoClient::needsReauth, this, [this]() {
        LOG_WARN("SunoLyricsManager: session lost - clearing lyrics queue");
        lyricsQueue_.clear();
        activeLyricsRequests_ = 0;
    });
}
SunoLyricsManager::~SunoLyricsManager() = default;

void SunoLyricsManager::queueLyricsFetch(const std::string& clipId) {
    // Duplicates and over-cap arrivals are refused here — the queue this
    // class used to run was unbounded and undeduplicated, which is what let
    // the page storm grow it without limit.
    if (!lyricsQueue_.tryEnqueue(clipId)) {
        return;
    }
    totalLyricsToFetch_++;
    if (activeLyricsRequests_ == 0) {
        lyricsSyncStartTime_ = std::chrono::steady_clock::now();
    }
    processQueue();
}

void SunoLyricsManager::processQueue() {
    // Limit concurrent requests to 3 to be nicer to API and avoid rate limits
    while (activeLyricsRequests_ < 3 && !lyricsQueue_.empty()) {
        auto next = lyricsQueue_.pop();
        if (!next) break;
        std::string id = std::move(*next);
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
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lyricsSyncStartTime_)
                               .count();

        std::string etaStr = "";
        if (processed > 0 && elapsed > 0) {
            double rate = static_cast<double>(processed) / elapsed;
            if (rate > 0) {
                int etaSec = static_cast<int>(remaining / rate);
                int etaMin = etaSec / 60;
                etaStr = " - ETA: " + std::to_string(etaMin) + "m " + std::to_string(etaSec % 60) +
                         "s";
            }
        }

        std::string status = "Syncing lyrics: " + std::to_string(processed) + "/" +
                             std::to_string(totalLyricsToFetch_) + " (" +
                             std::to_string(processed * 100 / totalLyricsToFetch_) + "%)" + etaStr;
        emit statusMessage(status);
    }

    processQueue();
    emit lyricsFetched(clipId, json);
}

void SunoLyricsManager::onLyricsFetchFailed(const std::string& clipId, const std::string& message) {
    activeLyricsRequests_ = std::max(0, activeLyricsRequests_ - 1);

    // "Lyrics processing:" is the server's still-aligning marker, requeued
    // for a later pass. No code in this tree currently emits that marker
    // (verified 2026-10-09) — the branch is retained for the shape's return;
    // pop() removing the id from the dedup set is exactly what lets the
    // requeue re-enter. The clip id rides the signal now instead of being
    // scraped back out of the message text.
    if (message.rfind("Lyrics processing:", 0) == 0) {
        if (!clipId.empty()) {
            LOG_INFO("SunoLyricsManager: Re-queueing processing lyrics for {}", clipId);
            (void)lyricsQueue_.tryEnqueue(clipId);
        }
    }

    processQueue();
    emit errorOccurred(message);
}

} // namespace vc::suno
