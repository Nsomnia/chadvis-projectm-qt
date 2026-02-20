#pragma once
#include "SunoLibrarySync.hpp"
#include "SunoLyricsManager.hpp"
#include "SunoPlaybackManager.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoDatabase.hpp"
#include "ui/SunoPersistentAuth.hpp"
#include "ui/SystemBrowserAuth.hpp"
#include "util/Signal.hpp"
#include <QObject>
#include <memory>

namespace vc {

class AudioEngine;
class OverlayEngine;
class MainWindow;

namespace ui {
    class SunoCookieDialog;
}

namespace suno {

class SunoController : public QObject {
    Q_OBJECT

public:
    explicit SunoController(AudioEngine* audioEngine,
                            OverlayEngine* overlayEngine,
                            MainWindow* window);
    ~SunoController() override;

    SunoClient* client() { return client_.get(); }
    SunoDatabase& db() { return db_; }
    
    void downloadAndPlay(const SunoClip& clip) {
        playbackManager_->downloadAndPlay(clip);
    }
    
    Result<AlignedLyrics> getLyrics(const std::string& clipId) {
        return lyricsManager_->getLyrics(clipId);
    }
    
    void refreshLibrary(int page = 1) { librarySync_->startSync(page); }
    void syncDatabase(bool forceAuth = false);
    void showCookieDialog();
    
    const std::vector<SunoClip>& clips() const { return librarySync_->clips(); }
    
    bool hasLyrics(const std::string& clipId) const {
        return db_.hasLyrics(clipId);
    }
    
    void setDebugLyrics(const AlignedLyrics& lyrics) {
        lyricsManager_->setDebugLyrics(lyrics);
    }

    Signal<const std::vector<SunoClip>&> libraryUpdated;
    Signal<const std::string&> clipUpdated;
    Signal<const std::string&> statusMessage;

public slots:
    void onLibraryFetched(const std::vector<SunoClip>& clips);
    void onAlignedLyricsFetched(const std::string& clipId, const std::string& json);
    void onWavConversionReady(const std::string& clipId, const std::string& wavUrl);
    void onError(const std::string& message);

private:
    AudioEngine* audioEngine_;
    OverlayEngine* overlayEngine_;
    MainWindow* window_;
    
    std::unique_ptr<SunoClient> client_;
    SunoDatabase db_;
    
    std::unique_ptr<SunoLibrarySync> librarySync_;
    std::unique_ptr<SunoLyricsManager> lyricsManager_;
    std::unique_ptr<SunoPlaybackManager> playbackManager_;
    
    std::unique_ptr<chadvis::SunoPersistentAuth> persistentAuth_;
    std::unique_ptr<chadvis::SystemBrowserAuth> systemAuth_;
    
    bool isRefreshingToken_{false};
};

} // namespace vc::suno
} // namespace vc
