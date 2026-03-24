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

    // Determine file extension based on download format
    std::string extension = ".mp3";
    bool useWav = (CONFIG.suno().downloadFormat == vc::SunoDownloadFormat::WAV);
    if (useWav) {
        extension = ".wav";
    }

    if (clip.audio_url.empty()) {
        LOG_INFO("SunoDownloader: Resolving clip details for ID {}", clip.id);
        
        SunoClip resolvedClip = clip;
        resolvedClip.audio_url = "https://cdn1.suno.ai/" + clip.id + ".mp3";
        resolvedClip.title = clip.id;
        
        // Note: Lyrics fetching is handled by SunoController/LyricsManager, 
        // but we trigger the conversion here if needed
        
        if (useWav) {
            LOG_INFO("SunoDownloader: Initiating WAV conversion for {}", clip.id);
            client_->initiateWavConversion(clip.id);
        } else {
            downloadAudio(resolvedClip);
        }
        return;
    }

    // Sanitize filename
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

    // If exists, play local
    if (fs::exists(targetPath)) {
        LOG_INFO("SunoDownloader: Playing local file: {}", targetPath.string());
        audioEngine_->playlist().addFile(targetPath);
        audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
        return;
    }

    if (useWav) {
        LOG_INFO("SunoDownloader: Initiating WAV conversion for {}", clip.id);
        client_->initiateWavConversion(clip.id);
    } else {
        downloadAudio(clip);
    }
}

void SunoDownloader::downloadAudio(const SunoClip& clip) {
    LOG_INFO("SunoDownloader: Downloading {}", clip.title);

    QUrl url(QString::fromStdString(clip.audio_url));
    QNetworkRequest request(url);

    QNetworkReply* reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, clip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoDownloader: Download failed: {}",
                      reply->errorString().toStdString());
            return;
        }

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

        QString fileName = QString::fromStdString(safeTitle) + ".mp3";
        fs::path filePath = downloadDir / fileName.toStdString();

        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            LOG_INFO("SunoDownloader: Saved to {}", filePath.string());
            processDownloadedFile(clip, filePath);
        } else {
            LOG_ERROR("SunoDownloader: Failed to open file for writing: {}",
                      filePath.string());
        }
    });
}

void SunoDownloader::processDownloadedFile(const SunoClip& clip,
                                           const fs::path& path) {
    audioEngine_->playlist().addFile(path);
    // Auto-play the downloaded file
    audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
}

void SunoDownloader::onWavConversionReady(const std::string& clipId,
                                          const std::string& wavUrl) {
    LOG_INFO("SunoDownloader: WAV conversion ready for {} at {}", clipId, wavUrl);
    
    // We need to fetch clip details, but we don't have accumulatedClips here.
    // We'll try the DB.
    auto clipOpt = db_.getClip(clipId);
    if (clipOpt.isOk() && clipOpt.value()) {
        // Create a temporary clip object for the download
        SunoClip clip = *clipOpt.value();
        downloadAudioFromUrl(clipId, wavUrl, ".wav");
    } else {
        // Fallback: try to deduce title or just use ID
         downloadAudioFromUrl(clipId, wavUrl, ".wav");
    }
}

void SunoDownloader::downloadAudioFromUrl(const std::string& clipId,
                                          const std::string& url,
                                          const std::string& extension) {
    LOG_INFO("SunoDownloader: Downloading audio from {} with extension {}", url, extension);
    
    QUrl qurl(QString::fromStdString(url));
    QNetworkRequest request(qurl);
    
    QNetworkReply* reply = networkManager_->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, clipId, extension]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoDownloader: Audio download failed: {}",
                      reply->errorString().toStdString());
            return;
        }
        
        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        vc::file::ensureDir(downloadDir);
        
        // Try to get title from DB for filename
        std::string safeTitle = clipId;
        auto clipOpt = db_.getClip(clipId);
        if (clipOpt.isOk() && clipOpt.value()) {
             safeTitle = clipOpt.value()->title;
        }
        
        std::replace(safeTitle.begin(), safeTitle.end(), '/', '_');
        std::replace(safeTitle.begin(), safeTitle.end(), '\\', '_');
        if (safeTitle.empty()) safeTitle = clipId;
        
        QString fileName = QString::fromStdString(safeTitle) + QString::fromStdString(extension);
        fs::path filePath = downloadDir / fileName.toStdString();
        
        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            LOG_INFO("SunoDownloader: Saved audio to {}", filePath.string());
            
            if (clipOpt.isOk() && clipOpt.value()) {
                processDownloadedFile(*clipOpt.value(), filePath);
                saveMetadataSidecar(*clipOpt.value());
            } else {
                // Synthesize a minimal clip
                SunoClip clip;
                clip.id = clipId;
                clip.title = safeTitle;
                processDownloadedFile(clip, filePath);
            }

        } else {
            LOG_ERROR("SunoDownloader: Failed to open file for writing: {}",
                      filePath.string());
        }
    });
}

void SunoDownloader::saveLyricsSidecar(const std::string& clipId, 
                                       const std::string& json,
                                       const QJsonDocument& doc,
                                       const std::vector<SunoClip>& clips) {
    // Determine save location
    fs::path saveDir = CONFIG.suno().downloadPath;
    if (saveDir.empty()) {
        QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
        saveDir = fs::path(musicLoc.toStdString());
    }

    // Get title for filename
    std::string safeTitle = clipId;
    std::string prompt;
    
    // Search in provided clips (e.g. from library manager)
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
    
    // Also try to find local MP3 and save alongside it
    fs::path audioPath = saveDir / (safeTitle + ".mp3");
    if (fs::exists(audioPath)) {
        fs::path jsonPath = saveDir / (safeTitle + ".json");
        fs::path srtPath = saveDir / (safeTitle + ".srt");
        
        // Save raw JSON
        std::ofstream jf(jsonPath);
        if (jf) {
            jf << json;
            LOG_INFO("SunoDownloader: Saved JSON lyrics to {}", jsonPath.string());
        }
        
        // Generate and save SRT
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
                        
                        sf << index++ << "\n";
                        sf << fmtTime(line.start_s) << " --> " << fmtTime(line.end_s) << "\n";
                        sf << line.text << "\n\n";
                    }
                    LOG_INFO("SunoDownloader: Saved SRT lyrics to {}", srtPath.string());
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
    if (safeTitle.empty()) safeTitle = clip.id;
    
    fs::path txtPath = downloadDir / (safeTitle + ".txt");
    
    std::ofstream file(txtPath);
    if (!file) {
        LOG_ERROR("SunoDownloader: Failed to create metadata sidecar: {}", txtPath.string());
        return;
    }
    
    file << "Title: " << clip.title << "\n";
    file << "Artist: " << clip.display_name << "\n";
    file << "Track ID: " << clip.id << "\n";
    file << "Duration: " << clip.metadata.duration << "\n";
    file << "BPM: " << clip.metadata.bpm << "\n";
    file << "Key: " << clip.metadata.key << "\n";
    file << "Model: " << clip.model_name << " " << clip.major_model_version << "\n";
    file << "Created: " << clip.created_at << "\n";
    file << "\n";
    file << "Tags/Styles: " << clip.metadata.tags << "\n";
    file << "\n";
    file << "Prompt:\n" << clip.metadata.prompt << "\n";
    file << "\n";
    file << "Lyrics:\n" << clip.metadata.lyrics << "\n";
    file << "\n";
    file << "Cover Art URL: " << clip.image_url << "\n";
    file << "Audio URL: " << clip.audio_url << "\n";
    
    file.close();
    LOG_INFO("SunoDownloader: Saved metadata sidecar to {}", txtPath.string());
}

} // namespace vc::suno
