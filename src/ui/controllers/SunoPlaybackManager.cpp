#include "SunoPlaybackManager.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/SunoClient.hpp"
#include "util/FileUtils.hpp"

#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QUrl>

namespace vc::suno {

SunoPlaybackManager::SunoPlaybackManager(SunoClient* client, AudioEngine* audioEngine)
    : client_(client)
    , audioEngine_(audioEngine)
    , networkManager_(new QNetworkAccessManager(this)) {}

void SunoPlaybackManager::downloadAndPlay(const SunoClip& clip) {
    if (clip.id.empty()) return;

    std::string extension = ".mp3";
    bool useWav = (CONFIG.suno().downloadFormat == vc::SunoDownloadFormat::WAV);
    if (useWav) extension = ".wav";

    if (clip.audio_url.empty()) {
        LOG_INFO("SunoPlaybackManager: Resolving clip {}", clip.id);
        
        SunoClip resolved = clip;
        resolved.audio_url = "https://cdn1.suno.ai/" + clip.id + ".mp3";
        resolved.title = clip.id;
        
        client_->fetchAlignedLyrics(clip.id);
        
        if (useWav) {
            client_->initiateWavConversion(clip.id);
        } else {
            downloadAudio(resolved);
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
        LOG_INFO("SunoPlaybackManager: Playing local file: {}", targetPath.string());
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

void SunoPlaybackManager::onWavConversionReady(const std::string& clipId, const std::string& wavUrl) {
    LOG_INFO("SunoPlaybackManager: WAV ready for {} at {}", clipId, wavUrl);
    downloadFromUrl(clipId, wavUrl, ".wav");
}

void SunoPlaybackManager::downloadFromUrl(const std::string& clipId, const std::string& url, const std::string& extension) {
    LOG_INFO("SunoPlaybackManager: Downloading from {} ext {}", url, extension);
    
    QUrl qurl(QString::fromStdString(url));
    QNetworkRequest request(qurl);
    QNetworkReply* reply = networkManager_->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, clipId, extension]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoPlaybackManager: Download failed: {}", reply->errorString().toStdString());
            return;
        }
        
        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        file::ensureDir(downloadDir);
        
        std::string safeTitle = clipId;
        if (clipsRef_) {
            for (const auto& clip : *clipsRef_) {
                if (clip.id == clipId) {
                    safeTitle = clip.title;
                    break;
                }
            }
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
            LOG_INFO("SunoPlaybackManager: Saved to {}", filePath.string());
            
            if (clipsRef_) {
                for (const auto& clip : *clipsRef_) {
                    if (clip.id == clipId) {
                        processDownloadedFile(clip, filePath);
                        saveMetadataSidecar(clip);
                        break;
                    }
                }
            }
        } else {
            LOG_ERROR("SunoPlaybackManager: Failed to write: {}", filePath.string());
        }
    });
}

void SunoPlaybackManager::downloadAudio(const SunoClip& clip) {
    LOG_INFO("SunoPlaybackManager: Downloading {}", clip.title);

    QUrl url(QString::fromStdString(clip.audio_url));
    QNetworkRequest request(url);
    QNetworkReply* reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, clip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoPlaybackManager: Download failed: {}", reply->errorString().toStdString());
            return;
        }

        fs::path downloadDir = CONFIG.suno().downloadPath;
        if (downloadDir.empty()) {
            QString musicLoc = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (musicLoc.isEmpty()) musicLoc = QDir::homePath() + "/Music";
            downloadDir = fs::path(musicLoc.toStdString());
        }
        file::ensureDir(downloadDir);

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
            LOG_INFO("SunoPlaybackManager: Saved to {}", filePath.string());
            processDownloadedFile(clip, filePath);
        } else {
            LOG_ERROR("SunoPlaybackManager: Failed to write: {}", filePath.string());
        }
    });
}

void SunoPlaybackManager::processDownloadedFile(const SunoClip& clip, const fs::path& path) {
    audioEngine_->playlist().addFile(path);
    audioEngine_->playlist().jumpTo(audioEngine_->playlist().size() - 1);
    fileDownloaded.emitSignal(clip, path);
}

void SunoPlaybackManager::saveMetadataSidecar(const SunoClip& clip) {
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
        LOG_ERROR("SunoPlaybackManager: Failed to create metadata sidecar: {}", txtPath.string());
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
    LOG_INFO("SunoPlaybackManager: Saved metadata to {}", txtPath.string());
}

} // namespace vc::suno
