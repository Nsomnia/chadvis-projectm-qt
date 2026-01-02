#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "util/FileUtils.hpp"

#include <QFile>
#include <QNetworkReply>
#include <QUrl>

namespace vc::suno {

SunoController::SunoController(AudioEngine* audioEngine,
                               OverlayEngine* overlayEngine,
                               QObject* parent)
    : QObject(parent),
      audioEngine_(audioEngine),
      overlayEngine_(overlayEngine),
      client_(std::make_unique<SunoClient>(this)),
      networkManager_(new QNetworkAccessManager(this)) {
    // Connect client signals
    client_->libraryFetched.connect(
            [this](const auto& clips) { onLibraryFetched(clips); });
    client_->errorOccurred.connect([this](const auto& msg) { onError(msg); });

    // Load token from config
    if (!CONFIG.suno().token.empty()) {
        client_->setToken(CONFIG.suno().token);
    }

    downloadDir_ = CONFIG.suno().downloadPath;
    if (downloadDir_.empty()) {
        downloadDir_ = file::dataDir() / "suno_downloads";
    }
    file::ensureDir(downloadDir_);
}

SunoController::~SunoController() = default;

void SunoController::refreshLibrary() {
    if (!client_->isAuthenticated()) {
        LOG_WARN("SunoController: Cannot refresh, not authenticated");
        return;
    }
    client_->fetchLibrary();
}

void SunoController::onLibraryFetched(const std::vector<SunoClip>& clips) {
    LOG_INFO("SunoController: Fetched {} clips", clips.size());
    libraryUpdated.emitSignal(clips);
}

void SunoController::onError(const std::string& message) {
    LOG_ERROR("SunoController: {}", message);
}

void SunoController::downloadAndPlay(const SunoClip& clip) {
    if (clip.audio_url.empty()) {
        LOG_ERROR("SunoController: No audio URL for {}", clip.title);
        return;
    }
    downloadAudio(clip);
}

void SunoController::downloadAudio(const SunoClip& clip) {
    LOG_INFO("SunoController: Downloading {}", clip.title);

    QUrl url(QString::fromStdString(clip.audio_url));
    QNetworkRequest request(url);

    QNetworkReply* reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, clip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("SunoController: Download failed: {}",
                      reply->errorString().toStdString());
            return;
        }

        QString fileName =
                QString::fromStdString(clip.title).replace(" ", "_") + ".mp3";
        fs::path filePath = downloadDir_ / fileName.toStdString();

        QFile file(QString::fromStdString(filePath.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            LOG_INFO("SunoController: Saved to {}", filePath.string());
            processDownloadedFile(clip, filePath);
        } else {
            LOG_ERROR("SunoController: Failed to open file for writing: {}",
                      filePath.string());
        }
    });
}

void SunoController::processDownloadedFile(const SunoClip& clip,
                                           const fs::path& path) {
    // 1. Embed metadata/lyrics (Todo)
    // 2. Add to playlist
    audioEngine_->playlist().addFile(path);

    // 3. Update overlay with lyrics if playing
    if (!clip.metadata.lyrics.empty()) {
        LOG_INFO("SunoController: Found lyrics ({} chars)",
                 clip.metadata.lyrics.size());
    }
}

} // namespace vc::suno
