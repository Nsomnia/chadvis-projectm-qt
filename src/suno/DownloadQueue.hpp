/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace vc::suno {

namespace fs = std::filesystem;

/// Lifecycle of a single download item. Carried through signals as a plain
/// int so QML consumers do not need metatype registration for the enum.
enum class DownloadState : int {
    Queued = 0,
    Downloading = 1,
    Completed = 2,
    FailedRetryable = 3, // retries exhausted; a later re-enqueue may succeed
    FailedPermanent = 4, // server refused (4xx); retrying will not help
    Cancelled = 5,
};

[[nodiscard]] constexpr const char* toString(DownloadState state) {
    switch (state) {
        case DownloadState::Queued:
            return "queued";
        case DownloadState::Downloading:
            return "downloading";
        case DownloadState::Completed:
            return "completed";
        case DownloadState::FailedRetryable:
            return "failed-retryable";
        case DownloadState::FailedPermanent:
            return "failed-permanent";
        case DownloadState::Cancelled:
            return "cancelled";
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

/// Bounds applied to a server-supplied `Retry-After`, in milliseconds. The
/// ceiling is the point of the clamp: `Retry-After: 3600` on a wedged host
/// would otherwise hold one of `maxConcurrent_` (default 3) slots for an hour,
/// which is the wedge this class exists to prevent. The floor keeps a server
/// that says "retry immediately" from spinning the ladder at line rate.
inline constexpr std::int64_t kMinRetryAfterMs = 1000;
inline constexpr std::int64_t kMaxRetryAfterMs = 60000;

/// Parse an RFC 9110 `Retry-After` value: either delta-seconds or an HTTP-date.
/// `std::nullopt` means "unparseable, use the ladder" -- never a guess, and
/// never an exception. `nowEpochSecs` is injected so the HTTP-date form is
/// testable without freezing a clock.
[[nodiscard]] std::optional<std::int64_t> parseRetryAfter(const QByteArray& headerValue,
                                                          std::int64_t nowEpochSecs);

/// Delay before the next attempt. A server hint wins over the ladder, clamped
/// to [kMinRetryAfterMs, kMaxRetryAfterMs]. The hint gets *positive* jitter
/// only (0..+20%): it is a floor the server asked us to respect, and subtracting
/// from it would defeat the purpose of sending it. The ladder keeps symmetric
/// +/-20% because that value is our own choice.
[[nodiscard]] std::int64_t
retryDelayMs(int attemptZeroBased, std::optional<std::int64_t> retryAfterSecs, std::mt19937& rng);

/// Outcome of one `QIODevice::write()` on the scratch file.
enum class SinkWrite {
    /// Every byte landed.
    Complete,
    /// Some but not all bytes landed. The file is now a prefix of the payload,
    /// so it must never be renamed into place.
    Short,
    /// The device refused outright (-1).
    Failed,
};

/// Pure classification of a sink write, mirroring `vc::WriteOutcome` for
/// FFmpeg muxer returns. `QFile::write()` returns the byte count actually
/// written and -1 on error, so both damage modes collapse to this one test.
[[nodiscard]] constexpr SinkWrite classifySinkWrite(qint64 written, qint64 wanted) {
    if (written < 0) return SinkWrite::Failed;
    return (written >= wanted) ? SinkWrite::Complete : SinkWrite::Short;
}

/// Why the scratch path could not be claimed for an attempt.
enum class PartOpenError {
    /// Another live writer holds an exclusive lock on the path right now.
    Collision,
    /// The open failed for a reason other than "already exists", or the open
    /// itself failed. Nothing was created.
    Failed,
};

[[nodiscard]] constexpr const char* toString(PartOpenError error) {
    switch (error) {
        case PartOpenError::Collision:
            return "collision";
        case PartOpenError::Failed:
            return "failed";
    }
    return "unknown";
}

/// User-facing wording for the self-inflicted failure reasons. Exported and
/// pure so the text is asserted by a test instead of only appearing in a log
/// nobody reads; each names the clip, because "download failed" with no
/// subject is indistinguishable across a batch of ten.
[[nodiscard]] std::string byteCapReason(std::string_view clipId, qint64 limitBytes,
                                        qint64 seenBytes);
[[nodiscard]] std::string shortWriteReason(std::string_view clipId, qint64 offsetBytes,
                                           qint64 wantedBytes, qint64 writtenBytes,
                                           std::string_view deviceError);
/// A 2xx that delivered fewer bytes than the transfer promised. Retryable
/// wording, not terminal: a truncated connection is a transient condition.
[[nodiscard]] std::string shortDownloadReason(std::string_view clipId, qint64 receivedBytes,
                                              qint64 expectedBytes);

/// Injectable seam: unit tests hand back scripted fake replies and never
/// touch the network. Production uses the single adopted QNetworkAccessManager.
using ReplyFactory = std::function<QNetworkReply*(const QNetworkRequest&)>;

/// Injectable seam for the scratch sink. Production returns a QFile opened
/// `WriteOnly|NewOnly` and locked; the test constructor may substitute any
/// QIODevice, which is the only portable way to induce a *write* failure --
/// filling a real filesystem is not something a unit test may do. The path
/// argument is passed so an injected opener can still honour it.
using PartOpener = std::function<std::unique_ptr<QIODevice>(const std::string& partPath)>;

/// Bounded-concurrency FIFO download scheduler with automatic retry,
/// cancellation, and .part atomic-rename finalization.
class DownloadQueue : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultMaxConcurrent = 3;
    static constexpr int kMaxAttempts = 3;
    static constexpr const char* kPartSuffix = ".part";

    /// Idle timeout applied to the media request, in milliseconds.
    ///
    /// Qt's own `DefaultTransferTimeoutConstant` is 30000, and its documented
    /// semantics are "abort the transfer if *no data is exchanged*" -- the timer
    /// is restarted on every chunk, so this is a stall deadline, NOT a total
    /// transfer deadline. That distinction is the whole reason setting it here
    /// is safe: a 200 MB clip on a slow link may take ten minutes of wall clock
    /// and must not be killed for it, whereas a socket that has gone quiet for
    /// 30 s is dead. 30 s also keeps the worst-case wedge bounded -- three
    /// attempts of 30 s plus the 1 s + 4 s ladder puts a stuck slot back in the
    /// pool inside ~95 s, where an unbounded wait never comes back at all.
    ///
    /// The complementary guard is the byte cap: this catches a dead socket,
    /// and the cap catches a server that keeps trickling. Neither catches the
    /// other, which is why both exist.
    static constexpr int kDefaultTransferTimeoutMs = 30'000;

    /// Hard ceiling on one item's bytes. A hostile or broken origin can stream
    /// forever, and with `maxConcurrent_` at 3 an unbounded item is a disk-wedge
    /// long before it is a memory problem. 256 MiB is ~13x the largest sane
    /// payload this queue carries (an 8-minute 320 kbps track is 19.2 MB), so it
    /// never fires on legitimate media, while still capping the worst case at
    /// 768 MiB across the default concurrency. Configurable because a caller
    /// rendering lossless masters legitimately needs a different ceiling; zero
    /// disables the cap.
    static constexpr qint64 kDefaultMaxBytesPerItem = 256LL * 1024 * 1024;

    /// Production constructor. Adopts an existing manager (reparented here)
    /// or lazily creates exactly one; ad-hoc per-download managers are banned.
    explicit DownloadQueue(QNetworkAccessManager* adoptedManager = nullptr,
                           QObject* parent = nullptr);
    /// Test constructor: replies come from the injected factory only.
    explicit DownloadQueue(ReplyFactory factory, QObject* parent = nullptr);
    /// Test constructor that also substitutes the scratch sink, so a write()
    /// failure is reachable without a full disk. Default opener = production.
    DownloadQueue(ReplyFactory factory, PartOpener partOpener, QObject* parent = nullptr);
    ~DownloadQueue() override;

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    void setMaxConcurrent(int maxConcurrent);
    [[nodiscard]] int maxConcurrent() const { return maxConcurrent_; }

    /// Stall deadline in ms; 0 disables it. Applies to the next attempt of
    /// every item, live ones included at their next request build.
    void setTransferTimeoutMs(int ms);
    [[nodiscard]] int transferTimeoutMs() const { return transferTimeoutMs_; }

    /// Per-item byte ceiling; 0 or negative disables it.
    void setMaxBytesPerItem(qint64 bytes);
    [[nodiscard]] qint64 maxBytesPerItem() const { return maxBytesPerItem_; }

    /// FIFO enqueue. Duplicate ids among live items are rejected gracefully.
    bool enqueue(std::string clipId, std::string url, std::filesystem::path destPath,
                 QVariantMap metadata = {});

    /// Aborts the in-flight reply and dequeues queued items for this id.
    /// Graceful no-op when the id is unknown or already terminal.
    bool cancel(const std::string& clipId);

    [[nodiscard]] bool isEmpty() const {
        return pending_.empty() && waiting_.empty() && active_.empty();
    }
    [[nodiscard]] int activeCount() const { return static_cast<int>(active_.size()); }
    [[nodiscard]] int queuedCount() const { return static_cast<int>(pending_.size()); }
    [[nodiscard]] int waitingCount() const { return static_cast<int>(waiting_.size()); }

signals:
    /// Per-item progress/state feed (percent is 0..100 while downloading).
    void itemStateChanged(const QString& clipId, int state, int progressPercent);
    /// Emitted once whenever the last item drains out of the queue.
    void queueIdle();

private:
    /// Why an attempt is being torn down before the reply has finished. Kept on
    /// the item rather than passed down because aborting a reply re-enters
    /// through finished(), and the classification that must win there is ours,
    /// not whatever error the transport happens to report.
    enum class AbortCause { None, ByteCap, ShortWrite, ShortDownload, Collision, OpenFailed };

    /// Shared by the containers and by every reply callback, which holds only a
    /// `std::weak_ptr`. That is not a style preference: retire() drops the
    /// containers' last reference on the same call that publishes a terminal
    /// state, so a reply callback that arrives afterwards finds an expired weak
    /// pointer and does nothing, instead of dereferencing freed memory. The
    /// three connections in startItem outlive the Item by construction.
    struct Item {
        std::string clipId;
        std::string url;
        std::filesystem::path destPath;
        QVariantMap metadata;
        DownloadState state{DownloadState::Queued};
        int attempts{0}; // completed attempts (failures consumed)
        int progressPercent{-1};
        qint64 bytesReceived{0}; // bytes this queue accepted from the reply
        /// Bytes the transfer promised for this attempt: the downloadProgress
        /// total when the transport reported one, else Content-Length at
        /// finalize time. 0 means no total was ever known -- the one shape the
        /// short-body guard must NOT fail.
        qint64 expectedTotal{0};
        bool finishing{false}; // swallow further callbacks for this reply
        bool cancelRequested{false};
        /// True only between a successful exclusive open and the next rename.
        /// Every `.part` removal is gated on this, so the queue can only ever
        /// delete scratch space it created -- a path someone else holds a lock
        /// on is not ours to unlink.
        bool ownsPart{false};
        AbortCause abortCause{AbortCause::None};
        std::string abortReason;
        QPointer<QNetworkReply> reply;
        std::unique_ptr<QIODevice> partFile;
    };

    [[nodiscard]] std::filesystem::path partPathFor(const std::filesystem::path& dest) const;

    /// Raw pointer for synchronous lookups only -- always while one of the
    /// containers still holds a reference.
    Item* findItem(const std::string& clipId);
    std::shared_ptr<Item> takeFromActive(const std::string& clipId);
    QNetworkReply* makeReply(const QNetworkRequest& request);
    void pump();
    void startItem(const std::shared_ptr<Item>& item);
    /// Claims the scratch path and produces the sink. Reclaim-vs-collide is
    /// resolved here, not by the caller; `item.abortReason` carries the
    /// user-facing sentence for a failure.
    std::expected<std::unique_ptr<QIODevice>, PartOpenError> openPart(Item& item);
    void onData(Item& item);
    void onProgress(Item& item, qint64 received, qint64 total);
    void onFinished(Item& item, QNetworkReply& reply);
    void handleFailure(Item& item, QNetworkReply* reply, FailureKind kind);
    /// Closes the sink without unlinking, so finalizeSuccess can rename the
    /// scratch file into place. Every non-success path uses dropPart() instead.
    void closeSink(Item& item);
    /// Closes and removes the `.part`, if and only if this queue created it.
    void dropPart(Item& item);
    /// Records a self-inflicted cause and aborts the reply, letting finished()
    /// apply the right state. Aborting is how the reply is torn down; it is
    /// never the classification. Only valid once item.reply is set.
    void failItem(Item& item, AbortCause cause, std::string reason);
    void finishCancelled(Item& item, QNetworkReply& reply);
    void finalizeSuccess(Item& item, QNetworkReply& reply);
    void resumeWaiting(const std::string& clipId);
    void setState(Item& item, DownloadState state);
    void retire(const std::string& clipId);
    void checkIdle();

    ReplyFactory factory_;
    PartOpener partOpener_;
    QNetworkAccessManager* nam_{nullptr}; // owned via QObject parenting
    int maxConcurrent_{kDefaultMaxConcurrent};
    int transferTimeoutMs_{kDefaultTransferTimeoutMs};
    qint64 maxBytesPerItem_{kDefaultMaxBytesPerItem};
    bool idleEmitted_{true};
    std::mt19937 rng_{std::random_device{}()};

    // FIFO order preserved in pending_; waiting_ holds items inside their
    // exponential-backoff delay; active_ holds open network transfers.
    std::deque<std::shared_ptr<Item>> pending_;
    std::vector<std::shared_ptr<Item>> waiting_;
    std::vector<std::shared_ptr<Item>> active_;
};

} // namespace vc::suno