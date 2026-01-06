#pragma once
// SunoController.hpp - Logic for Suno AI integration
// Coordinates fetching, downloading, and metadata processing

#include <QNetworkAccessManager>
#include <QObject>
#include <deque>
#include <filesystem>
#include <memory>
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "suno/SunoLyrics.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"

namespace fs = std::filesystem;

namespace vc {

class AudioEngine;
class OverlayEngine;
class MainWindow;

namespace suno {

class SunoController : public QObject {
    Q_OBJECT

public:
    explicit SunoController(AudioEngine* audioEngine,
                            OverlayEngine* overlayEngine,
                            MainWindow* window);
    ~SunoController() override;

    SunoClient* client() {
        return client_.get();
    }

    // Logic
    void downloadAndPlay(const SunoClip& clip);
    void refreshLibrary();
    void syncDatabase(bool forceAuth = false);
    void showCookieDialog();

    // Signal for UI
    Signal<const std::vector<SunoClip>&> libraryUpdated;
    Signal<const std::string&> statusMessage;

public slots:
    void onLibraryFetched(const std::vector<SunoClip>& clips);
    void onAlignedLyricsFetched(const std::string& clipId,
                                const std::string& json);
    void onError(const std::string& message);

private:
    void downloadAudio(const SunoClip& clip);
    void processDownloadedFile(const SunoClip& clip, const fs::path& path);
    void processLyricsQueue();

    AudioEngine* audioEngine_;
    OverlayEngine* overlayEngine_;
    MainWindow* window_;
    std::unique_ptr<SunoClient> client_;
    SunoDatabase db_;
    QNetworkAccessManager* networkManager_;

    fs::path downloadDir_;
    std::deque<std::string> lyricsQueue_;
    int activeLyricsRequests_{0};
};

} // namespace suno
} // namespace vc
