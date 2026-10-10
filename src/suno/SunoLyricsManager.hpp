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
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"

namespace vc::suno {

/// Bounded, deduplicated FIFO of clip ids awaiting lyrics fetches. Pure and
/// unit-testable on its own: the manager itself is not constructible in a
/// test (its client owns a keychain-reading worker), but this component is
/// the whole enqueue policy — a page pass may not double-enqueue an id, the
/// pending set may not grow without limit, and a "still processing" requeue
/// must be able to return an id that just popped.
class LyricsFetchQueue {
public:
    /// Drop-new, never drop-old: FIFO order is the fairness contract. False
    /// means duplicate-in-set or the cap is full.
    bool tryEnqueue(const std::string& clipId, const std::size_t cap) {
        if (pendingSet_.count(clipId) > 0 || pending_.size() >= cap) return false;
        pending_.push_back(clipId);
        pendingSet_.insert(clipId);
        return true;
    }
    bool tryEnqueue(const std::string& clipId) { return tryEnqueue(clipId, kDefaultCap); }

    /// Pops the head; the id leaves the dedup set, which is exactly what lets
    /// a "still processing" requeue re-enter.
    std::optional<std::string> pop() {
        if (pending_.empty()) return std::nullopt;
        std::string id = std::move(pending_.front());
        pending_.pop_front();
        pendingSet_.erase(id);
        return id;
    }

    void clear() {
        pending_.clear();
        pendingSet_.clear();
    }

    [[nodiscard]] bool empty() const { return pending_.empty(); }
    [[nodiscard]] std::size_t size() const { return pending_.size(); }
    [[nodiscard]] bool contains(const std::string& clipId) const {
        return pendingSet_.count(clipId) > 0;
    }

    /// Generous by design: the page-diff in SunoController already makes the
    /// steady state one enqueue per clip, so this is a valve against
    /// pathological re-entry, not a feature limit. 1000 ids is ~64 KB.
    static constexpr std::size_t kDefaultCap = 1000;

private:
    std::deque<std::string> pending_;
    std::unordered_set<std::string> pendingSet_;
};

class SunoLyricsManager : public QObject {
    Q_OBJECT

public:
    explicit SunoLyricsManager(SunoClient* client, SunoDatabase& db, QObject* parent = nullptr);
    ~SunoLyricsManager() override;

    void queueLyricsFetch(const std::string& clipId);
    void processQueue();

signals:
    void statusMessage(const std::string& message);
    void lyricsFetched(const std::string& clipId, const std::string& json);
    void errorOccurred(const std::string& message);

private:
    SunoClient* client_;
    SunoDatabase& db_;

    LyricsFetchQueue lyricsQueue_;
    int activeLyricsRequests_{0};
    size_t totalLyricsToFetch_{0};
    std::chrono::steady_clock::time_point lyricsSyncStartTime_;

    void onAlignedLyricsFetched(const std::string& clipId, const std::string& json);
    void onLyricsFetchFailed(const std::string& clipId, const std::string& message);
};

} // namespace vc::suno
