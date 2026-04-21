/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <string>
#include <memory>
#include <filesystem>

#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "audio/AudioEngine.hpp"
#include "util/Result.hpp"

namespace fs = std::filesystem;

namespace vc::suno {

class SunoDownloader : public QObject {
    Q_OBJECT

public:
    explicit SunoDownloader(SunoClient* client, 
                           SunoDatabase& db, 
                           AudioEngine* audioEngine,
                           QNetworkAccessManager* networkManager,
                           QObject* parent = nullptr);
    ~SunoDownloader() override;

    void downloadAndPlay(const SunoClip& clip);
    void saveLyricsSidecar(const std::string& clipId, 
                          const std::string& json,
                          const QJsonDocument& doc,
                          const std::vector<SunoClip>& clips);
    void saveMetadataSidecar(const SunoClip& clip);
    
    // Embedded tagging functionality
    void tagAudioFile(const fs::path& path, const SunoClip& clip);

private:
    SunoClient* client_;
    SunoDatabase& db_;
    AudioEngine* audioEngine_;
    QNetworkAccessManager* networkManager_;

    void downloadAudio(const SunoClip& clip);
    void downloadAudioFromUrl(const std::string& clipId,
                             const std::string& url,
                             const std::string& extension);
    void onWavConversionReady(const std::string& clipId, const std::string& wavUrl);
    void processDownloadedFile(const SunoClip& clip, const fs::path& path);
};

} // namespace vc::suno
