#pragma once
// SunoController.hpp - Logic for Suno AI integration
// Coordinates fetching, downloading, and metadata processing

#include <QNetworkAccessManager>
#include <QObject>
#include <deque>
#include <memory>
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "util/Signal.hpp"

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

    Result<AlignedLyrics> getLyrics(const std::string& clipId);

    void refreshLibrary(int page = 1);
    void syncDatabase(bool forceAuth = false);
    void showCookieDialog();

    const std::vector<SunoClip>& clips() const {
        return accumulatedClips_;
    }

    SunoDatabase& db() {
        return db_;
    }

    bool hasLyrics(const std::string& clipId) const {
        return db_.hasLyrics(clipId);
    }

    // Signal for UI
    Signal<const std::vector<SunoClip>&> libraryUpdated;
    Signal<const std::string&> clipUpdated; // id
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
    void onTrackChanged();

    AudioEngine* audioEngine_;
    OverlayEngine* overlayEngine_;
    MainWindow* window_;
    std::unique_ptr<SunoClient> client_;
    SunoDatabase db_;
    QNetworkAccessManager* networkManager_;

    fs::path downloadDir_;
    std::deque<std::string> lyricsQueue_;
    int activeLyricsRequests_{0};
    int currentSyncPage_{1};
    bool isSyncing_{false};
    std::vector<SunoClip> accumulatedClips_;
};

} // namespace suno
} // namespace vc
