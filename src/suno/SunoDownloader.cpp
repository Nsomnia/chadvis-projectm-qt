#include "suno/SunoDownloader.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/ClipResolver.hpp"
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
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/textidentificationframe.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>

namespace vc::suno {

namespace {

/// The one extension this downloader writes. A WAV request is rejected
/// before a destination is ever chosen (see download()), so a format
/// parameter on the destination helpers would be a parameter with exactly
/// one legal value.
constexpr std::string_view kAudioExtension = ".mp3";

} // namespace

SunoDownloader::SunoDownloader(SunoClient*,
                               SunoDatabase& db,
                               AudioEngine* audioEngine,
                               QNetworkAccessManager* networkManager,
                               QObject* parent)
    : QObject(parent),
      db_(db),
      audioEngine_(audioEngine),
      // The queue adopts the controller-provided manager; this is the one
      // QNetworkAccessManager for clip downloads (no ad-hoc managers here).
      queue_(std::make_unique<DownloadQueue>(networkManager, this)) {
    wireQueue();
}

SunoDownloader::SunoDownloader(SunoDatabase& db,
                               AudioEngine* audioEngine,
                               ReplyFactory replyFactory)
    : QObject(nullptr),
      db_(db),
      audioEngine_(audioEngine),
      queue_(std::make_unique<DownloadQueue>(std::move(replyFactory), this)) {
    wireQueue();
}

SunoDownloader::~SunoDownloader() = default;

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
    if (!url.isValid() || url.isRelative()) return false;

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("https")) {
        return false;
    }

    const int port = url.port();
    if (port != -1 && port != 443) return false;
    if (!url.userInfo().isEmpty() || !url.fragment().isEmpty() ||
        url.path().isEmpty()) {
        return false;
    }
    if (url.host().compare(QStringLiteral("audiopipe.suno.ai"),
                           Qt::CaseInsensitive) != 0) {
        return false;
    }

    return !url.path().contains(QStringLiteral("api/forbidden"),
                                Qt::CaseInsensitive);
}

}

bool SunoDownloader::isSupportedMediaUrl(const QString& url)
{
    return isUsableCapturedUrl(url.toStdString());
}

std::optional<std::string>
SunoDownloader::selectDownloadUrl(const SunoClip& clip,
                                  const vc::SunoDownloadFormat format) {
    if (format != vc::SunoDownloadFormat::MP3 || clip.status != "complete") {
        return std::nullopt;
    }

    for (const auto& media : clip.media_urls) {
        if (media.content_type == "mp3" && isUsableCapturedUrl(media.url)) {
            return media.url;
        }
    }

    return std::nullopt;
}

QString SunoDownloader::noUsableMediaMessage(const SunoClip& clip) {
    if (clip.status != "complete") {
        return QStringLiteral("This clip is not finished (status: %1), so there is no audio to "
                              "save yet.")
            .arg(clip.status.empty() ? QStringLiteral("unknown")
                                     : QString::fromStdString(clip.status));
    }
    // Deliberately says nothing about which media types exist or might be
    // usable: it only restates the fail-closed decision selectDownloadUrl()
    // already made, so that a dead button is at least an explained one.
    return QStringLiteral(
        "This clip has no playable audio on the captured media host, so there is "
        "nothing to save.");
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

    const auto selectedUrl = selectDownloadUrl(clip, format);
    if (!selectedUrl) {
        LOG_WARN("SunoDownloader: no usable captured MP3 URL for clip {} (status {})",
                 clip.id, clip.status);
        return std::unexpected(noUsableMediaMessage(clip));
    }

    const fs::path targetPath = resolveDestPath(clip);
    if (fs::exists(targetPath)) {
        return finishAlreadyOnDisk(clip, targetPath, action);
    }

    enqueueAudio(clip, *selectedUrl, action);
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

fs::path SunoDownloader::resolveDestPath(const SunoClip& clip) {
    if (const auto known = destByClip_.find(clip.id); known != destByClip_.end()) {
        return known->second;
    }

    const fs::path dir = getDownloadDir();
    const std::string stem = safeStem(clip.title, clip.id);
    fs::path candidate = dir / (stem + std::string(kAudioExtension));

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

    if (destOwner_.contains(claim(candidate))) {
        // clip.id is sanitized because it is interpolated into a path here, and
        // ClipParser::parseClip validates only that it is non-empty -- so an id
        // containing "../" would otherwise write outside the download directory.
        // Remote data reaching a path unsanitized is the exact bar at which this
        // matters; a hostile upstream is a real possibility for a scraped id.
        candidate = dir / (stem + "-" + vc::file::sanitizeFilename(clip.id) +
                           std::string(kAudioExtension));
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
    return getDownloadDir() / (safeStem(clip ? clip->title : std::string{}, clipId) +
                               std::string(kAudioExtension));
}

/// Route one transfer through the shared DownloadQueue; tagging/sidecars run
/// from the completion hook in handleItemState().
void SunoDownloader::enqueueAudio(const SunoClip& clip,
                                  const std::string& url,
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

    const fs::path targetPath = resolveDestPath(clip);
    pendingClips_.emplace(clip.id, PendingDownload{clip, targetPath, action});
    if (!queue_->enqueue(clip.id, url, targetPath)) {
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
            // The file, its tags and its sidecars are all in place before the
            // path can reach the audio engine, so a synchronous lyrics fetch
            // triggered from addAndPlay() finds a complete download directory.
            tagAudioFile(pending.destPath, pending.clip);
            saveMetadataSidecar(pending.clip);
            if (pending.action == DownloadAction::SaveAndPlay) {
                static_cast<void>(addAndPlay(pending.destPath, pending.clip.id));
            }
            emit fileSaved(QString::fromStdString(clipId),
                           QString::fromStdString(pending.destPath.string()));
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

void SunoDownloader::saveMetadataSidecar(const SunoClip& clip) {
    // replace_extension on the resolved audio path: the sidecar can only ever
    // land beside the file it describes, disambiguated or not.
    const fs::path sidecar = resolveDestPath(clip).replace_extension(".txt");

    std::ofstream file(sidecar);
    if (file) {
        file << "Title: " << clip.title << "\nArtist: " << clip.display_name << "\nTrack ID: " << clip.id << "\nPrompt: " << clip.metadata.prompt << "\nTags: " << clip.metadata.tags << "\nLyrics:\n" << clip.metadata.lyrics;
    }
}

} // namespace vc::suno
