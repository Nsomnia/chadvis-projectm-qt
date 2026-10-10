#include "suno/DownloadQueue.hpp"

#include "core/Logger.hpp"

#include <QDateTime>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <random>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace vc::suno {

namespace {

// ── Advisory-lock primitives ─────────────────────────────────────────────────
//
// Same shape as src/recorder/VideoRecorderFFmpeg.cpp:42-87, deliberately not
// shared: the two callers need opposite halves of it. The recorder may
// legitimately find its output path already present, so it needs the lock to
// arbitrate two legitimate claimants. This queue refuses to touch a path that
// exists at all (O_EXCL) and uses the lock only to tell a crashed leftover
// apart from a live writer.
//
// The probe below is meaningful within one process and not just across
// programs: BSD flock() treats two descriptors for the same file as
// independent -- "an attempt to lock the file using one of these file
// descriptors may be denied by a lock that the calling process has already
// placed via another file descriptor".

#ifdef Q_OS_WIN

int nativeHandleOf(QFile& file) { return _get_osfhandle(static_cast<int>(file.nativeHandle())); }

bool lockExclusive(const int fd) {
    OVERLAPPED overlapped{};
    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
    return LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD,
                      MAXDWORD, &overlapped) != FALSE;
}

void unlockExclusive(const int fd) {
    OVERLAPPED overlapped{};
    LockFileEx(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)), LOCKFILE_FAIL_IMMEDIATELY, 0,
               MAXDWORD, MAXDWORD, &overlapped);
}

#else

int nativeHandleOf(QFile& file) { return file.handle(); }

bool lockExclusive(const int fd) { return flock(fd, LOCK_EX | LOCK_NB) == 0; }

void unlockExclusive(const int fd) { flock(fd, LOCK_UN); }

#endif

/// Try to take an exclusive lock on `path` without creating or truncating it.
/// True means nothing else holds it. A path that cannot be opened at all also
/// reports true: an unopenable leftover cannot be an active writer, and the
/// reclaim that follows surfaces the real reason.
bool claimIfUnlocked(const fs::path& path) {
    QFile probe(QString::fromStdString(path.string()));
    if (!probe.open(QIODevice::ReadOnly)) return true;

    const int fd = nativeHandleOf(probe);
    if (fd < 0) return true;

    const bool locked = lockExclusive(fd);
    if (locked) unlockExclusive(fd); // probe only; release immediately
    probe.close();
    return locked;
}

} // namespace

FailureKind classifyFailure(const QNetworkReply::NetworkError err, const int httpStatus) {
    using NE = QNetworkReply::NetworkError;
    if (err == NE::OperationCanceledError) return FailureKind::Cancelled;

    if (httpStatus >= 300 && httpStatus < 400) {
        return FailureKind::Permanent;
    }
    if (httpStatus >= 400) {
        if (httpStatus >= 500) return FailureKind::Retryable;
        return (httpStatus == 408 || httpStatus == 429) ? FailureKind::Retryable
                                                        : FailureKind::Permanent;
    }
    if (err == NE::NoError) return FailureKind::None;

    switch (err) {
        case NE::AuthenticationRequiredError:
        case NE::ContentAccessDenied:
        case NE::ContentOperationNotPermittedError:
        case NE::ProxyAuthenticationRequiredError:
            return FailureKind::Permanent;
        default:
            // Timeouts, connection drops, DNS hiccups: worth another shot.
            return FailureKind::Retryable;
    }
}

std::int64_t backoffWithJitterMs(const int attemptZeroBased, std::mt19937& rng) {
    const std::int64_t base = backoffBaseMs(attemptZeroBased);
    std::uniform_int_distribution<std::int64_t> jitter(-base / 5, base / 5);
    return base + jitter(rng);
}

namespace {

/// Month spellings for the IMF-fixdate token set. RFC 9110 fixes these nine
/// tokens; anything else is malformed, so this is an exact match, not a prefix.
constexpr std::array<const char*, 12> kHttpMonths{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/// Parse an HTTP-date (RFC 9110 5.6.7) into a UTC epoch.
///
/// Hand-rolled on purpose, because Qt's own parser cannot be used here and the
/// failure is silent. Measured against the Qt 6.11.1 on this machine:
/// `QDateTime::fromString(s, Qt::RFC2822Date)` returns an **invalid** QDateTime
/// for "Sun, 06 Nov 1994 08:49:37 GMT" -- the exact example in RFC 2822, and
/// the exact spelling IMF-fixdate (RFC 9110's only mandated HTTP-date form)
/// mandates -- and for every "GMT"/"UT"/"UTC" spelling, because RFC 2822's
/// timezone token is a *named* zone that Qt does not accept in that position.
///
/// A CORRECTION to the earlier version of this comment, which claimed Qt
/// "silently ignores" numeric offsets too. It does not. Measured on the same Qt:
///   "...08:49:37 +0000" -> 784111777  (exact)
///   "...08:49:37 +0100" -> 784108177  (exact)
///   "...08:49:37 -0500" -> 784129777  (exact)
/// The original evidence compared against
/// `QDateTime::fromString("2026-01-01T00:00:00", ISODate).toUTC()`, which is a
/// *local-time* parse -- it measured this machine's UTC offset, not a Qt bug.
/// Hand-parsing is still correct, but for exactly one reason: **named** zones are
/// unparseable. A numeric offset would have been fine.
///
/// The weekday is required to be present and is then discarded: RFC 9110 5.6.7
/// makes it redundant and requires recipients to accept a mismatched one.
std::optional<std::int64_t> parseHttpDate(const QByteArray& value,
                                          const std::int64_t nowEpochSecs) {
    const QString text = QString::fromLatin1(value);
    const qsizetype comma = text.indexOf(QLatin1Char(','));
    if (comma <= 0) return std::nullopt;

    const QStringList clock =
            text.mid(comma + 1).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // "06 Nov 1994 08:49:37 GMT" -> four clock fields plus the zone.
    if (clock.size() != 5) return std::nullopt;

    const int day = clock[0].toInt();
    const int month = static_cast<int>(std::find_if(kHttpMonths.begin(), kHttpMonths.end(),
                                                    [&](const char* m) {
                                                        return clock[1] == QLatin1String(m);
                                                    }) -
                                       kHttpMonths.begin()) +
                      1;
    const int year = clock[2].toInt();
    const QStringList hms = clock[3].split(QLatin1Char(':'));
    if (hms.size() != 3) return std::nullopt;
    const int hour = hms[0].toInt();
    const int minute = hms[1].toInt();
    const int second = hms[2].toInt();

    // Widths are lenient -- a hand-rolled server that emits "6 Nov" is common
    // enough to be worth accepting. Ranges are not, and the measured reason is
    // sharper than "an unchecked field yields a wrong time": QTime REJECTS an
    // out-of-range field, yet QDateTime{d, invalidQTime, UTC}.isValid() is TRUE
    // and its toSecsSinceEpoch() is MIDNIGHT of that day. Measured on Qt 6.11.1:
    //   QTime(8,49,60).isValid()                          -> false
    //   QDateTime(QDate(1994,11,6), QTime(8,49,60), UTC)
    //       .toSecsSinceEpoch()                           -> 784080000, midnight
    // So the !when.isValid() guard below cannot catch a bad field: QDateTime
    // claims valid and substitutes a plausible instant. `Retry-After:
    // Sun, 06 Nov 1994 08:49:60 GMT` therefore parsed as -31 717 seconds --
    // a leap-second hint read as "retry immediately", clamped up to the 1 s
    // floor. RFC 9110 does permit time-second 60, but Qt cannot represent it, so
    // reject it and let the caller fall back to the backoff ladder.
    if (day < 1 || day > 31 || month < 1 || month > 12 || hour > 23 || hour < 0 || minute > 59 ||
        minute < 0 || second > 59 || second < 0) {
        return std::nullopt;
    }

    // Zone: the four names RFC 9110 lists as accepted, or an explicit numeric
    // offset. An unrecognised zone is malformed rather than assumed to be UTC --
    // guessing here is the fail-open this class exists to avoid.
    std::int64_t zoneOffsetSecs = 0;
    const QString zone = clock[4];
    if (zone.compare("GMT", Qt::CaseInsensitive) == 0 ||
        zone.compare("UT", Qt::CaseInsensitive) == 0 ||
        zone.compare("UTC", Qt::CaseInsensitive) == 0 ||
        zone.compare("Z", Qt::CaseInsensitive) == 0) {
        zoneOffsetSecs = 0;
    } else if (zone.size() == 5 &&
               (zone.at(0) == QLatin1Char('+') || zone.at(0) == QLatin1Char('-'))) {
        bool ok = false;
        const int offset = zone.mid(1).toInt(&ok);
        if (!ok) return std::nullopt;
        zoneOffsetSecs = (offset / 100) * 3600 + (offset % 100) * 60;
        if (zone.at(0) == QLatin1Char('-')) zoneOffsetSecs = -zoneOffsetSecs;
    } else {
        return std::nullopt;
    }

    const QDateTime when{QDate(year, month, day), QTime(hour, minute, second), QTimeZone::utc()};
    if (!when.isValid()) return std::nullopt; // e.g. 40 Nov, or hour 99
    return std::optional<std::int64_t>{when.toSecsSinceEpoch() - zoneOffsetSecs - nowEpochSecs};
}

} // namespace

std::optional<std::int64_t> parseRetryAfter(const QByteArray& headerValue,
                                            const std::int64_t nowEpochSecs) {
    const QByteArray trimmed = headerValue.trimmed();
    if (trimmed.isEmpty()) return std::nullopt;

    // delta-seconds form. QByteArray::toLongLong consumes the *whole* string or
    // reports failure -- measured on this Qt: "5s" returns ok=false with 0, so
    // trailing garbage cannot be silently read as a valid number, and a
    // 23-digit overflow also returns ok=false rather than saturating. It does
    // accept a leading '-', and RFC 9110's delta-seconds is 1*DIGIT with no
    // sign, so a negative hint is treated as unparseable rather than clamped
    // into a plausible-looking zero.
    bool ok = false;
    const qlonglong seconds = trimmed.toLongLong(&ok);
    if (ok && seconds >= 0) return std::optional<std::int64_t>{static_cast<std::int64_t>(seconds)};

    return parseHttpDate(trimmed, nowEpochSecs);
}

std::int64_t retryDelayMs(const int attemptZeroBased,
                          const std::optional<std::int64_t> retryAfterSecs, std::mt19937& rng) {
    if (!retryAfterSecs.has_value()) return backoffWithJitterMs(attemptZeroBased, rng);

    const std::int64_t hint =
            std::clamp(*retryAfterSecs * 1000, kMinRetryAfterMs, kMaxRetryAfterMs);
    std::uniform_int_distribution<std::int64_t> jitter(0, hint / 5);
    return hint + jitter(rng);
}

std::string byteCapReason(const std::string_view clipId, const qint64 limitBytes,
                          const qint64 seenBytes) {
    return std::format("{} exceeded the per-item byte cap of {} bytes ({} received)", clipId,
                       limitBytes, seenBytes);
}

std::string shortWriteReason(const std::string_view clipId, const qint64 offsetBytes,
                             const qint64 wantedBytes, const qint64 writtenBytes,
                             const std::string_view deviceError) {
    return std::format("{} could not be written: {} of {} bytes at offset {} ({})", clipId,
                       writtenBytes, wantedBytes, offsetBytes, deviceError);
}

std::string shortDownloadReason(const std::string_view clipId, const qint64 receivedBytes,
                                const qint64 expectedBytes) {
    return std::format("{} downloaded {} of the {} bytes the server promised", clipId,
                       receivedBytes, expectedBytes);
}

DownloadQueue::DownloadQueue(QNetworkAccessManager* adoptedManager, QObject* parent)
    : QObject(parent) {
    if (adoptedManager) {
        nam_ = adoptedManager;
        nam_->setParent(this); // we own the one and only manager now
    }
}

DownloadQueue::DownloadQueue(ReplyFactory factory, QObject* parent)
    : QObject(parent), factory_(std::move(factory)) {}

DownloadQueue::DownloadQueue(ReplyFactory factory, PartOpener partOpener, QObject* parent)
    : QObject(parent), factory_(std::move(factory)), partOpener_(std::move(partOpener)) {}

DownloadQueue::~DownloadQueue() {
    // A QFile unlinks nothing when it is destroyed, so an item still in flight at
    // shutdown would leave its scratch file behind. Every non-success path
    // unlinks, which is what lets the reclaim in openPart() treat a survivor as
    // a crashed run -- so the queue has to honour that invariant on its own exit
    // too, or the first thing a crashed run finds is a queue that leaks.
    const auto release = [this](std::vector<std::shared_ptr<Item>>& container) {
        for (auto& item : container)
            dropPart(*item);
    };
    release(active_);
    release(waiting_);
}

void DownloadQueue::setMaxConcurrent(const int maxConcurrent) {
    maxConcurrent_ = std::max(1, maxConcurrent);
    pump();
}

void DownloadQueue::setTransferTimeoutMs(const int ms) { transferTimeoutMs_ = std::max(0, ms); }

void DownloadQueue::setMaxBytesPerItem(const qint64 bytes) { maxBytesPerItem_ = bytes; }

QNetworkReply* DownloadQueue::makeReply(const QNetworkRequest& request) {
    if (factory_) return factory_(request);
    if (!nam_) {
        nam_ = new QNetworkAccessManager(this);
    }
    return nam_->get(request);
}

std::filesystem::path DownloadQueue::partPathFor(const std::filesystem::path& dest) const {
    return dest.string() + kPartSuffix;
}

bool DownloadQueue::enqueue(std::string clipId, std::string url, std::filesystem::path destPath,
                            QVariantMap metadata) {
    if (clipId.empty() || url.empty() || destPath.empty()) {
        LOG_WARN("DownloadQueue: rejected job with empty id/url/dest");
        return false;
    }
    if (findItem(clipId)) {
        LOG_WARN("DownloadQueue: duplicate live job for clip {}", clipId);
        return false;
    }

    auto item = std::make_shared<Item>();
    item->clipId = std::move(clipId);
    item->url = std::move(url);
    item->destPath = std::move(destPath);
    item->metadata = std::move(metadata);

    std::error_code ec;
    if (item->destPath.has_parent_path()) {
        fs::create_directories(item->destPath.parent_path(), ec);
    }

    pending_.push_back(std::move(item));
    emit itemStateChanged(QString::fromStdString(pending_.back()->clipId),
                          static_cast<int>(DownloadState::Queued), 0);

    idleEmitted_ = false;
    pump();
    return true;
}

bool DownloadQueue::cancel(const std::string& clipId) {
    const auto matches = [&clipId](const auto& ptr) { return ptr->clipId == clipId; };
    const auto dequeueCancelled = [&](auto& container) {
        const auto it = std::find_if(container.begin(), container.end(), matches);
        if (it == container.end()) return false;
        dropPart(**it);
        setState(**it, DownloadState::Cancelled);
        container.erase(it);
        checkIdle();
        return true;
    };

    if (dequeueCancelled(pending_) || dequeueCancelled(waiting_)) return true;

    if (const auto it = std::find_if(active_.begin(), active_.end(), matches);
        it != active_.end()) {
        (*it)->cancelRequested = true;
        // finished() fires from abort(); classification handles the rest.
        if (auto* reply = (*it)->reply.data()) reply->abort();
        return true;
    }
    return false; // unknown or terminal id: graceful no-op
}

DownloadQueue::Item* DownloadQueue::findItem(const std::string& clipId) {
    const auto matches = [&clipId](const auto& ptr) { return ptr->clipId == clipId; };
    const auto scan = [&](auto& container) -> Item* {
        const auto it = std::find_if(container.begin(), container.end(), matches);
        return it != container.end() ? it->get() : nullptr;
    };
    if (Item* item = scan(active_)) return item;
    if (Item* item = scan(waiting_)) return item;
    return scan(pending_);
}

void DownloadQueue::pump() {
    while (!pending_.empty() && static_cast<int>(active_.size()) < maxConcurrent_) {
        // One local owner for the whole iteration. startItem() can retire the
        // item -- and retire() drops the containers' references -- so nothing
        // here may depend on a container still holding it.
        std::shared_ptr<Item> item = std::move(pending_.front());
        pending_.pop_front();
        startItem(item);
        if (!isTerminal(item->state)) {
            active_.push_back(std::move(item));
        }
    }
    checkIdle();
}

void DownloadQueue::startItem(const std::shared_ptr<Item>& item) {
    item->finishing = false;
    item->cancelRequested = false;
    item->bytesReceived = 0;
    item->expectedTotal = 0;
    item->ownsPart = false;
    item->abortCause = AbortCause::None;
    item->abortReason.clear();
    setState(*item, DownloadState::Downloading);

    auto part = openPart(*item);
    if (!part.has_value()) {
        // No reply exists yet, so there is nothing to abort and nothing to
        // classify: the claim itself is terminal.
        LOG_ERROR("DownloadQueue: {}", item->abortReason);
        setState(*item, DownloadState::FailedPermanent);
        retire(item->clipId);
        return;
    }
    item->partFile = std::move(*part);

    QNetworkRequest request{QUrl(QString::fromStdString(item->url))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    // Stall deadline, not a wall-clock budget: Qt restarts this timer on every
    // chunk, so a slow-but-progressing transfer is never cut off while a socket
    // that has gone quiet is. Requires Qt >= 6.7 -- see kDefaultTransferTimeoutMs.
    if (transferTimeoutMs_ > 0) request.setTransferTimeout(transferTimeoutMs_);
    item->progressPercent = -1;

    QNetworkReply* reply = makeReply(request);
    item->reply = reply;

    // Weak, not raw. A reply outlives its item whenever the item is retired
    // while the reply is still alive, and a transport really does emit one more
    // downloadProgress after abort() -- which is how this queue tears its own
    // attempts down. Capturing the address instead would be a use-after-free on
    // every terminal path; locking here is what makes that impossible, and an
    // expired pointer is also the honest answer for a callback about an item
    // that no longer exists.
    const std::weak_ptr<Item> weak = item;
    connect(reply, &QNetworkReply::readyRead, this, [this, weak]() {
        if (const auto held = weak.lock(); held) onData(*held);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, weak](qint64 rec, qint64 total) {
        if (const auto held = weak.lock(); held) onProgress(*held, rec, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, weak, reply]() {
        if (const auto held = weak.lock(); held) onFinished(*held, *reply);
    });
}

std::expected<std::unique_ptr<QIODevice>, PartOpenError> DownloadQueue::openPart(Item& item) {
    const fs::path path = partPathFor(item.destPath);
    const std::string pathStr = path.string();

    // An injected opener owns the whole claim, so this is the only way to
    // substitute a sink -- the production policy below cannot be bypassed by
    // accident. It is only ever supplied by the test constructor.
    if (partOpener_) {
        auto injected = partOpener_(pathStr);
        if (!injected) {
            item.abortReason =
                    std::format("{} could not open its scratch file at {}", item.clipId, pathStr);
            return std::unexpected(PartOpenError::Failed);
        }
        item.ownsPart = true;
        return std::expected<std::unique_ptr<QIODevice>, PartOpenError>{std::move(injected)};
    }

    // WriteOnly|NewOnly maps to O_CREAT|O_EXCL|O_WRONLY on POSIX and CREATE_NEW
    // on Win32: Qt's openModeToOpenFlags() adds QT_OPEN_CREAT for a writable
    // mode and QT_OPEN_EXCL for NewOnly, independently of Truncate
    // (qfsfileengine_p.h:230-248). That is the same claim
    // VideoRecorderFFmpeg.cpp:71-73 makes with a raw ::open, and it is exactly
    // what the old WriteOnly|Truncate lacked -- truncation follows a symlink,
    // and O_EXCL fails on a dangling one.
    std::string openError;
    const auto openOnce = [&pathStr, &openError]() -> std::unique_ptr<QFile> {
        auto file = std::make_unique<QFile>(QString::fromStdString(pathStr));
        if (file->open(QIODevice::WriteOnly | QIODevice::NewOnly)) return file;
        openError = file->errorString().toStdString();
        return nullptr;
    };
    const auto lockCreated = [](QFile& file) {
        const int fd = nativeHandleOf(file);
        return fd < 0 || lockExclusive(fd); // no native handle: nothing to lock
    };

    if (auto file = openOnce()) {
        if (!lockCreated(*file)) {
            // We hold the only exclusive claim on a path we just created, so a
            // refusal here means a holder arrived in the gap between create and
            // lock. Unlink what we made and report rather than share the file.
            LOG_WARN("DownloadQueue: lost the race for {} immediately after creating it", pathStr);
            std::error_code ec;
            fs::remove(path, ec);
            item.abortReason = std::format("{} lost the race to create its scratch file at {}",
                                           item.clipId, pathStr);
            return std::unexpected(PartOpenError::Failed);
        }
        item.ownsPart = true;
        return std::expected<std::unique_ptr<QIODevice>, PartOpenError>{std::move(file)};
    }

    // The open failed. "Already exists" is the only failure this queue can
    // reason about, and symlink_status does not follow links, so a dangling
    // symlink -- which nothing legitimate here ever creates -- reports as
    // symlink rather than not_found and is never mistaken for a crashed run.
    std::error_code ec;
    if (fs::symlink_status(path, ec).type() == fs::file_type::not_found) {
        LOG_WARN("DownloadQueue: cannot open part file for {} at {}: {}", item.clipId, pathStr,
                 openError);
        item.abortReason = std::format("{} could not open its scratch file at {} ({})", item.clipId,
                                       pathStr, openError);
        return std::unexpected(PartOpenError::Failed);
    }

    // A path exists. Unlocked, it can only be a crashed run: every non-success
    // path unlinks it, the retry path unlinks before re-queueing, and range
    // resume is deliberately disabled so there is nothing to resume from.
    // Refusing instead would wedge the item behind a file whose name is an
    // implementation detail -- a silently disabled feature with no way back.
    // So it is reclaimed. Locked, a live writer owns the path and removing it
    // would destroy another run's work: that is a collision, and it is reported.
    if (!claimIfUnlocked(path)) {
        LOG_WARN("DownloadQueue: {} collides with a live writer at {}", item.clipId, pathStr);
        item.abortReason =
                std::format("{} found another download already writing {}", item.clipId, pathStr);
        return std::unexpected(PartOpenError::Collision);
    }

    std::error_code removeEc;
    fs::remove(path, removeEc);
    if (removeEc) {
        LOG_WARN("DownloadQueue: cannot reclaim stale part for {} at {}: {}", item.clipId, pathStr,
                 removeEc.message());
        item.abortReason = std::format("{} found a stale scratch file at {} that could not be "
                                       "removed ({})",
                                       item.clipId, pathStr, removeEc.message());
        return std::unexpected(PartOpenError::Failed);
    }

    auto reclaimed = openOnce();
    if (!reclaimed || !lockCreated(*reclaimed)) {
        LOG_WARN("DownloadQueue: cannot reclaim stale part for {} at {}: {}", item.clipId, pathStr,
                 reclaimed ? std::string("could not lock after reclaiming") : openError);
        std::error_code cleanupEc;
        fs::remove(path, cleanupEc);
        item.abortReason = std::format("{} could not take over the stale scratch file at {}",
                                       item.clipId, pathStr);
        return std::unexpected(PartOpenError::Failed);
    }

    LOG_INFO("DownloadQueue: reclaimed stale part for {} at {}", item.clipId, pathStr);
    item.ownsPart = true;
    return std::expected<std::unique_ptr<QIODevice>, PartOpenError>{std::move(reclaimed)};
}

void DownloadQueue::onData(Item& item) {
    auto* reply = item.reply.data();
    if (!reply || item.finishing) return;
    // abort() can re-enter through the transport's own signal handling; a cause
    // already recorded means this attempt is being torn down, so nothing more
    // may be appended to a file that is about to be unlinked.
    if (item.abortCause != AbortCause::None) return;
    if (!item.partFile) return;

    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) return;

    const qint64 wanted = static_cast<qint64>(chunk.size());

    // Byte cap, checked before the write so the overflowing chunk never lands.
    // Strictly-greater is what makes "exactly at the limit" a success: an item
    // whose total equals the cap is legal and completes.
    if (maxBytesPerItem_ > 0 && item.bytesReceived + wanted > maxBytesPerItem_) {
        failItem(item, AbortCause::ByteCap,
                 byteCapReason(item.clipId, maxBytesPerItem_, item.bytesReceived + wanted));
        return;
    }

    const qint64 written = item.partFile->write(chunk);
    if (classifySinkWrite(written, wanted) != SinkWrite::Complete) {
        // ENOSPC lands here. Discarding this return value is how a truncated
        // file gets renamed into place and reported Completed -- and a
        // plausible-looking short MP3 that plays to the break is worse than a
        // visible failure, because no downstream consumer can tell it apart
        // from a real download. Retryable: the scratch file is unlinked and the
        // next attempt starts from byte zero, so a transient full disk recovers.
        failItem(item, AbortCause::ShortWrite,
                 shortWriteReason(item.clipId, item.bytesReceived, wanted, written,
                                  item.partFile->errorString().toStdString()));
        return;
    }
    item.bytesReceived += written;
}

void DownloadQueue::onProgress(Item& item, const qint64 received, const qint64 total) {
    // A transport is free to emit one last downloadProgress after abort() -- and
    // it does: the queue's own teardown path is triggered from inside readyRead,
    // so the progress signal that the aborted reply emits next lands *after* the
    // terminal state. Without this guard a failed item flips back to
    // "downloading" in the UI, which is the phantom-state bug the itemStateChanged
    // feed exists to avoid. `finishing` is exactly this flag; onData and
    // onFinished already honour it.
    if (item.finishing || item.abortCause != AbortCause::None) return;
    if (total <= 0) return;
    // The promised size, recorded for the short-body guard in finalizeSuccess.
    // Only a total the transport actually reported may be enforced.
    item.expectedTotal = total;
    const qint64 absolute = received;
    const qint64 grand = total;
    const int percent = static_cast<int>(std::clamp<qint64>(absolute * 100 / grand, 0, 100));
    if (percent != item.progressPercent) {
        item.progressPercent = percent;
        emit itemStateChanged(QString::fromStdString(item.clipId),
                              static_cast<int>(DownloadState::Downloading), percent);
    }
}

void DownloadQueue::onFinished(Item& item, QNetworkReply& reply) {
    if (item.finishing) return;

    // A self-inflicted cause outranks whatever the transport reports. abort()
    // sets OperationCanceledError, which classifies as Cancelled -- the exact
    // lie this ordering exists to prevent, since the user cancelled nothing.
    if (item.abortCause != AbortCause::None) {
        const bool retryable = item.abortCause == AbortCause::ShortWrite;
        LOG_WARN("DownloadQueue: {} tore its attempt down: {}", item.clipId, item.abortReason);
        handleFailure(item, &reply, retryable ? FailureKind::Retryable : FailureKind::Permanent);
        return;
    }

    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const FailureKind kind = classifyFailure(reply.error(), status);

    switch (kind) {
        case FailureKind::None:
            finalizeSuccess(item, reply);
            break;
        case FailureKind::Cancelled:
            finishCancelled(item, reply);
            break;
        case FailureKind::Retryable:
        case FailureKind::Permanent:
            handleFailure(item, &reply, kind);
            break;
    }
}

void DownloadQueue::failItem(Item& item, const AbortCause cause, std::string reason) {
    item.abortCause = cause;
    item.abortReason = std::move(reason);
    if (auto* reply = item.reply.data()) reply->abort();
}

void DownloadQueue::closeSink(Item& item) {
    if (!item.partFile) return;
    item.partFile->close();
    item.partFile.reset();
}

void DownloadQueue::dropPart(Item& item) {
    closeSink(item);
    if (!item.ownsPart) return;
    item.ownsPart = false;

    std::error_code ec;
    fs::remove(partPathFor(item.destPath), ec);
    if (ec) {
        LOG_WARN("DownloadQueue: could not remove part file for {}: {}", item.clipId, ec.message());
    }
}

void DownloadQueue::finalizeSuccess(Item& item, QNetworkReply& reply) {
    item.finishing = true;

    // Short-body guard, before any byte is promoted: a 2xx whose connection
    // closed early still arrives here with NoError, and without this check
    // the plausible-looking prefix was renamed into place and reported
    // Completed -- a file that plays to the break and that no downstream
    // consumer can tell apart from a real download. Only a total the
    // transfer actually reported can be enforced: no progress total and no
    // Content-Length means nothing to compare against, and failing there
    // would break every legitimate unknown-length transfer.
    qint64 expectedTotal = item.expectedTotal;
    if (expectedTotal <= 0) {
        const QVariant length = reply.header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid()) expectedTotal = length.toLongLong();
    }
    if (expectedTotal > 0 && item.bytesReceived < expectedTotal) {
        const std::string reason =
                shortDownloadReason(item.clipId, item.bytesReceived, expectedTotal);
        LOG_WARN("DownloadQueue: {}", reason);
        item.abortCause = AbortCause::ShortDownload;
        item.abortReason = reason;
        // A truncated transfer is a transient network condition: the same
        // retry treatment as any other transport failure, bounded by
        // kMaxAttempts, restarting from byte zero (range resume is
        // deliberately disabled).
        handleFailure(item, &reply, FailureKind::Retryable);
        return;
    }

    // Close, never unlink: the bytes live under the scratch name until the
    // rename below, and dropPart() here would delete them first -- which is
    // precisely how a Completed item ends up with no file at all.
    closeSink(item);
    reply.deleteLater();
    item.reply.clear();

    // A rename failure is unrecoverable for this attempt: the sink was closed
    // and there is no second copy of the bytes anywhere. Terminal, not retried.
    std::error_code ec;
    fs::rename(partPathFor(item.destPath), item.destPath, ec); // atomic on POSIX
    if (ec) {
        LOG_WARN("DownloadQueue: rename failed for {}: {}", item.clipId, ec.message());
        item.abortReason = std::format("{} downloaded but could not be moved into place ({})",
                                       item.clipId, ec.message());
        setState(item, DownloadState::FailedPermanent);
        retire(item.clipId);
        return;
    }

    item.progressPercent = 100;
    LOG_INFO("DownloadQueue: completed {} ({} bytes)", item.clipId, item.bytesReceived);
    setState(item, DownloadState::Completed);
    retire(item.clipId);
}

void DownloadQueue::finishCancelled(Item& item, QNetworkReply& reply) {
    item.finishing = true;
    dropPart(item);
    reply.deleteLater();
    item.reply.clear();

    LOG_INFO("DownloadQueue: cancelled {}", item.clipId);
    setState(item, DownloadState::Cancelled);
    retire(item.clipId);
}

void DownloadQueue::handleFailure(Item& item, QNetworkReply* reply, const FailureKind kind) {
    dropPart(item);
    item.finishing = true;
    ++item.attempts;

    if (kind == FailureKind::Retryable && item.attempts < kMaxAttempts) {
        // Read the hint before the reply is scheduled for deletion. Honouring
        // Retry-After is strictly better than the ladder: it is the server
        // telling us when it will be ready, and the ladder is a guess.
        std::optional<std::int64_t> retryAfter;
        if (reply) {
            retryAfter =
                    parseRetryAfter(reply->rawHeader("Retry-After"),
                                    std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        }
        // The attempt's reply is finished business, and this branch used to
        // return without releasing it: startItem's next assignment orphaned
        // up to kMaxAttempts-1 replies per item, each still holding its
        // connection and buffers. deleteLater is safe mid-signal -- the
        // object survives until control returns to the event loop, which is
        // what every other teardown path here already relies on.
        if (reply) {
            reply->deleteLater();
            item.reply.clear();
        }
        const std::int64_t delay = retryDelayMs(item.attempts - 1, retryAfter, rng_);
        LOG_WARN("DownloadQueue: {} failed (attempt {}/{}), retrying in {} ms{}", item.clipId,
                 item.attempts, kMaxAttempts, delay,
                 retryAfter.has_value() ? " (Retry-After)" : "");
        // The returned reference is the only owner between the erase and the
        // push_back, so `item` cannot die mid-statement.
        waiting_.push_back(takeFromActive(item.clipId));
        setState(item, DownloadState::Queued);
        QTimer::singleShot(delay, this, [this, id = item.clipId]() { resumeWaiting(id); });
        return;
    }

    if (reply) {
        reply->deleteLater();
        item.reply.clear();
    }
    setState(item, kind == FailureKind::Permanent ? DownloadState::FailedPermanent
                                                  : DownloadState::FailedRetryable);
    retire(item.clipId);
}

void DownloadQueue::resumeWaiting(const std::string& clipId) {
    const auto it = std::find_if(waiting_.begin(), waiting_.end(),
                                 [&clipId](const auto& ptr) { return ptr->clipId == clipId; });
    if (it == waiting_.end()) return; // cancelled during backoff
    auto item = std::move(*it);
    waiting_.erase(it);
    pending_.push_back(std::move(item)); // retries rejoin at FIFO front priority
    pump();
}

void DownloadQueue::setState(Item& item, const DownloadState state) {
    item.state = state;
    emit itemStateChanged(QString::fromStdString(item.clipId), static_cast<int>(state),
                          state == DownloadState::Completed ? 100 : item.progressPercent);
}

std::shared_ptr<DownloadQueue::Item> DownloadQueue::takeFromActive(const std::string& clipId) {
    const auto it = std::find_if(active_.begin(), active_.end(),
                                 [&clipId](const auto& ptr) { return ptr->clipId == clipId; });
    if (it == active_.end()) return nullptr;
    std::shared_ptr<Item> owned = std::move(*it);
    active_.erase(it);
    return owned;
}

void DownloadQueue::retire(const std::string& clipId) {
    const auto matches = [&clipId](const auto& ptr) { return ptr->clipId == clipId; };
    for (auto* container : {&active_, &waiting_}) {
        container->erase(std::remove_if(container->begin(), container->end(), matches),
                         container->end());
    }
    // A freed slot may let queued items start right away.
    pump();
}

void DownloadQueue::checkIdle() {
    const bool idleNow = isEmpty();
    if (idleNow && !idleEmitted_) {
        idleEmitted_ = true;
        emit queueIdle();
    } else if (!idleNow) {
        idleEmitted_ = false;
    }
}

} // namespace vc::suno