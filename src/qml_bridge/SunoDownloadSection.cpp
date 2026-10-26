#include "SunoDownloadSection.hpp"

#include <QFileInfo>

#include "core/Logger.hpp"
#include "suno/ClipResolver.hpp"
#include "suno/DownloadQueue.hpp"
#include "suno/SunoDownloader.hpp"
#include "ui/controllers/SunoController.hpp"

namespace qml_bridge {

SunoDownloadSection::SunoDownloadSection(QObject* parent) : QObject(parent) {}

SunoDownloadSection::~SunoDownloadSection() { detach(); }

void SunoDownloadSection::attach(vc::suno::SunoController* controller) {
    if (controller_ == controller && downloader_) {
        return;
    }
    detach();
    controller_ = controller;
    if (!controller_) {
        return;
    }
    // The controller owns the one downloader and therefore the one
    // DownloadQueue. Asked of the controller directly rather than found via
    // findChild: findChild resolves by object tree, which fails silently and
    // without a diagnostic if the parent ever changes, and it also matches any
    // other downloader that happened to be parented somewhere.
    downloader_ = controller_->downloader();
    if (downloader_) {
        connect(downloader_, &vc::suno::SunoDownloader::fileSaved, this,
                &SunoDownloadSection::clipSaved);
    }
    connect(controller_, &vc::suno::SunoController::downloadStateChanged, this,
            &SunoDownloadSection::onDownloadStateChanged);
}

void SunoDownloadSection::detach() {
    if (downloader_) {
        disconnect(downloader_, nullptr, this, nullptr);
        downloader_ = nullptr;
    }
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
        controller_ = nullptr;
    }
    // The latches describe a transfer that is no longer running. Leaving them
    // would let the next controller's first event be judged by the old
    // request's answer.
    playRequestClipId_.clear();
    activeClipId_.clear();
}

void SunoDownloadSection::setTitleProvider(std::function<QString(const QString&)> provider) {
    titleProvider_ = std::move(provider);
}

QString SunoDownloadSection::status() const { return status_; }

QString SunoDownloadSection::activeClipId() const { return activeClipId_; }

void SunoDownloadSection::playClip(const QString& clipId) {
    if (clipId.isEmpty() || !controller_) {
        emit errorMessageRequested(QStringLiteral("This clip has no playable media yet."));
        return;
    }

    LOG_INFO("SunoDownloadSection: play requested for clip {}", clipId.toStdString());
    emit errorMessageRequested({});
    activeClipId_ = clipId;
    playRequestClipId_ = clipId;
    const QString title = titleFor(clipId);
    emit statusMessageRequested(QStringLiteral("Preparing %1 for playback").arg(title));
    setStatusForClip(clipId, QStringLiteral("Preparing %1 for playback").arg(title));
    if (!controller_->playClipById(clipId.toStdString())) {
        // The controller logs the branch; this line ties the refusal to the
        // gesture the user made, so a terminal capture is enough to tell "the
        // click never arrived" from "the click arrived and play was refused".
        LOG_WARN("SunoDownloadSection: play request for {} was refused by the controller",
                 clipId.toStdString());
        playRequestClipId_.clear();
        activeClipId_.clear();
        emit errorMessageRequested(QStringLiteral("This clip has no playable media yet."));
    }
}

void SunoDownloadSection::downloadClip(const QString& clipId) { downloadInto({clipId}); }

void SunoDownloadSection::downloadClips(const QStringList& clipIds) { downloadInto(clipIds); }

void SunoDownloadSection::downloadInto(const QStringList& clipIds) {
    auto* downloader = ensureDownloader();
    if (!downloader || !controller_) {
        emit errorMessageRequested(
                QStringLiteral("Downloads are unavailable because no download service is attached."));
        return;
    }
    if (clipIds.isEmpty()) {
        return;
    }

    emit errorMessageRequested({});

    int accepted = 0;
    int rejected = 0;
    int unknown = 0;
    QString firstRejection;
    for (const QString& clipId : clipIds) {
        const auto clip = vc::suno::resolveClip(controller_->clips(), controller_->db(),
                                                clipId.toStdString());
        if (!clip) {
            ++unknown;
            emit clipSaveRefused(clipId,
                                 QStringLiteral("This clip is no longer in the library, so it "
                                                "cannot be saved."));
            continue;
        }
        auto result = downloader->download(*clip);
        if (!result) {
            ++rejected;
            if (firstRejection.isEmpty()) {
                firstRejection = result.error();
            }
            // Per-clip, carrying the id. Without it a batch refusal collapses
            // into one aggregate string and the UI cannot tell WHICH clips
            // failed — so every rejected card stays stuck on "Saving" while one
            // of them silently did nothing.
            emit clipSaveRefused(clipId, result.error());
            continue;
        }
        ++accepted;
    }

    if (accepted > 0) {
        emit statusMessageRequested(accepted == 1 ? QStringLiteral("Saving 1 clip to disk")
                                                  : QStringLiteral("Saving %1 clips to disk")
                                                            .arg(accepted));
    }

    // Report why a request was refused rather than leaving a dead button: the
    // clip has no capture-backed media, or it is not in the loaded library.
    QString message;
    if (rejected == 1) {
        message = firstRejection;
    } else if (rejected > 1) {
        message = QStringLiteral("%1 clips could not be saved. First reason: %2")
                          .arg(rejected)
                          .arg(firstRejection);
    }
    if (unknown > 0) {
        QString missing;
        if (unknown == 1) {
            missing = QStringLiteral("1 clip is not in the loaded library.");
        } else {
            missing = QStringLiteral("%1 clips are not in the loaded library.").arg(unknown);
        }
        message = message.isEmpty() ? missing : QStringLiteral("%1 %2").arg(missing, message);
    }
    if (!message.isEmpty()) {
        emit errorMessageRequested(message);
    }
}

vc::suno::SunoDownloader* SunoDownloadSection::ensureDownloader() {
    if (!controller_) {
        downloader_ = nullptr;
        return nullptr;
    }
    auto* found = controller_->downloader();
    if (found == downloader_) {
        return downloader_;
    }
    if (downloader_) {
        disconnect(downloader_, nullptr, this, nullptr);
    }
    downloader_ = found;
    if (downloader_) {
        connect(downloader_, &vc::suno::SunoDownloader::fileSaved, this,
                &SunoDownloadSection::clipSaved);
    }
    return downloader_;
}

void SunoDownloadSection::onDownloadStateChanged(const QString& clipId, const int state,
                                                 const int percent) {
    // Only an explicit play request suppresses other clips' events.
    // A save-only request leaves this empty, so a batch's results
    // are never filtered out by the play path's bookkeeping.
    if (!playRequestClipId_.isEmpty() && clipId != playRequestClipId_) {
        return;
    }
    const QString title = titleFor(clipId);
    switch (static_cast<vc::suno::DownloadState>(state)) {
        case vc::suno::DownloadState::Queued:
            setStatusForClip(clipId, QStringLiteral("Queued for download"));
            break;
        case vc::suno::DownloadState::Downloading:
            setStatusForClip(clipId, QStringLiteral("Downloading %1… %2%").arg(title).arg(qMax(0, percent)));
            break;
        case vc::suno::DownloadState::Completed: {
            // Save and play are separate requests that share one queue
            // event, so the wording has to come from the request rather
            // than be hardcoded: a silent save must never claim the
            // transport moved.
            const bool wasPlayRequest = playRequestClipId_ == clipId;
            setStatusForClip(clipId,
                             wasPlayRequest
                                     ? QStringLiteral("Downloaded %1; now playing").arg(title)
                                     : QStringLiteral("Saved %1 to your downloads").arg(title));
            if (wasPlayRequest) {
                playRequestClipId_.clear();
                activeClipId_.clear();
            }
            break;
        }
        case vc::suno::DownloadState::FailedRetryable:
        case vc::suno::DownloadState::FailedPermanent:
            setStatusForClip(clipId, QStringLiteral("Download failed for %1").arg(title));
            // T0168: the id-carrying channel. The status string above
            // still names the clip by title for the UI's single-line
            // summary, but nothing should have to parse it back out
            // to learn WHICH clip failed.
            emit clipDownloadSettled(clipId, state, QStringLiteral("Download failed for %1").arg(title));
            if (playRequestClipId_ == clipId) {
                playRequestClipId_.clear();
                activeClipId_.clear();
            }
            break;
        case vc::suno::DownloadState::Cancelled:
            setStatusForClip(clipId, QStringLiteral("Download cancelled for %1").arg(title));
            emit clipDownloadSettled(clipId, state,
                                     QStringLiteral("Download cancelled for %1").arg(title));
            if (playRequestClipId_ == clipId) {
                playRequestClipId_.clear();
                activeClipId_.clear();
            }
            break;
    }
}

void SunoDownloadSection::setStatusForClip(const QString& clipId, const QString& message) {
    if (activeClipId_.isEmpty()) {
        activeClipId_ = clipId;
    }
    if (status_ == message) {
        return;
    }
    status_ = message;
    emit statusChanged();
}

QString SunoDownloadSection::titleFor(const QString& clipId) const {
    if (!titleProvider_) {
        return QStringLiteral("clip");
    }
    const QString title = titleProvider_(clipId);
    return title.isEmpty() ? QStringLiteral("clip") : title;
}

} // namespace qml_bridge
