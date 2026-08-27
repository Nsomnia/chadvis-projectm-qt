#include "suno/SunoDownloader.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/ClipResolver.hpp"
#include "suno/SunoEndpoints.hpp"
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

SunoDownloader::SunoDownloader(SunoClient* client,
                               SunoDatabase& db,
                               AudioEngine* audioEngine,
                               QNetworkAccessManager* networkManager,
                               QObject* parent)
    : QObject(parent),
      client_(client),
      db_(db),
      // The queue adopts the controller-provided manager; this is the one
      // QNetworkAccessManager for clip downloads (no ad-hoc managers here).
      queue_(std::make_unique<DownloadQueue>(networkManager, this)) {

    client_->wavConversionReady.connect(
        [this](const auto& id, const auto& url) {
            onWavConversionReady(id, url);
        });

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

void SunoDownloader::downloadAndPlay(const SunoClip& clip) {
    if (clip.id.empty()) return;

    std::string extension = ".mp3";
    bool useWav = (CONFIG.suno().downloadFormat == vc::SunoDownloadFormat::WAV);
    if (useWav) extension = ".wav";

    if (clip.audio_url.empty()) {
        SunoClip resolvedClip = clip;
        resolvedClip.audio_url =
            std::string(vc::suno::endpoints::CDN_BASE) + "/" + clip.id + ".mp3";
        if (useWav) {
            client_->initiateWavConversion(clip.id);
        } else {
            enqueueAudio(resolvedClip, resolvedClip.audio_url, extension);
        }
        return;
    }

  std::string safeTitle = safeStem(clip.title, clip.id);

  fs::path downloadDir = getDownloadDir();

  fs::path targetPath = downloadDir / (safeTitle + extension);

    if (fs::exists(targetPath)) {
        audioEngine_->playlist().addFile(targetPath);
        audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
        return;
    }

    if (useWav) {
        client_->initiateWavConversion(clip.id);
    } else {
        enqueueAudio(clip, clip.audio_url, extension);
    }
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

void SunoDownloader::processDownloadedFile(const SunoClip& clip, const fs::path& path) {
    audioEngine_->playlist().addFile(path);
    audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
}

void SunoDownloader::onWavConversionReady(const std::string& clipId, const std::string& wavUrl) {
    // Resolve the clip up front so the completion hook has full metadata.
    auto clipOpt = resolveClip({}, db_, clipId);
    if (clipOpt) {
        enqueueAudio(*clipOpt, wavUrl, ".wav");
        return;
    }

    SunoClip stub;
    stub.id = clipId;
    stub.title = clipId;
    enqueueAudio(stub, wavUrl, ".wav");
}

void SunoDownloader::saveLyricsSidecar(const std::string& clipId, const std::string& json, const QJsonDocument& doc, const std::vector<SunoClip>& clips) {
  fs::path saveDir = getDownloadDir();

  // Library cache first, database fallback (shared ClipResolver).
  std::string prompt;
  std::string title;
  if (auto clipOpt = resolveClip(clips, db_, clipId)) {
    prompt = clipOpt->metadata.prompt;
    title = clipOpt->title;
  }

  std::string safeTitle = safeStem(title, clipId);

    fs::path audioPath = saveDir / (safeTitle + ".mp3");
    if (fs::exists(audioPath)) {
        fs::path srtPath = saveDir / (safeTitle + ".srt");
        auto words = LyricsAligner::parseJson(QByteArray::fromStdString(json));
        if (!words.empty()) {
            AlignedLyrics lyrics = LyricsAligner::align(prompt, words);
            if (!lyrics.lines.empty()) {
                std::ofstream sf(srtPath);
                if (sf) {
                    int index = 1;
                    for (const auto& line : lyrics.lines) {
                        auto fmtTime = [](double s) {
                            int ms = (int)((s - (int)s) * 1000);
                            int totSec = (int)s;
                            int hr = totSec / 3600;
                            int mn = (totSec % 3600) / 60;
                            int sc = totSec % 60;
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%03d", hr, mn, sc, ms);
                            return std::string(buf);
                        };
                        sf << index++ << "\n" << fmtTime(line.start_s) << " --> " << fmtTime(line.end_s) << "\n" << line.text << "\n\n";
                    }
                }
            }
        }
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
