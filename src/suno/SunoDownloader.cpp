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
#include <QUrl>
#include <fstream>
#include <algorithm>

// TagLib Includes
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/textidentificationframe.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>

namespace vc::suno {

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

    connect(queue_.get(), &DownloadQueue::itemStateChanged, this,
            [this](const QString& clipId, int state, int progressPercent) {
                emit downloadStateChanged(clipId, state, progressPercent);
                handleItemState(clipId.toStdString(),
                                static_cast<DownloadState>(state));
            });
    connect(queue_.get(), &DownloadQueue::queueIdle,
            this, &SunoDownloader::downloadQueueIdle);
}

SunoDownloader::~SunoDownloader() = default;

fs::path SunoDownloader::getDownloadDir() const {
  fs::path dir = CONFIG.suno().downloadPath;
  if (dir.empty()) {
    QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
    dir = fs::path(musicLoc.toStdString());
  }
  vc::file::ensureDir(dir);
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

void SunoDownloader::downloadAndPlay(const SunoClip& clip) {
    if (clip.id.empty()) return;

    const auto format = CONFIG.suno().downloadFormat;
    if (format == vc::SunoDownloadFormat::WAV) {
        LOG_WARN("SunoDownloader: WAV download rejected for clip {}: conversion route is LEAD-only",
                 clip.id);
        return;
    }

    const auto selectedUrl = selectDownloadUrl(clip, format);
    if (!selectedUrl) {
        LOG_WARN("SunoDownloader: no usable captured MP3 URL for clip {} (status {})",
                 clip.id, clip.status);
        return;
    }

    const std::string safeTitle = safeStem(clip.title, clip.id);
    const fs::path targetPath =
        getDownloadDir() / (safeTitle + ".mp3");

    if (fs::exists(targetPath)) {
        if (addAndPlay(targetPath, clip.id)) {
            emit downloadStateChanged(QString::fromStdString(clip.id),
                                      static_cast<int>(DownloadState::Completed), 100);
        }
        return;
    }

    enqueueAudio(clip, *selectedUrl, ".mp3");
}

/// Route one transfer through the shared DownloadQueue; tagging/sidecars run
/// from the completion hook in handleItemState().
void SunoDownloader::enqueueAudio(const SunoClip& clip,
                                  const std::string& url,
                                  const std::string& extension) {
    const fs::path targetPath =
        getDownloadDir() / (safeStem(clip.title, clip.id) + extension);
    pendingClips_[clip.id] = PendingDownload{clip, targetPath};
    if (!queue_->enqueue(clip.id, url, targetPath)) {
        pendingClips_.erase(clip.id);  // duplicate live job: queue already owns it
    }
}

void SunoDownloader::handleItemState(const std::string& clipId, DownloadState state) {
    if (!isTerminal(state)) return;

    const auto it = pendingClips_.find(clipId);
    if (it == pendingClips_.end()) return;
    const PendingDownload pending = it->second;
    pendingClips_.erase(it);

    switch (state) {
        case DownloadState::Completed:
            tagAudioFile(pending.destPath, pending.clip);
            processDownloadedFile(pending.clip, pending.destPath);
            saveMetadataSidecar(pending.clip);
            break;
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
        TagLib::ID3v2::Tag* tag = f.ID3v2Tag(true);

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
        TagLib::Ogg::XiphComment* tag = f.xiphComment(true);

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

    if (const auto* current = playlist.currentItem();
        current == nullptr || current->path != path) {
        return false;
    }

    emit playbackReady(QString::fromStdString(clipId));
    return true;
}

bool SunoDownloader::processDownloadedFile(const SunoClip& clip, const fs::path& path) {
    return addAndPlay(path, clip.id);
}

void SunoDownloader::saveLyricsSidecar(const std::string& clipId,
                                      const std::string& json,
                                      const QJsonDocument&,
                                      const std::vector<SunoClip>& clips) {
    const auto saveDir = getDownloadDir();
    auto clip = resolveClip(clips, db_, clipId);
    if (!clip) {
        clip = SunoClip{};
        clip->id = clipId;
    }

    const auto data = LyricsAligner::parseCapturedSunoLyrics(json, *clip);
    if (!data || data->lines.empty()) {
        return;
    }

    const auto audioPath = saveDir / (safeStem(clip->title, clipId) + ".mp3");
    if (!fs::exists(audioPath)) {
        return;
    }

    const auto srtPath = saveDir / (safeStem(clip->title, clipId) + ".srt");
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
  fs::path downloadDir = getDownloadDir();

  std::string safeTitle = safeStem(clip.title, clip.id);

    std::ofstream file(downloadDir / (safeTitle + ".txt"));
    if (file) {
        file << "Title: " << clip.title << "\nArtist: " << clip.display_name << "\nTrack ID: " << clip.id << "\nPrompt: " << clip.metadata.prompt << "\nTags: " << clip.metadata.tags << "\nLyrics:\n" << clip.metadata.lyrics;
    }
}

} // namespace vc::suno
