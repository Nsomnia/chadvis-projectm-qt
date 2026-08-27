/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QObject>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace vc::suno {

namespace fs = std::filesystem;

/// Lifecycle of a single download item. Carried through signals as a plain
/// int so QML consumers do not need metatype registration for the enum.
enum class DownloadState : int {
    Queued = 0,
    Downloading = 1,
    Completed = 2,
    FailedRetryable = 3,  // retries exhausted; a later re-enqueue may succeed
    FailedPermanent = 4,  // server refused (4xx); retrying will not help
    Cancelled = 5,
};

[[nodiscard]] constexpr const char* toString(DownloadState state) {
    switch (state) {
        case DownloadState::Queued: return "queued";
        case DownloadState::Downloading: return "downloading";
        case DownloadState::Completed: return "completed";
        case DownloadState::FailedRetryable: return "failed-retryable";
        case DownloadState::FailedPermanent: return "failed-permanent";
        case DownloadState::Cancelled: return "cancelled";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool isTerminal(DownloadState state) {
    return state == DownloadState::Completed || state == DownloadState::FailedRetryable ||
           state == DownloadState::FailedPermanent || state == DownloadState::Cancelled;
}

/// How a finished reply should be treated by the scheduler.
enum class FailureKind { None, Cancelled, Retryable, Permanent };

/// Pure classification: HTTP 5xx retryable; 4xx permanent except 408/429;
/// transport-level errors retryable except auth/permission denials.
[[nodiscard]] FailureKind classifyFailure(QNetworkReply::NetworkError err, int httpStatus);

/// Pure exponential backoff ladder: 1s / 4s / 16s (capped) for attempts 0,1,2...
[[nodiscard]] constexpr std::int64_t backoffBaseMs(int attemptZeroBased) {
    if (attemptZeroBased < 0) attemptZeroBased = 0;
    if (attemptZeroBased > 2) attemptZeroBased = 2;
    return 1000LL << (2 * attemptZeroBased);
}

/// Backoff with uniform jitter in +/-20% so parallel failures do not sync up.
[[nodiscard]] std::int64_t backoffWithJitterMs(int attemptZeroBased, std::mt19937& rng);

/// Injectable seam: unit tests hand back scripted fake replies and never
/// touch the network. Production uses the single adopted QNetworkAccessManager.
using ReplyFactory = std::function<QNetworkReply*(const QNetworkRequest&)>;

/// Bounded-concurrency FIFO download scheduler with automatic retry,
/// HTTP-range resume, cancellation, and .part atomic-rename finalization.
class DownloadQueue : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultMaxConcurrent = 3;
    static constexpr int kMaxAttempts = 3;
    static constexpr const char* kPartSuffix = ".part";

    /// Production constructor. Adopts an existing manager (reparented here)
    /// or lazily creates exactly one; ad-hoc per-download managers are banned.
    explicit DownloadQueue(QNetworkAccessManager* adoptedManager = nullptr,
                           QObject* parent = nullptr);
    /// Test constructor: replies come from the injected factory only.
    explicit DownloadQueue(ReplyFactory factory, QObject* parent = nullptr);
    ~DownloadQueue() override;

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    void setMaxConcurrent(int maxConcurrent);
    [[nodiscard]] int maxConcurrent() const { return maxConcurrent_; }

    /// FIFO enqueue. Duplicate ids among live items are rejected gracefully.
    bool enqueue(std::string clipId,
                 std::string url,
                 std::filesystem::path destPath,
                 QVariantMap metadata = {});

    /// Aborts the in-flight reply and dequeues queued items for this id.
    /// Graceful no-op when the id is unknown or already terminal.
    bool cancel(const std::string& clipId);

    [[nodiscard]] bool isEmpty() const { return pending_.empty() && waiting_.empty() && active_.empty(); }
    [[nodiscard]] int activeCount() const { return static_cast<int>(active_.size()); }
    [[nodiscard]] int queuedCount() const { return static_cast<int>(pending_.size()); }
    [[nodiscard]] int waitingCount() const { return static_cast<int>(waiting_.size()); }

signals:
    /// Per-item progress/state feed (percent is 0..100 while downloading).
    void itemStateChanged(const QString& clipId, int state, int progressPercent);
    /// Emitted once whenever the last item drains out of the queue.
    void queueIdle();

private:
    struct Item {
        std::string clipId;
        std::string url;
        std::filesystem::path destPath;
        QVariantMap metadata;
        DownloadState state{DownloadState::Queued};
        int attempts{0};          // completed attempts (failures consumed)
        qint64 rangeOffset{0};    // bytes pre-existing in .part for this attempt
        int progressPercent{-1};
        bool resumable{false};    // peer advertised Accept-Ranges on a failure
        bool awaitingStatusCheck{false};  // first bytes of a resumed request
        bool finishing{false};    // swallow further callbacks for this reply
        bool cancelRequested{false};
        QPointer<QNetworkReply> reply;
        QFile partFile;
    };

    [[nodiscard]] std::filesystem::path partPathFor(const std::filesystem::path& dest) const;

    Item* findItem(const std::string& clipId);
    std::unique_ptr<Item> takeFromActive(const std::string& clipId);
    QNetworkReply* makeReply(const QNetworkRequest& request);
    void pump();
    void startItem(Item& item);
    void onData(Item& item);
    void onProgress(Item& item, qint64 received, qint64 total);
    void onFinished(Item& item, QNetworkReply& reply);
    void handleFailure(Item& item, QNetworkReply* reply, FailureKind kind);
    void restartFromScratch(Item& item, QNetworkReply& reply);
    void finishCancelled(Item& item, QNetworkReply& reply);
    void finalizeSuccess(Item& item, QNetworkReply& reply);
    void resumeWaiting(const std::string& clipId);
    void setState(Item& item, DownloadState state);
    void retire(const std::string& clipId);
    void checkIdle();

    ReplyFactory factory_;
    QNetworkAccessManager* nam_{nullptr};   // owned via QObject parenting
    int maxConcurrent_{kDefaultMaxConcurrent};
    bool idleEmitted_{true};
    std::mt19937 rng_{std::random_device{}()};

    // FIFO order preserved in pending_; waiting_ holds items inside their
    // exponential-backoff delay; active_ holds open network transfers.
    std::deque<std::unique_ptr<Item>> pending_;
    std::vector<std::unique_ptr<Item>> waiting_;
    std::vector<std::unique_ptr<Item>> active_;
};

} // namespace vc::suno
