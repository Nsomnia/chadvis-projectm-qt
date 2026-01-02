#pragma once
// SunoController.hpp - Logic for Suno AI integration
// Coordinates fetching, downloading, and metadata processing

#include <QNetworkAccessManager>
#include <QObject>
#include <filesystem>
#include <memory>
#include "suno/SunoClient.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

namespace fs = std::filesystem;

namespace vc {

class AudioEngine;
class OverlayEngine;

namespace suno {

class SunoController : public QObject {
    Q_OBJECT

public:
    explicit SunoController(AudioEngine* audioEngine,
                            OverlayEngine* overlayEngine,
                            QObject* parent = nullptr);
    ~SunoController() override;

    SunoClient* client() {
        return client_.get();
    }

    // Logic
    void downloadAndPlay(const SunoClip& clip);
    void refreshLibrary();

    // Signal for UI
    Signal<const std::vector<SunoClip>&> libraryUpdated;

public slots:
    void onLibraryFetched(const std::vector<SunoClip>& clips);
    void onError(const std::string& message);

private:
    void downloadAudio(const SunoClip& clip);
    void processDownloadedFile(const SunoClip& clip, const fs::path& path);

    AudioEngine* audioEngine_;
    OverlayEngine* overlayEngine_;
    std::unique_ptr<SunoClient> client_;
    QNetworkAccessManager* networkManager_;

    fs::path downloadDir_;
};

} // namespace suno
} // namespace vc
