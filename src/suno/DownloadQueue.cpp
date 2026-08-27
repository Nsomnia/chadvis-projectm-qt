#include "suno/DownloadQueue.hpp"

#include "core/Logger.hpp"

#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <random>

namespace vc::suno {

FailureKind classifyFailure(const QNetworkReply::NetworkError err, const int httpStatus) {
    using NE = QNetworkReply::NetworkError;
    if (err == NE::NoError) return FailureKind::None;
    if (err == NE::OperationCanceledError) return FailureKind::Cancelled;

    if (httpStatus >= 400) {
        if (httpStatus >= 500) return FailureKind::Retryable;
        return (httpStatus == 408 || httpStatus == 429) ? FailureKind::Retryable
                                                        : FailureKind::Permanent;
    }

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

DownloadQueue::DownloadQueue(QNetworkAccessManager* adoptedManager, QObject* parent)
    : QObject(parent) {
    if (adoptedManager) {
        nam_ = adoptedManager;
        nam_->setParent(this);  // we own the one and only manager now
    }
}

DownloadQueue::DownloadQueue(ReplyFactory factory, QObject* parent)
    : QObject(parent), factory_(std::move(factory)) {}

DownloadQueue::~DownloadQueue() = default;

void DownloadQueue::setMaxConcurrent(const int maxConcurrent) {
    maxConcurrent_ = std::max(1, maxConcurrent);
    pump();
}

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

bool DownloadQueue::enqueue(std::string clipId,
                            std::string url,
                            std::filesystem::path destPath,
                            QVariantMap metadata) {
    if (clipId.empty() || url.empty() || destPath.empty()) {
        LOG_WARN("DownloadQueue: rejected job with empty id/url/dest");
        return false;
    }
    if (findItem(clipId)) {
        LOG_WARN("DownloadQueue: duplicate live job for clip {}", clipId);
        return false;
    }

    auto item = std::make_unique<Item>();
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
    return false;  // unknown or terminal id: graceful no-op
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
        auto item = std::move(pending_.front());
        pending_.pop_front();
        Item& ref = *item;
        startItem(ref);
        if (!isTerminal(ref.state)) {
            active_.push_back(std::move(item));
        }
    }
    checkIdle();
}

void DownloadQueue::startItem(Item& item) {
    item.finishing = false;
    item.cancelRequested = false;
    setState(item, DownloadState::Downloading);

    qint64 offset = 0;
    if (item.resumable) {
        std::error_code ec;
        offset = static_cast<qint64>(fs::file_size(partPathFor(item.destPath), ec));
        if (ec || offset <= 0) {
            offset = 0;
            item.resumable = false;  // lost partial data: clean restart
        }
    }

    QNetworkRequest request{QUrl(QString::fromStdString(item.url))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (offset > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + '-');
        LOG_INFO("DownloadQueue: resuming {} at {} bytes", item.clipId, offset);
    }

    item.rangeOffset = offset;
    item.awaitingStatusCheck = offset > 0;
    item.progressPercent = -1;

    item.partFile.setFileName(QString::fromStdString(partPathFor(item.destPath).string()));
    if (!item.partFile.open(offset > 0 ? QIODevice::Append
                                       : QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN("DownloadQueue: cannot open part file for {}: {}", item.clipId,
                 item.partFile.errorString().toStdString());
        setState(item, DownloadState::FailedPermanent);
        retire(item.clipId);
        return;
    }

    QNetworkReply* reply = makeReply(request);
    item.reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, &item]() { onData(item); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, &item](qint64 rec, qint64 total) { onProgress(item, rec, total); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, &item]() { onFinished(item, *reply); });
}

void DownloadQueue::onData(Item& item) {
    auto* reply = item.reply.data();
    if (!reply || item.finishing) return;

    if (item.awaitingStatusCheck) {
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        item.awaitingStatusCheck = false;
        if (status == 206) {
            // Proper Partial Content: keep appending.
        } else if (status == 200) {
            // Server ignored our Range and restarted from zero: mirror it.
            item.partFile.close();
            if (!item.partFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                LOG_WARN("DownloadQueue: cannot truncate part for {}: {}", item.clipId,
                         item.partFile.errorString().toStdString());
            }
            item.rangeOffset = 0;
        } else {
            // Misbehaving CDN on a ranged request: silent full restart.
            restartFromScratch(item, *reply);
            return;
        }
    }

    const QByteArray chunk = reply->readAll();
    if (!chunk.isEmpty()) item.partFile.write(chunk);
}

void DownloadQueue::onProgress(Item& item, const qint64 received, const qint64 total) {
    if (total <= 0) return;
    const qint64 absolute = item.rangeOffset + received;
    const qint64 grand = item.rangeOffset + total;
    const int percent = static_cast<int>(std::clamp<qint64>(absolute * 100 / grand, 0, 100));
    if (percent != item.progressPercent) {
        item.progressPercent = percent;
        emit itemStateChanged(QString::fromStdString(item.clipId),
                              static_cast<int>(DownloadState::Downloading), percent);
    }
}

void DownloadQueue::onFinished(Item& item, QNetworkReply& reply) {
    if (item.finishing) return;

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

void DownloadQueue::finalizeSuccess(Item& item, QNetworkReply& reply) {
    item.finishing = true;
    if (item.partFile.isOpen()) item.partFile.close();
    reply.deleteLater();
    item.reply.clear();

    std::error_code ec;
    fs::rename(partPathFor(item.destPath), item.destPath, ec);  // atomic on POSIX
    if (ec) {
        LOG_WARN("DownloadQueue: rename failed for {}: {}", item.clipId, ec.message());
        item.attempts = kMaxAttempts;  // nothing left to salvage
        setState(item, DownloadState::FailedPermanent);
        retire(item.clipId);
        return;
    }

    item.resumable = false;
    item.progressPercent = 100;
    LOG_INFO("DownloadQueue: completed {}", item.clipId);
    setState(item, DownloadState::Completed);
    retire(item.clipId);
}

void DownloadQueue::finishCancelled(Item& item, QNetworkReply& reply) {
    item.finishing = true;
    if (item.partFile.isOpen()) item.partFile.close();
    std::error_code ec;
    fs::remove(partPathFor(item.destPath), ec);
    reply.deleteLater();
    item.reply.clear();

    LOG_INFO("DownloadQueue: cancelled {}", item.clipId);
    setState(item, DownloadState::Cancelled);
    retire(item.clipId);
}

void DownloadQueue::handleFailure(Item& item, QNetworkReply* reply, const FailureKind kind) {
    if (item.partFile.isOpen()) item.partFile.close();

    // Learn resume capability only when the server advertised range support
    // AND usable partial data is actually on disk.
    std::error_code ec;
    const auto part = partPathFor(item.destPath);
    if (reply && reply->hasRawHeader("Accept-Ranges") && fs::exists(part, ec) &&
        fs::file_size(part, ec) > 0) {
        item.resumable = true;
    } else {
        item.resumable = false;
        fs::remove(part, ec);
    }

    item.finishing = true;
    if (reply) {
        reply->deleteLater();
        item.reply.clear();
    }
    ++item.attempts;

    if (kind == FailureKind::Retryable && item.attempts < kMaxAttempts) {
        LOG_WARN("DownloadQueue: {} failed (attempt {}/{}), backing off", item.clipId,
                 item.attempts, kMaxAttempts);
        // Moving containers never relocates the Item itself, so `item` stays
        // valid even after ownership hops over to waiting_.
        waiting_.push_back(takeFromActive(item.clipId));
        setState(item, DownloadState::Queued);
        QTimer::singleShot(backoffWithJitterMs(item.attempts - 1, rng_), this,
                           [this, id = item.clipId]() { resumeWaiting(id); });
        return;
    }

    setState(item, kind == FailureKind::Permanent ? DownloadState::FailedPermanent
                                                  : DownloadState::FailedRetryable);
    retire(item.clipId);
}

void DownloadQueue::restartFromScratch(Item& item, QNetworkReply& reply) {
    // Misbehaving CDN on a ranged request: silent full restart.
    item.finishing = true;  // swallow further callbacks from this reply
    if (item.partFile.isOpen()) item.partFile.close();
    std::error_code ec;
    fs::remove(partPathFor(item.destPath), ec);
    reply.deleteLater();
    item.reply.clear();
    handleFailure(item, nullptr, FailureKind::Retryable);
}

void DownloadQueue::resumeWaiting(const std::string& clipId) {
    const auto it = std::find_if(waiting_.begin(), waiting_.end(),
                                 [&clipId](const auto& ptr) { return ptr->clipId == clipId; });
    if (it == waiting_.end()) return;  // cancelled during backoff
    auto item = std::move(*it);
    waiting_.erase(it);
    pending_.push_back(std::move(item));  // retries rejoin at FIFO front priority
    pump();
}

void DownloadQueue::setState(Item& item, const DownloadState state) {
    item.state = state;
    emit itemStateChanged(QString::fromStdString(item.clipId),
                          static_cast<int>(state),
                          state == DownloadState::Completed ? 100 : item.progressPercent);
}

std::unique_ptr<DownloadQueue::Item> DownloadQueue::takeFromActive(const std::string& clipId) {
    const auto it = std::find_if(active_.begin(), active_.end(),
                                 [&clipId](const auto& ptr) { return ptr->clipId == clipId; });
    if (it == active_.end()) return nullptr;
    auto owned = std::move(*it);
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
