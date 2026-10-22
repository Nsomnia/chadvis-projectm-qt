#include "suno/SunoDownloader.hpp"

#include <QFile>
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/CapturedHosts.hpp"
#include "suno/ClipResolver.hpp"
#include "suno/MangoCrypto.hpp"
#include "suno/SunoLyrics.hpp"
#include "util/FileUtils.hpp"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QUrl>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <ranges>
#include <utility>

// TagLib Includes
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/fileref.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/textidentificationframe.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>

namespace vc::suno {

namespace {

/// The extension of the unencrypted form this client fetches. A WAV request
/// is rejected before a destination is ever chosen (see download()).
constexpr std::string_view kAudioExtension = ".mp3";

/// The extension a media form is written under, or nullopt when the form is
/// one this client will not fetch.
///
/// The names follow the CONTENT, not the wire type: an `m4a-opus` body is an
/// MP4 container with Opus in it, and writing it under ".mp3" would be a lie
/// the player would then have to forgive.
std::optional<std::string> extensionFor(const std::string& contentType, const bool encrypted) {
    if (encrypted) {
        if (contentType == "m4a-opus") {
            return std::string(".m4a");
        }
        if (contentType == "webm-opus") {
            return std::string(".webm");
        }
        // An encrypted form this client cannot name is a form it will not
        // fetch: the extension would be a guess, and the guess would end up
        // in a filename.
        return std::nullopt;
    }
    if (contentType == "mp3") {
        return std::string(".mp3");
    }
    return std::nullopt;
}

} // namespace

SunoDownloader::SunoDownloader(SunoClient* client,
                               SunoDatabase& db,
                               AudioEngine* audioEngine,
                               QNetworkAccessManager* networkManager,
                               QObject* parent)
    : QObject(parent),
      db_(db),
      audioEngine_(audioEngine),
      // The queue adopts the controller-provided manager; this is the one
      // QNetworkAccessManager for clip downloads (no ad-hoc managers here).
      queue_(std::make_unique<DownloadQueue>(networkManager, this)),
      client_(client) {
    // The content-rights call is how an encrypted body becomes audio. It
    // lives on the client because the client owns the bearer the rights
    // server wraps the key with.
    if (client_) {
        contentKeyFetcher_ = [client](const std::string& clipId,
                                      std::function<void(std::expected<ContentKey, QString>)> done) {
            client->requestContentKey(clipId, std::move(done));
        };
    }
    // T0200: the finalize wiring is NOT test-only. It used to live in the
    // ReplyFactory constructor, which meant the shipped downloader emitted
    // finalizeFinished into nothing: no tags, no sidecars, no fileSaved and no
    // auto-play for any downloaded clip.
    wireFinalize();
    wireQueue();
}

SunoDownloader::SunoDownloader(SunoDatabase& db,
                               AudioEngine* audioEngine,
                               ReplyFactory replyFactory)
    : QObject(nullptr),
      db_(db),
      audioEngine_(audioEngine),
      queue_(std::make_unique<DownloadQueue>(std::move(replyFactory), this)) {
    wireFinalize();
    wireQueue();
}

SunoDownloader::~SunoDownloader() {
    // T0037: stop the finalize thread BEFORE anything it touches goes away.
    //
    // Order matters and is not arbitrary: quit() then wait() drains nothing, so
    // a job already running still has to finish. wait() with no timeout is
    // deliberate -- returning while a job is mid-write would leave TagLib
    // writing a file this destructor is about to invalidate, which is the
    // use-after-free class T0032 was about, just reached the other way round.
    // A TagLib write is short and bounded; blocking shutdown on it is correct.
    //
    // What quit()+wait() does NOT guarantee: a job still QUEUED when quit()
    // takes effect never runs. On shutdown that is the right outcome -- the app
    // is going away and playing audio has nowhere to go -- but it means a clip
    // whose tagging was never started is silently left untagged, with no
    // fileSaved. That was already true of the queued downloads themselves, so
    // this is not a new class of loss, only a new place it can happen.
    finalizeThread_.quit();
    finalizeThread_.wait();
}

void SunoDownloader::wireFinalize() {
    // T0037: the finalize worker. Queued connection, so the handler below runs on
    // the GUI thread where addAndPlay and the QML-facing signals belong, and Qt
    // drops the delivery if `this` is gone by then.
    connect(this, &SunoDownloader::finalizeFinished, this,
            [this](const QString& clipId, const QString& destPath, bool play) {
                if (play) {
                    static_cast<void>(addAndPlay(fs::path(destPath.toStdString()),
                                                 clipId.toStdString()));
                }
                emit fileSaved(clipId, destPath);
            },
            Qt::QueuedConnection);
    connect(this, &SunoDownloader::finalizeFailed, this,
            [this](const QString& clipId, const QString& reason) {
                LOG_ERROR("SunoDownloader: {} could not be finished: {}",
                          clipId.toStdString(), reason.toStdString());
                emit downloadStateChanged(clipId,
                                          static_cast<int>(DownloadState::FailedPermanent), 0);
            },
            Qt::QueuedConnection);
    finalizeThread_.setObjectName(QStringLiteral("SunoDownloader::finalize"));
    finalizeThread_.start();
}

void SunoDownloader::runFinalize(std::function<void()> job) {
    // Captured by value so the closure owns everything it needs and cannot
    // reference a dead SunoDownloader (see the member comment).
    QMetaObject::invokeMethod(
            &finalizeThread_, [work = std::move(job)]() { work(); },
            Qt::QueuedConnection);
}

void SunoDownloader::wireQueue() {
    connect(queue_.get(), &DownloadQueue::itemStateChanged, this,
            [this](const QString& clipId, int state, int progressPercent) {
                emit downloadStateChanged(clipId, state, progressPercent);
                handleItemState(clipId.toStdString(),
                                static_cast<DownloadState>(state));
            });
    connect(queue_.get(), &DownloadQueue::queueIdle,
            this, &SunoDownloader::downloadQueueIdle);
}

fs::path SunoDownloader::getDownloadDir() const {
  fs::path dir = CONFIG.suno().downloadPath;
  if (dir.empty()) {
    QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
    dir = fs::path(musicLoc.toStdString());
  }
  if (auto result = vc::file::ensureDir(dir); !result) {
    LOG_WARN("SunoDownloader: Failed to create download directory: {}",
             result.error().message);
  }
  return dir;
}

std::string SunoDownloader::safeStem(std::string_view title, const std::string& clipId) {
  std::string safe = vc::file::sanitizeFilename(std::string(title));
  return safe.empty() ? clipId : safe;
}

namespace {

bool isUsableCapturedUrl(const std::string& value) {
    if (value.empty()) return false;

    const QString text = QString::fromStdString(value);
    if (text.contains(QStringLiteral("/api/forbidden"), Qt::CaseInsensitive)) {
        return false;
    }

    const QUrl url(text);
    // T0033: the scheme/port/userinfo/path half was a hand-rolled copy of the
    // outbound-host rules (CapturedHosts.hpp:6). That half is now the registry's,
    // via mayBeFetched -- a media download carries no bearer.
    //
    // The host test STAYS, and it is not redundant: the registry admits every
    // captured host, including artwork, Clerk and the studio API. This function
    // asks the narrower "is this the audio delivery host". Host policy and
    // endpoint identity are different questions.
    //
    // So is the path sentinel: `api/forbidden` is a server route, not a host
    // fact, and belongs to the endpoint rather than to the registry.
    //
    // The audio delivery hosts come from the registry too: the captured
    // streaming origin and the captured progressive CloudFront distribution.
    // This used to be a literal for the streaming origin alone, which is why
    // the encrypted progressive form -- the form every synced clip carries --
    // was refused before it was ever fetched. The registry owns the list, so
    // a new captured media host is a table row rather than a literal here.
    bool mediaHost = false;
    for (const auto role : {hosts::Role::MediaOrigin, hosts::Role::CloudFront}) {
        for (const auto& rule : hosts::hostsForRole(role)) {
            if (url.host().compare(QString::fromUtf8(rule.host.data(), static_cast<qsizetype>(rule.host.size())),
                                   Qt::CaseInsensitive) == 0) {
                mediaHost = true;
                break;
            }
        }
        if (mediaHost) {
            break;
        }
    }
    if (!mediaHost) {
        return false;
    }
    // A fragment is refused, and stays refused here rather than being folded into
    // the registry: it is not sent to the server, so it is not a host fact, and
    // `test_ClipParser.cpp:220` pins the refusal. mayBeFetched deliberately does
    // not look at it -- a fragment-less host is still a reachable host, and two
    // call sites wanting different fragment policies is exactly what the
    // registry should not be deciding.
    if (!url.fragment().isEmpty()) {
        return false;
    }
    if (!hosts::mayBeFetched(url, hosts::Tier::Production)) {
        return false;
    }

    return !url.path().contains(QStringLiteral("api/forbidden"),
                                Qt::CaseInsensitive);
}

/// The server's own answer to "may this account save this clip", consulted
/// BEFORE any host or format reasoning.
///
/// This is the first gate on purpose. It was previously never read at all, so
/// every refusal this function produced was attributed to a `content_type`
/// filter — meaning a clip the server had already declined was reported to the
/// user as "no playable audio on the captured media host". That is the wrong
/// diagnosis of a real fault, and the same class of mistake as reporting a
/// library-sync failure for an unrelated subsystem error.
///
/// Only an explicit `false` refuses. An **absent** field deliberately does not,
/// which is the opposite of the reflexive "unknown is not permission" rule and
/// needs its justification, because it is a real judgement call:
///
///  * The same payload carries `audio_url` set to the `/api/forbidden` sentinel.
///    The server therefore *does* express denial explicitly when it means to —
///    absence of the flag is absence of a denial, not a denial.
///  * A refusal is issued *alongside* a usable `media_urls` entry, so the field
///    is not what withholds access to the bytes; the host and content-type
///    filters already do that, and they are unchanged by this gate.
///  * Requiring an explicit `true` would refuse every clip persisted before
///    this field was parsed — including a signed-in user's existing library.
///    That is a regression in exchange for no additional safety.
///
/// Every observed refusal (36 of 40 captured clips carry
/// `is_download_unlocked: false`) is still honoured.
///
/// Deliberately NOT said: that the cause is a download quota. A metered
/// server-side allowance appears in third-party material and is `[LEAD]`-grade,
/// so it is not asserted to a user as the explanation.
std::optional<QString> downloadEntitlementRefusal(const SunoClip& clip) {
    if (clip.is_download_unlocked.value_or(true)) {
        return std::nullopt;
    }
    if (!clip.download_disabled_reason.empty()) {
        return QStringLiteral("Suno does not allow this clip to be downloaded (%1).")
            .arg(QString::fromStdString(clip.download_disabled_reason));
    }
    return QStringLiteral(
        "Suno does not allow this clip to be downloaded. This can be a monthly "
        "download allowance rather than anything to do with this clip.");
}

}

bool SunoDownloader::isSupportedMediaUrl(const QString& url)
{
    return isUsableCapturedUrl(url.toStdString());
}

std::optional<MediaSelection>
SunoDownloader::selectMedia(const SunoClip& clip, const vc::SunoDownloadFormat format) {
    if (format != vc::SunoDownloadFormat::MP3 || clip.status != "complete") {
        return std::nullopt;
    }

    // The encrypted form first, which is the order the site's own player
    // uses: the captured progressive entry is the one every synced clip
    // carries, and the plain mp3 form is the older capture's.
    std::optional<MediaSelection> plain;
    for (const auto& media : clip.media_urls) {
        if (!isUsableCapturedUrl(media.url)) {
            continue;
        }
        // The presence of `encoding` is what marks a media entry as the
        // encrypted variant -- the site's own client makes the same test.
        const bool encrypted = !media.encoding.empty();
        const auto extension = extensionFor(media.content_type, encrypted);
        if (!extension) {
            continue;
        }
        if (encrypted) {
            return MediaSelection{media.url, *extension, true};
        }
        if (!plain) {
            plain = MediaSelection{media.url, *extension, false};
        }
    }

    return plain;
}

std::optional<std::string>
SunoDownloader::selectDownloadUrl(const SunoClip& clip,
                                  const vc::SunoDownloadFormat format) {
    const auto media = selectMedia(clip, format);
    if (!media) {
        return std::nullopt;
    }
    return media->url;
}

// Deliberately says nothing about which media types exist or might be usable:
// it only restates the fail-closed decision selectDownloadUrl() already made, so
// that a dead button is at least an explained one. It also no longer claims
// "the captured media host" — there is more than one, and this client accepts
// only one of them, so that phrasing named a thing the user has no way to know
// about. The mp3-only predicate is an internal filter, not a user-facing
// concept, and is logged rather than shown.
//
// Reached only after the download-entitlement gate has already passed, so the
// remaining causes are genuinely local: the clip is unfinished, or its media
// entries are not ones this client will fetch.
QString SunoDownloader::noUsableMediaMessage(const SunoClip& clip) {
    if (clip.status != "complete") {
        return QStringLiteral("This clip is not finished (status: %1), so there is no audio to "
                              "save yet.")
            .arg(clip.status.empty() ? QStringLiteral("unknown")
                                     : QString::fromStdString(clip.status));
    }
    return QStringLiteral(
        "This clip's audio is served in a format or from a host this client "
        "will not fetch, so there is nothing to save.");
}

std::expected<DownloadStarted, QString> SunoDownloader::download(const SunoClip& clip,
                                                                const DownloadAction action) {
    if (clip.id.empty()) {
        return std::unexpected(
            QStringLiteral("This clip has no identifier, so it cannot be saved."));
    }

    const auto format = CONFIG.suno().downloadFormat;
    if (format != vc::SunoDownloadFormat::MP3) {
        LOG_WARN("SunoDownloader: WAV download rejected for clip {}: conversion route is LEAD-only",
                 clip.id);
        return std::unexpected(
            QStringLiteral("WAV download is unavailable: the conversion route is not "
                           "capture-backed."));
    }

    // Entitlement first, and before any host/format work: it is the server's own
    // decision, it is captured, and checking it last would misattribute a
    // server refusal to a local filter.
    if (const auto refusal = downloadEntitlementRefusal(clip)) {
        LOG_WARN("SunoDownloader: clip {} refused by server download entitlement: {}",
                 clip.id, refusal->toStdString());
        return std::unexpected(*refusal);
    }

    const auto media = selectMedia(clip, format);
    if (!media) {
        LOG_WARN("SunoDownloader: no usable captured media URL for clip {} (status {})",
                 clip.id, clip.status);
        return std::unexpected(noUsableMediaMessage(clip));
    }

    const fs::path targetPath = resolveDestPath(clip, media->extension);
    if (fs::exists(targetPath)) {
        return finishAlreadyOnDisk(clip, targetPath, action);
    }

    enqueueAudio(clip, *media, action);
    return DownloadStarted{clip.id, action, false};
}

void SunoDownloader::downloadAndPlay(const SunoClip& clip) {
    static_cast<void>(download(clip, DownloadAction::SaveAndPlay));
}

bool SunoDownloader::cancelDownload(const std::string& clipId) {
    if (clipId.empty()) {
        return false;
    }
    // The queue drops the .part file and emits the terminal Cancelled state,
    // which is what clears pendingClips_; nothing is written to disk and no
    // saved-file notification is emitted.
    return queue_->cancel(clipId);
}

std::expected<DownloadStarted, QString>
SunoDownloader::finishAlreadyOnDisk(const SunoClip& clip,
                                    const fs::path& path,
                                    const DownloadAction action) {
    if (action == DownloadAction::SaveAndPlay) {
        // Pre-existing shortcut, preserved deliberately: a clip already in the
        // download directory goes straight to the transport with no transfer,
        // and the synthetic Completed state is emitted only when playback
        // actually started.
        if (addAndPlay(path, clip.id)) {
            emit downloadStateChanged(QString::fromStdString(clip.id),
                                      static_cast<int>(DownloadState::Completed), 100);
        } else {
            LOG_WARN("SunoDownloader: {} is on disk but could not be enqueued for playback",
                     clip.id);
        }
        return DownloadStarted{clip.id, action, true};
    }

    // SaveOnly: the file is already where it belongs, so announce it and
    // leave the playlist and the transport exactly as they were.
    emit fileSaved(QString::fromStdString(clip.id), QString::fromStdString(path.string()));
    emit downloadStateChanged(QString::fromStdString(clip.id),
                              static_cast<int>(DownloadState::Completed), 100);
    return DownloadStarted{clip.id, action, true};
}

fs::path SunoDownloader::resolveDestPath(const SunoClip& clip, const std::string& extension) {
    if (const auto known = destByClip_.find(clip.id); known != destByClip_.end()) {
        return known->second;
    }

    const fs::path dir = getDownloadDir();
    const std::string stem = safeStem(clip.title, clip.id);
    fs::path candidate = dir / (stem + extension);

    // Two clips that share a title must not share a file, or a batch silently
    // overwrites the first with the second. The plain name counts as taken
    // when any other clip has already been handed it -- whether or not its
    // bytes have landed -- and the unique clip id breaks the tie.
    //
    // Deliberately NOT keyed on fs::exists(): a file left by an earlier
    // session must still be reused, because that is the "already downloaded"
    // shortcut downloadAndPlay depends on, and probing the filesystem here
    // would make the answer depend on whether this clip's own bytes have
    // arrived yet -- so the transfer and the sidecar writers, which run on
    // either side of that moment, could disagree about the path.
    //
    // The key is lowercased, because APFS and NTFS compare paths
    // case-insensitively and this map does not: `Song.mp3` and `song.mp3` are
    // the SAME file on both, but two different keys here, so the second clip
    // would be handed a path the first one already owns and the first would be
    // silently clobbered. The filesystem owns that comparison, not the
    // sanitizer, which is why it is fixed here and not in sanitizeFilename.
    const auto claim = [](const fs::path& path) {
        std::string key = path.string();
        std::ranges::transform(key, key.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    };

    // T0169: the stale-file half of the tie-break. `destOwner_` only knows about
    // clips THIS process has handed paths to, so a plain-named file left by an
    // EARLIER session is invisible to it -- and a different clip whose title
    // matches then gets handed the same path and silently overwrites it, which
    // is data loss with no error and no user visible anywhere.
    //
    // Reading the embedded SUNO_ID closes it without changing the filename: the
    // "already downloaded" shortcut that downloadAndPlay depends on still works
    // for the clip that really owns the file, and a foreign file no longer
    // counts as taken-by-us.
    //
    // Deliberately NOT keyed on fs::exists() for THIS clip's claim, because
    // probing the filesystem for whether our own bytes have landed would make
    // the answer depend on transfer timing: the transfer and the sidecar
    // writers run on either side of that moment and would then disagree about
    // the path. Only a FOREIGN identification is allowed to move candidate;
    // ours never is.
    if (!destOwner_.contains(claim(candidate))) {
        if (const auto embedded = embeddedSunoId(candidate); embedded && *embedded != clip.id) {
            // Someone else's file, and it is not ours to overwrite. Fall through
            // to the id-suffixed name below rather than reusing it.
            destOwner_.emplace(claim(candidate), *embedded);
            candidate = dir / (stem + "-" + vc::file::sanitizeFilename(clip.id) + extension);
        } else if (embedded) {
            // Our own file from a previous session: reuse the plain name, which
            // is the shortcut's entire job.
            //
            // Deliberately does NOT claim `destOwner_` here. That map answers
            // "is this name taken by another clip", and recording ourselves
            // would make the disambiguation block two lines below see the name
            // as taken and rename it -- one inbound clip silently getting a
            // suffixed file and losing the reuse it was entitled to.
            candidate = dir / (stem + extension);
        }
    }
    if (destOwner_.contains(claim(candidate))) {
        // clip.id is sanitized because it is interpolated into a path here, and
        // ClipParser::parseClip validates only that it is non-empty -- so an id
        // containing "../" would otherwise write outside the download directory.
        // Remote data reaching a path unsanitized is the exact bar at which this
        // matters; a hostile upstream is a real possibility for a scraped id.
        candidate = dir / (stem + "-" + vc::file::sanitizeFilename(clip.id) + extension);
    }
    destOwner_.emplace(claim(candidate), clip.id);
    destByClip_.emplace(clip.id, candidate);
    return candidate;
}

fs::path SunoDownloader::defaultDestPathFor(const std::string& clipId) const {
    if (const auto known = destByClip_.find(clipId); known != destByClip_.end()) {
        return known->second;
    }
    // Claims nothing: a lyrics fetch can arrive for a clip this downloader
    // never saved, and it must not steal a destination from a download that
    // has not happened yet.
    const auto clip = resolveClip({}, db_, clipId);
    // The same extension the transfer would use, so the sidecar a lyrics
    // fetch writes lands beside the file it belongs to.
    std::string extension(kAudioExtension);
    if (clip) {
        if (const auto media = selectMedia(*clip, CONFIG.suno().downloadFormat)) {
            extension = media->extension;
        }
    }
    return getDownloadDir() / (safeStem(clip ? clip->title : std::string{}, clipId) + extension);
}

/// Route one transfer through the shared DownloadQueue; tagging/sidecars run
/// from the completion hook in handleItemState().
void SunoDownloader::enqueueAudio(const SunoClip& clip,
                                  const MediaSelection& media,
                                  const DownloadAction action) {
    if (const auto live = pendingClips_.find(clip.id); live != pendingClips_.end()) {
        // A live job already owns this clip id and DownloadQueue rejects a
        // second one for it. The pre-fix branch overwrote the live request's
        // completion record and then erased it, so the in-flight transfer
        // finished with no tag, no sidecar and no announcement. Escalate the
        // live request instead: SaveAndPlay wins, because escalating a save
        // into a play is the only direction a later request may move it.
        if (action == DownloadAction::SaveAndPlay) {
            live->second.action = DownloadAction::SaveAndPlay;
        }
        return;
    }

    const fs::path targetPath = resolveDestPath(clip, media.extension);
    pendingClips_.emplace(clip.id, PendingDownload{clip, targetPath, action, media.encrypted});
    if (!queue_->enqueue(clip.id, media.url, targetPath)) {
        pendingClips_.erase(clip.id);  // rejected for a reason only the queue knows
    }
}

void SunoDownloader::handleItemState(const std::string& clipId, DownloadState state) {
    if (!isTerminal(state)) return;

    const auto it = pendingClips_.find(clipId);
    if (it == pendingClips_.end()) return;
    const PendingDownload pending = it->second;
    pendingClips_.erase(it);

    switch (state) {
        case DownloadState::Completed: {
            // T0037: the tagging and both sidecar writes moved off the GUI
            // thread. They are blocking file I/O -- TagLib rewrites the file's
            // ID3 container -- and this runs once per completed download, so a
            // batch froze the UI once per clip for the length of a tag write.
            //
            // `pending` is copied into the closure, so the worker never reads
            // pendingClips_, which the next completion may already be mutating.
            //
            // The ordering guarantee the original comment described is
            // preserved and is the reason addAndPlay is in the completion half:
            // the file, its tags and its sidecars are all in place before the
            // path reaches the audio engine, so a synchronous lyrics fetch
            // triggered from addAndPlay() finds a complete download directory.
            const bool play = pending.action == DownloadAction::SaveAndPlay;
            const QString clipIdQt = QString::fromStdString(clipId);
            const fs::path destPath = pending.destPath;

            // An encrypted body is not audio yet: the bytes on disk are the
            // server's ciphertext, and only the content key turns them into
            // a container a player can open. The key comes from the server,
            // so this cannot be finished here -- it continues when the key
            // arrives.
            if (pending.encrypted) {
                fetchContentKey(clipId, pending, play);
                break;
            }
            // The worker closure captures `this`, which is safe HERE ONLY because the
            // destructor body quits and joins finalizeThread_ before any member
            // is destroyed: a job is therefore either fully executed while the
            // object is still whole, or discarded when the thread's event loop
            // stops. Do not move this closure anywhere that outlives that join --
            // a queued lambda running on a destroyed object is the T0032 bug, and
            // a QPointer guard would not fix it, since reading the QPointer from
            // the worker races the destruction it is meant to observe.
            //
            // tagAudioFile and saveMetadataSidecar are static and take the
            // resolved path precisely so this closure touches no mutable member:
            // destByClip_/destOwner_ are GUI-thread state and reading them here
            // would be a second, quieter race.
            runFinalize([this, pending, clipIdQt, destPath, play]() {
                tagAudioFile(pending.destPath, pending.clip);
                saveMetadataSidecar(destPath, pending.clip);
                emit finalizeFinished(clipIdQt, QString::fromStdString(destPath.string()), play);
            });
            break;
        }
        case DownloadState::FailedPermanent:
            LOG_ERROR("SunoDownloader: permanent failure for clip {}", clipId);
            break;
        case DownloadState::FailedRetryable:
            LOG_WARN("SunoDownloader: retries exhausted for clip {}", clipId);
            break;
        default:
            break;  // Cancelled needs no post-processing
    }
}

namespace {

/// Decrypts an encrypted media body in place.
///
/// The body is one AES-128-CTR stream, so it is decrypted a chunk at a time
/// and written to a sibling file; the original is replaced only once the
/// decrypted file is complete. A crash therefore leaves either the encrypted
/// body or the decrypted one, never half of each.
bool decryptMediaBody(const fs::path& path, const ContentKey& key) {
    std::error_code ec;
    std::unique_ptr<CtrStream> stream;
    {
        CtrStream candidate(key.key, key.counter);
        if (!candidate.valid()) {
            LOG_ERROR("SunoDownloader: the content key is not usable for {}", path.string());
            return false;
        }
        stream = std::make_unique<CtrStream>(std::move(candidate));
    }

    QFile source(QString::fromStdString(path.string()));
    if (!source.open(QIODevice::ReadOnly)) {
        LOG_ERROR("SunoDownloader: cannot read {} to decrypt it", path.string());
        return false;
    }

    const fs::path target = path.string() + ".decrypting";
    {
        QFile output(QString::fromStdString(target.string()));
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            LOG_ERROR("SunoDownloader: cannot write {}", target.string());
            return false;
        }
        constexpr qint64 kChunk = 64 * 1024;
        QByteArray head;
        while (true) {
            const QByteArray chunk = source.read(kChunk);
            if (chunk.isEmpty()) {
                break;
            }
            const QByteArray clear = stream->process(chunk);
            if (head.isEmpty()) {
                head = clear.left(12);
            }
            if (!clear.isEmpty() && output.write(clear) != clear.size()) {
                LOG_ERROR("SunoDownloader: writing {} failed", target.string());
                output.close();
                std::filesystem::remove(target, ec);
                return false;
            }
        }
        // The container check. A wrong key or a corrupted body decrypts to
        // bytes that are not a container, and writing them under an .m4a name
        // would leave the user with a file no player can open and no reason
        // why. The two magics are the ones the media forms use: an ISO base
        // media file ("ftyp" at offset 4) and an EBML/WebM header.
        const bool mp4 = head.size() >= 8 && head.mid(4, 4) == QByteArrayLiteral("ftyp");
        const bool webm = head.size() >= 4 && head.left(4) == QByteArrayLiteral("\x1a\x45\xdf\xa3");
        if (!mp4 && !webm) {
            LOG_ERROR("SunoDownloader: the decrypted body of {} is not an audio container",
                      path.string());
            output.close();
            std::filesystem::remove(target, ec);
            return false;
        }
        const QByteArray tail = stream->finish();
        if (!tail.isEmpty() && output.write(tail) != tail.size()) {
            LOG_ERROR("SunoDownloader: writing {} failed", target.string());
            output.close();
            std::filesystem::remove(target, ec);
            return false;
        }
    }

    // Swap the decrypted file in. The target is this downloader's own file and
    // the replacement is already complete, so removing it first cannot lose
    // anything: the worst case is that the body stays encrypted.
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(target, path, ec);
    if (ec) {
        LOG_ERROR("SunoDownloader: cannot replace {} with the decrypted body: {}",
                  path.string(), ec.message());
        std::filesystem::remove(target, ec);
        return false;
    }
    return true;
}

} // namespace

void SunoDownloader::fetchContentKey(const std::string& clipId,
                                    const PendingDownload& pending,
                                    const bool play) {
    if (!contentKeyFetcher_) {
        LOG_ERROR("SunoDownloader: clip {} has an encrypted body and this build has no way to "
                  "ask for its key",
                  clipId);
        emit finalizeFailed(QString::fromStdString(clipId),
                            QStringLiteral("This build cannot unlock encrypted playback."));
        return;
    }

    contentKeyFetcher_(clipId, [this, pending, play](std::expected<ContentKey, QString> key) {
        if (!key) {
            LOG_ERROR("SunoDownloader: no content key for clip {}: {}",
                      pending.clip.id, key.error().toStdString());
            emit finalizeFailed(QString::fromStdString(pending.clip.id), key.error());
            return;
        }
        runFinalize([this, pending, play, key = *key]() {
            if (!decryptMediaBody(pending.destPath, key)) {
                emit finalizeFailed(QString::fromStdString(pending.clip.id),
                                    QStringLiteral("The downloaded audio could not be decrypted."));
                return;
            }
            tagAudioFile(pending.destPath, pending.clip);
            saveMetadataSidecar(pending.destPath, pending.clip);
            emit finalizeFinished(QString::fromStdString(pending.clip.id),
                                  QString::fromStdString(pending.destPath.string()), play);
        });
    });
}

void SunoDownloader::tagAudioFile(const fs::path& path, const SunoClip& clip) {
    LOG_INFO("SunoDownloader: Tagging file {}", path.string());

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".mp3") {
        TagLib::MPEG::File f(path.c_str());
        // TagLib hands back a null tag when the file will not open, and a
        // download that renamed into place on an HTTP 200 is not guaranteed
        // to be a readable MPEG stream. Every write below would be through
        // that null pointer.
        if (!f.isValid()) {
            LOG_WARN("SunoDownloader: {} is not a readable MPEG file; left untagged",
                     path.string());
            return;
        }
        TagLib::ID3v2::Tag* tag = f.ID3v2Tag(true);
        if (!tag) {
            LOG_WARN("SunoDownloader: no ID3v2 tag available for {}", path.string());
            return;
        }

        tag->setTitle(TagLib::String(clip.title, TagLib::String::UTF8));
        tag->setArtist(TagLib::String(clip.display_name, TagLib::String::UTF8));
        tag->setAlbum(TagLib::String("Suno AI Generations", TagLib::String::UTF8));
        tag->setComment(TagLib::String(clip.id, TagLib::String::UTF8));

        // Unsynced Lyrics
        if (!clip.metadata.lyrics.empty()) {
            tag->removeFrames("USLT");
            auto* frame = new TagLib::ID3v2::UnsynchronizedLyricsFrame();
            frame->setText(TagLib::String(clip.metadata.lyrics, TagLib::String::UTF8));
            frame->setLanguage("eng");
            tag->addFrame(frame);
        }

        // Custom Suno Metadata
        auto addTxxx = [&](const std::string& desc, const std::string& val) {
            auto* frame = new TagLib::ID3v2::UserTextIdentificationFrame();
            frame->setDescription(TagLib::String(desc, TagLib::String::UTF8));
            frame->setText(TagLib::String(val, TagLib::String::UTF8));
            tag->addFrame(frame);
        };

        addTxxx("SUNO_ID", clip.id);
        addTxxx("SUNO_PROMPT", clip.metadata.prompt);
        addTxxx("SUNO_STYLE", clip.metadata.tags);
        addTxxx("SUNO_MODEL", clip.model_name);

        f.save();
    } else if (ext == ".m4a" || ext == ".mp4") {
        TagLib::MP4::File f(path.c_str());
        if (!f.isValid()) {
            LOG_WARN("SunoDownloader: {} is not a readable MP4 file; left untagged",
                     path.string());
            return;
        }
        TagLib::MP4::Tag* tag = f.tag();
        if (!tag) {
            LOG_WARN("SunoDownloader: no MP4 tag available for {}", path.string());
            return;
        }

        tag->setTitle(TagLib::String(clip.title, TagLib::String::UTF8));
        tag->setArtist(TagLib::String(clip.display_name, TagLib::String::UTF8));
        tag->setAlbum(TagLib::String("Suno AI Generations", TagLib::String::UTF8));
        tag->setComment(TagLib::String(clip.id, TagLib::String::UTF8));

        // The same custom metadata the MP3 branch writes, as freeform atoms.
        // "----:mean:name" is the key shape iTunes uses for a freeform atom
        // ("com.apple.iTunes" is the mean, the name is the field), and it is
        // what a player reads back.
        const auto setCustom = [&](const char* name, const std::string& value) {
            tag->setItem(TagLib::String(std::string("----:com.apple.iTunes:") + name),
                         TagLib::MP4::Item(TagLib::StringList{
                             TagLib::String(value, TagLib::String::UTF8)}));
        };
        setCustom("SUNO_ID", clip.id);
        setCustom("SUNO_PROMPT", clip.metadata.prompt);
        setCustom("SUNO_STYLE", clip.metadata.tags);
        setCustom("SUNO_MODEL", clip.model_name);

        f.save();
    } else if (ext == ".flac") {
        TagLib::FLAC::File f(path.c_str());
        if (!f.isValid()) {
            LOG_WARN("SunoDownloader: {} is not a readable FLAC file; left untagged",
                     path.string());
            return;
        }
        TagLib::Ogg::XiphComment* tag = f.xiphComment(true);
        if (!tag) {
            LOG_WARN("SunoDownloader: no XiphComment available for {}", path.string());
            return;
        }

        tag->setTitle(TagLib::String(clip.title, TagLib::String::UTF8));
        tag->setArtist(TagLib::String(clip.display_name, TagLib::String::UTF8));
        tag->addField("LYRICS", TagLib::String(clip.metadata.lyrics, TagLib::String::UTF8));
        tag->addField("SUNO_ID", TagLib::String(clip.id, TagLib::String::UTF8));
        tag->addField("SUNO_PROMPT", TagLib::String(clip.metadata.prompt, TagLib::String::UTF8));

        f.save();
    }
}

/// The `SUNO_ID` embedded in a saved file, or nullopt.
///
/// T0169. tagAudioFile writes this frame for every clip it tags (ID3v2 TXXX for
/// MP3, a XiphComment field for FLAC), which makes it the one value that can
/// tell two files apart when their titles match. resolveDestPath needs it:
/// "is this plain-named file mine, or a different clip's from an earlier
/// session?" is a question only the tag can answer.
///
/// Returns nullopt for a file that cannot be read or that carries no id -- an
/// untagged file from outside the app, or a partial download. Never throws:
/// TagLib's constructors are the only paths that could, and both are caught.
std::optional<std::string> SunoDownloader::embeddedSunoId(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return std::nullopt;
    }
    try {
        const std::string lower = [](std::string ext) {
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext;
        }(path.extension().string());

        if (lower == ".mp3") {
            TagLib::MPEG::File f(path.c_str());
            if (!f.isValid() || !f.hasID3v2Tag()) {
                return std::nullopt;
            }
            const TagLib::ID3v2::FrameList frames =
                f.ID3v2Tag()->frameListMap()[ "TXXX" ];
            for (const auto* frame : frames) {
                const auto* txxx =
                    dynamic_cast<const TagLib::ID3v2::UserTextIdentificationFrame*>(frame);
                if (txxx && txxx->description().upper() == "SUNO_ID") {
                    // fieldList()[1], NOT fieldList().toString(). A TXXX frame's
                    // text list is ["SUNO_ID", "<the value>"] -- the description is
                    // field 0 -- so toString() renders "SUNO_ID <value>" and could
                    // never equal a clip id, while [1] is exactly what the tagger
                    // put there. Reading what was written, not a rendering of it.
                    //
                    // The size check is T0202: a frame with a description and no
                    // value is legal ID3, and indexing [1] on it read past the end
                    // of the list. Keep looking rather than returning -- a later
                    // TXXX may be the well-formed one.
                    const TagLib::StringList fields = txxx->fieldList();
                    if (fields.size() < 2) {
                        continue;
                    }
                    return std::string(fields[1].to8Bit());
                }
            }
            return std::nullopt;
        }

        if (lower == ".flac") {
            // Same FLAC::File + xiphComment() API the tagger writes through, so
            // the reader and writer cannot disagree about where the field lives.
            TagLib::FLAC::File f(path.c_str());
            if (!f.isValid()) {
                return std::nullopt;
            }
            TagLib::Ogg::XiphComment* tag = f.xiphComment();
            if (!tag) {
                return std::nullopt;
            }
            const TagLib::String value = tag->fieldListMap()["SUNO_ID"].toString();
            if (value.isEmpty()) {
                return std::nullopt;
            }
            return std::string(value.to8Bit());
        }
        return std::nullopt;
    } catch (const std::exception&) {
        // A malformed tag is not an error worth failing a download over: the
        // file is simply not identifiable, which resolveDestPath treats the
        // same way it treats a file with no id at all.
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool SunoDownloader::addAndPlay(const fs::path& path, const std::string& clipId) {
    if (clipId.empty() || !audioEngine_) {
        return false;
    }

    auto& playlist = audioEngine_->playlist();
    const auto previousSize = playlist.size();
    playlist.addFile(path);
    if (playlist.size() == previousSize) {
        return false;
    }

    const auto index = playlist.size() - 1;
    if (!playlist.jumpTo(index)) {
        return false;
    }

    if (const auto current = playlist.currentItem();
        !current || current->path != path) {
        return false;
    }

    emit playbackReady(QString::fromStdString(clipId));
    return true;
}

void SunoDownloader::saveLyricsSidecar(const std::string& clipId,
                                      const std::string& json,
                                      const QJsonDocument&,
                                      const std::vector<SunoClip>& clips) {
    auto clip = resolveClip(clips, db_, clipId);
    if (!clip) {
        clip = SunoClip{};
        clip->id = clipId;
    }

    const auto data = LyricsAligner::parseCapturedSunoLyrics(json, *clip);
    if (!data || data->lines.empty()) {
        return;
    }

    // Derived from the audio path, not from a second sanitize-and-append, so a
    // disambiguated destination (two clips sharing a title) still gets its
    // lyrics written next to the file it belongs to.
    fs::path audioPath = defaultDestPathFor(clipId);
    if (!fs::exists(audioPath)) {
        return;
    }

    const fs::path srtPath = audioPath.replace_extension(".srt");
    std::ofstream sf(srtPath);
    if (!sf) {
        return;
    }

    auto formatTime = [](double seconds) {
        const int totalMs = static_cast<int>(seconds * 1000.0);
        const int ms = totalMs % 1000;
        const int totalSec = totalMs / 1000;
        const int sec = totalSec % 60;
        const int min = (totalSec / 60) % 60;
        const int hr = totalSec / 3600;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", hr, min, sec, ms);
        return std::string(buf);
    };

    int index = 1;
    for (const auto& line : data->lines) {
        if (line.isInstrumental) {
            continue;
        }
        sf << index++ << "\n"
           << formatTime(line.startTime) << " --> " << formatTime(line.endTime) << "\n"
           << line.text << "\n\n";
    }
}

void SunoDownloader::saveMetadataSidecar(fs::path audioPath, const SunoClip& clip) {
    // T0037: takes the already-resolved audio path instead of calling
    // resolveDestPath() itself. resolveDestPath reads destByClip_/destOwner_,
    // which are GUI-thread members; a sidecar write now runs on the finalize
    // thread, and reading them from there would be a data race. The caller
    // already resolved the path on the GUI thread (handleItemState), so this is
    // a removal of duplicated work, not a new responsibility.
    //
    // replace_extension on that path: the sidecar can only ever land beside the
    // file it describes, disambiguated or not. Taken by value because
    // replace_extension mutates in place and the caller's copy is not ours to
    // rewrite.
    fs::path sidecar = audioPath.replace_extension(".txt");

    std::ofstream file(sidecar);
    if (file) {
        file << "Title: " << clip.title << "\nArtist: " << clip.display_name << "\nTrack ID: " << clip.id << "\nPrompt: " << clip.metadata.prompt << "\nTags: " << clip.metadata.tags << "\nLyrics:\n" << clip.metadata.lyrics;
    }
}

} // namespace vc::suno
