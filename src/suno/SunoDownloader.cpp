#include "suno/SunoDownloader.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
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
      audioEngine_(audioEngine), 
      networkManager_(networkManager) {

    client_->wavConversionReady.connect(
        [this](const auto& id, const auto& url) {
            onWavConversionReady(id, url);
        });
}

SunoDownloader::~SunoDownloader() = default;

void SunoDownloader::downloadAndPlay(const SunoClip& clip) {
    if (clip.id.empty()) return;

    std::string extension = ".mp3";
    bool useWav = (CONFIG.suno().downloadFormat == vc::SunoDownloadFormat::WAV);
    if (useWav) extension = ".wav";

    if (clip.audio_url.empty()) {
        SunoClip resolvedClip = clip;
        resolvedClip.audio_url = "https://cdn1.suno.ai/" + clip.id + ".mp3";
        if (useWav) {
            client_->initiateWavConversion(clip.id);
        } else {
            downloadAudio(resolvedClip);
        }
        return;
    }

    std::string safeTitle = clip.title;
    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    if (safeTitle.empty()) safeTitle = clip.id;

    fs::path downloadDir = CONFIG.suno().downloadPath;
    if (downloadDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        downloadDir = fs::path(musicLoc.toStdString());
    }
    file::ensureDir(downloadDir);

    fs::path targetPath = downloadDir / (safeTitle + extension);

    if (fs::exists(targetPath)) {
        audioEngine_->playlist().addFile(targetPath);
        audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
        return;
    }

    if (useWav) {
        client_->initiateWavConversion(clip.id);
    } else {
        downloadAudio(clip);
    }
}

void SunoDownloader::downloadAudio(const SunoClip& clip) {
    QUrl url(QString::fromStdString(clip.audio_url));
    QNetworkRequest request(url);
    QNetworkReply* reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, clip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        vc::file::ensureDir(downloadDir);

        std::string safeTitle = clip.title;
        std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
        if (safeTitle.empty()) safeTitle = clip.id;

        fs::path filePath = downloadDir / (safeTitle + ".mp3");

        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            tagAudioFile(filePath, clip);
            processDownloadedFile(clip, filePath);
            saveMetadataSidecar(clip);
        }
    });
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
    auto clipOpt = db_.getClip(clipId);
    if (clipOpt.isOk() && clipOpt.value()) {
        downloadAudioFromUrl(clipId, wavUrl, ".wav");
    } else {
        downloadAudioFromUrl(clipId, wavUrl, ".wav");
    }
}

void SunoDownloader::downloadAudioFromUrl(const std::string& clipId, const std::string& url, const std::string& extension) {
    QUrl qurl(QString::fromStdString(url));
    QNetworkRequest request(qurl);
    QNetworkReply* reply = networkManager_->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, clipId, extension]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        
        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        vc::file::ensureDir(downloadDir);
        
        std::string safeTitle = clipId;
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) safeTitle = clipOpt.value()->title;
        
        std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
        
        fs::path filePath = downloadDir / (safeTitle + extension);
        
        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            
            if (clipOpt.isOk() && clipOpt.value()) {
                tagAudioFile(filePath, *clipOpt.value());
                processDownloadedFile(*clipOpt.value(), filePath);
                saveMetadataSidecar(*clipOpt.value());
            } else {
                SunoClip clip;
                clip.id = clipId;
                clip.title = safeTitle;
                processDownloadedFile(clip, filePath);
            }
        }
    });
}

void SunoDownloader::saveLyricsSidecar(const std::string& clipId, const std::string& json, const QJsonDocument& doc, const std::vector<SunoClip>& clips) {
    fs::path saveDir = CONFIG.suno().downloadPath;
    if (saveDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        saveDir = fs::path(musicLoc.toStdString());
    }

    std::string safeTitle = clipId;
    std::string prompt;
    bool found = false;
    for (const auto& clip : clips) {
        if (clip.id == clipId) {
            safeTitle = clip.title;
            prompt = clip.metadata.prompt;
            found = true;
            break;
        }
    }
    
    if (!found) {
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
            safeTitle = clipOpt.value()->title;
            prompt = clipOpt.value()->metadata.prompt;
        }
    }

    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    
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
    fs::path downloadDir = CONFIG.suno().downloadPath;
    if (downloadDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        downloadDir = fs::path(musicLoc.toStdString());
    }
    
    std::string safeTitle = clip.title;
    std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
    std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
    
    std::ofstream file(downloadDir / (safeTitle + ".txt"));
    if (file) {
        file << "Title: " << clip.title << "\nArtist: " << clip.display_name << "\nTrack ID: " << clip.id << "\nPrompt: " << clip.metadata.prompt << "\nTags: " << clip.metadata.tags << "\nLyrics:\n" << clip.metadata.lyrics;
    }
}

} // namespace vc::suno
