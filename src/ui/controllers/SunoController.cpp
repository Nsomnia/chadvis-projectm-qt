#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "ui/SunoCookieDialog.hpp"
#include "util/FileUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>

namespace vc::suno {

SunoController::SunoController(AudioEngine* audioEngine,
                               OverlayEngine* overlayEngine,
                               QObject* parent)
    : QObject(parent),
      audioEngine_(audioEngine),
      overlayEngine_(overlayEngine),
      client_(std::make_unique<SunoClient>(nullptr)),
      networkManager_(new QNetworkAccessManager(this)) {
    // Set client parent manually to avoid conversion issues if any
    client_->setParent(this);

    // Connect client signals
    client_->libraryFetched.connect(
            [this](const auto& clips) { onLibraryFetched(clips); });
    client_->alignedLyricsFetched.connect(
            [this](const auto& id, const auto& json) {
                onAlignedLyricsFetched(id, json);
            });
    client_->errorOccurred.connect([this](const auto& msg) { onError(msg); });

    // Initialize database
    fs::path dbPath = file::dataDir() / "suno_library.db";
    db_.init(dbPath.string());

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
        showCookieDialog();
        return;
    }
    client_->fetchLibrary();
}

void SunoController::syncDatabase() {
    refreshLibrary();
}

void SunoController::showCookieDialog() {
    auto* dialog = new ui::SunoCookieDialog();
    if (dialog->exec() == QDialog::Accepted) {
        std::string cookie = dialog->getCookie().toStdString();
        client_->setCookie(cookie);
        // We could save this to config too
        client_->fetchLibrary();
    }
    dialog->deleteLater();
}

void SunoController::onLibraryFetched(const std::vector<SunoClip>& clips) {
    LOG_INFO("SunoController: Fetched {} clips", clips.size());
    db_.saveClips(clips);
    libraryUpdated.emitSignal(clips);

    // Automatically fetch aligned lyrics for clips that don't have them
    for (const auto& clip : clips) {
        if (db_.getAlignedLyrics(clip.id).isErr()) {
            client_->fetchAlignedLyrics(clip.id);
        }
    }
}

void SunoController::onAlignedLyricsFetched(const std::string& clipId,
                                            const std::string& json) {
    LOG_INFO("SunoController: Fetched aligned lyrics for {}", clipId);
    db_.saveAlignedLyrics(clipId, json);

    // If this is the currently playing clip, update overlay
    // (Assuming we have a way to know what's playing)

    // Parse JSON
    QJsonDocument doc =
            QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (doc.isArray() || (doc.isObject() && doc.object().contains("words"))) {
        AlignedLyrics lyrics;
        lyrics.songId = clipId;

        QJsonArray arr =
                doc.isArray() ? doc.array() : doc.object()["words"].toArray();
        // In some versions it's "aligned_words"
        if (arr.isEmpty() && doc.isObject()) {
            arr = doc.object()["aligned_words"].toArray();
        }

        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            AlignedWord w;
            w.word = obj["word"].toString().toStdString();
            w.start_s = obj["start_s"].toDouble(obj["start"].toDouble());
            w.end_s = obj["end_s"].toDouble(obj["end"].toDouble());
            w.score = obj["p_align"].toDouble(obj["score"].toDouble());
            lyrics.words.push_back(w);
        }
        overlayEngine_->setAlignedLyrics(lyrics);
    }
}

void SunoController::onError(const std::string& message) {
    LOG_ERROR("SunoController: {}", message);
    statusMessage.emitSignal(message);
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
    audioEngine_->playlist().addFile(path);

    // If we have aligned lyrics, prepare them for overlay
    auto lyricsRes = db_.getAlignedLyrics(clip.id);
    if (lyricsRes.isOk()) {
        LOG_INFO("SunoController: Loaded aligned lyrics for {}", clip.title);
        // TODO: Pass to overlay engine
    }
}

} // namespace vc::suno
