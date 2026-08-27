/*
 * ChadVis - ProjectM 4.0 Qt Frontend
 * Copyright (c) 2026 Nsomnia
 */

#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "audio/AudioEngine.hpp"
#include "suno/DownloadQueue.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "util/Result.hpp"

namespace fs = std::filesystem;

class QJsonDocument;

namespace vc::suno {

/// High-level download orchestration: decides destinations, feeds the shared
/// DownloadQueue, then tags/sidecars finished files. All network transfer
/// mechanics (retry/resume/concurrency) live in DownloadQueue.
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

signals:
    /// Forwarded from the DownloadQueue for controller/bridge progress wiring.
    void downloadStateChanged(const QString& clipId, int state, int progressPercent);
    void downloadQueueIdle();

private:
    SunoClient* client_;
    SunoDatabase& db_;
    AudioEngine* audioEngine_;
    std::unique_ptr<DownloadQueue> queue_;

    struct PendingDownload {
        SunoClip clip;
        fs::path destPath;
    };

    /// Clip payloads awaiting their queue completion hook.
    std::unordered_map<std::string, PendingDownload> pendingClips_;

    /// Sanitize a title and fall back to the clip id when the result is empty.
    [[nodiscard]] static std::string safeStem(std::string_view title, const std::string& clipId);

    void enqueueAudio(const SunoClip& clip, const std::string& url, const std::string& extension);
    void handleItemState(const std::string& clipId, DownloadState state);
    void onWavConversionReady(const std::string& clipId, const std::string& wavUrl);
    void processDownloadedFile(const SunoClip& clip, const fs::path& path);

    [[nodiscard]] fs::path getDownloadDir() const;
};

} // namespace vc::suno
