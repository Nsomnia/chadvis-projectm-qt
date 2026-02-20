#pragma once
#include "suno/SunoClip.hpp"
#include "suno/SunoLyrics.hpp"
#include "util/Types.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <functional>
#include <string>
#include <unordered_map>

namespace vc {

class AudioEngine;
class OverlayEngine;

namespace suno {

class SunoClient;

class SunoPlaybackManager : public QObject {
    Q_OBJECT

public:
    explicit SunoPlaybackManager(SunoClient* client, AudioEngine* audioEngine);
    ~SunoPlaybackManager() override = default;

    void downloadAndPlay(const SunoClip& clip);
    void downloadFromUrl(const std::string& clipId, const std::string& url, const std::string& extension);
    void setClipsRef(const std::vector<SunoClip>* clips) { clipsRef_ = clips; }

    Signal<const SunoClip&, const fs::path&> fileDownloaded;

public slots:
    void onWavConversionReady(const std::string& clipId, const std::string& wavUrl);

private:
    void downloadAudio(const SunoClip& clip);
    void processDownloadedFile(const SunoClip& clip, const fs::path& path);
    void saveMetadataSidecar(const SunoClip& clip);

    SunoClient* client_;
    AudioEngine* audioEngine_;
    QNetworkAccessManager* networkManager_;
    const std::vector<SunoClip>* clipsRef_{nullptr};
};

} // namespace vc::suno
} // namespace vc
