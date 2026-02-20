#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "overlay/OverlayEngine.hpp"
#include "suno/SunoLyrics.hpp"
#include "ui/SunoCookieDialog.hpp"
#include "ui/MainWindow.hpp"
#include "util/FileUtils.hpp"

#include <QTimer>

namespace vc::suno {

SunoController::SunoController(AudioEngine* audioEngine,
                               OverlayEngine* overlayEngine,
                               MainWindow* window)
    : QObject(nullptr)
    , audioEngine_(audioEngine)
    , overlayEngine_(overlayEngine)
    , window_(window)
    , client_(std::make_unique<SunoClient>(nullptr))
    , persistentAuth_(std::make_unique<chadvis::SunoPersistentAuth>(nullptr))
    , systemAuth_(std::make_unique<chadvis::SystemBrowserAuth>(nullptr)) {
    
    fs::path dataDir = file::dataDir();
    file::ensureDir(dataDir);
    fs::path dbPath = dataDir / "suno_library.db";
    db_.init(dbPath.string());
    
    librarySync_ = std::make_unique<SunoLibrarySync>(client_.get(), db_);
    lyricsManager_ = std::make_unique<SunoLyricsManager>(client_.get(), db_, audioEngine_, overlayEngine_);
    playbackManager_ = std::make_unique<SunoPlaybackManager>(client_.get(), audioEngine_);
    
    lyricsManager_->setClipsRef(&librarySync_->clips());
    playbackManager_->setClipsRef(&librarySync_->clips());
    
    connect(persistentAuth_.get(), &chadvis::SunoPersistentAuth::authenticated, this, [this](const auto& authState) {
        LOG_INFO("SunoController: Persistent auth session restored");
        if (!authState.bearerToken.isEmpty()) {
            client_->setToken(authState.bearerToken.toStdString());
            CONFIG.suno().token = authState.bearerToken.toStdString();
        }
        
        QString cookieStr;
        if (!authState.clientCookie.isEmpty()) cookieStr += "__client=" + authState.clientCookie + "; ";
        if (!authState.sessionCookie.isEmpty()) cookieStr += "__session=" + authState.sessionCookie;
        
        if (!cookieStr.isEmpty()) {
            client_->setCookie(cookieStr.toStdString());
            CONFIG.suno().cookie = cookieStr.toStdString();
        }
        CONFIG.save(CONFIG.configPath());
        
        statusMessage.emitSignal("Authentication restored from persistent session");
        
        if (!librarySync_->isSyncing()) {
            librarySync_->startSync(1);
        }
    });

    connect(systemAuth_.get(), &chadvis::SystemBrowserAuth::authSuccess, this, [this](const QString& token) {
        LOG_INFO("SunoController: System auth success");
        if (token.startsWith("eyJ")) {
            client_->setToken(token.toStdString());
            CONFIG.suno().token = token.toStdString();
            CONFIG.save(CONFIG.configPath());
            statusMessage.emitSignal("System authentication successful");
            librarySync_->startSync(1);
        } else {
            LOG_INFO("SunoController: Received token from system auth: {}", token.left(10).toStdString());
        }
    });
    
    connect(systemAuth_.get(), &chadvis::SystemBrowserAuth::authFailed, this, [this](const QString& reason) {
        LOG_ERROR("SunoController: System auth failed: {}", reason.toStdString());
        statusMessage.emitSignal("System Login Failed: " + reason.toStdString());
    });

    persistentAuth_->initialize();

    client_->libraryFetched.connect([this](const auto& clips) { onLibraryFetched(clips); });
    client_->alignedLyricsFetched.connect([this](const auto& id, const auto& json) { onAlignedLyricsFetched(id, json); });
    client_->wavConversionReady.connect([this](const auto& id, const auto& url) { onWavConversionReady(id, url); });
    client_->tokenChanged.connect([this](const auto& token) {
        LOG_INFO("SunoController: Token updated, saving to config");
        CONFIG.suno().token = token;
        CONFIG.save(CONFIG.configPath());
        
        if (isRefreshingToken_) {
            isRefreshingToken_ = false;
            if (!CONFIG.suno().token.empty()) {
                LOG_INFO("SunoController: Resuming lyrics queue after token refresh");
                librarySync_->resumeAfterTokenRefresh();
            }
        }
    });
    client_->errorOccurred.connect([this](const auto& msg) { onError(msg); });

    if (!CONFIG.suno().token.empty()) {
        LOG_INFO("SunoController: Loaded token from config (length: {})", CONFIG.suno().token.length());
        client_->setToken(CONFIG.suno().token);
    }

    if (!CONFIG.suno().cookie.empty()) {
        LOG_INFO("SunoController: Loaded cookie from config (length: {})", CONFIG.suno().cookie.length());
        client_->setCookie(CONFIG.suno().cookie);
    }

    if (client_->isAuthenticated()) {
        QTimer::singleShot(2000, this, [this]() { librarySync_->startSync(1); });
    }
    
    audioEngine_->playlist().currentChanged.connect([this](size_t) { lyricsManager_->onTrackChanged(); });

    if (CONFIG.suno().debugLyrics && !CONFIG.suno().debugLyricsFile.empty()) {
        fs::path p = CONFIG.suno().debugLyricsFile;
        if (fs::exists(p)) {
            LOG_INFO("SunoController: Loading debug lyrics from {}", p.string());
            QFile f(QString::fromStdString(p.string()));
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray data = f.readAll();
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".srt") {
                    auto lyrics = LyricsAligner::parseSrt(data.toStdString());
                    lyrics.songId = "debug-srt-id";
                    lyricsManager_->setDebugLyrics(lyrics);
                } else {
                    auto words = LyricsAligner::parseJson(data);
                    if (!words.empty()) {
                        std::string prompt;
                        for (size_t i = 0; i < words.size(); ++i) {
                            prompt += words[i].word;
                            if ((i + 1) % 5 == 0) prompt += "\n"; 
                        }
                        auto lyrics = LyricsAligner::align(prompt, words);
                        lyrics.songId = "debug-test-id";
                        lyricsManager_->setDebugLyrics(lyrics);
                    }
                }
            }
        }
        lyricsManager_->setDebugMode(true);
    }
}

SunoController::~SunoController() = default;

void SunoController::syncDatabase(bool forceAuth) {
    if (forceAuth) {
        showCookieDialog();
    } else {
        librarySync_->startSync(1);
    }
}

void SunoController::showCookieDialog() {
    auto* dialog = new ui::SunoCookieDialog(window_, persistentAuth_.get());
    
    connect(dialog, &ui::SunoCookieDialog::startSystemAuthRequested, this, [this, dialog]() {
        LOG_INFO("SunoController: Switching to System Browser Auth");
        dialog->close();
        systemAuth_->startAuth();
    });
    
    if (dialog->exec() == QDialog::Accepted) {
        std::string cookie = dialog->getCookie().toStdString();
        if (!cookie.empty()) {
            client_->setCookie(cookie);
            CONFIG.suno().cookie = cookie;
            CONFIG.save(CONFIG.configPath());
            librarySync_->clearClips();
            client_->fetchLibrary(1);
        }
    }
    dialog->deleteLater();
}

void SunoController::onLibraryFetched(const std::vector<SunoClip>& clips) {
    librarySync_->onLibraryFetched(clips);
    libraryUpdated.emitSignal(librarySync_->clips());
}

void SunoController::onAlignedLyricsFetched(const std::string& clipId, const std::string& json) {
    librarySync_->onAlignedLyricsFetched(clipId, json);
    lyricsManager_->onLyricsFetched(clipId, json);
    clipUpdated.emitSignal(clipId);
}

void SunoController::onWavConversionReady(const std::string& clipId, const std::string& wavUrl) {
    playbackManager_->onWavConversionReady(clipId, wavUrl);
}

void SunoController::onError(const std::string& message) {
    librarySync_->onError(message);
    
    if (message.find("Unauthorized") != std::string::npos || message.find("401") != std::string::npos) {
        if (!isRefreshingToken_) {
            isRefreshingToken_ = true;
            librarySync_->pauseForTokenRefresh();
            
            client_->refreshAuthToken([this](bool success) {
                if (!success) {
                    LOG_ERROR("SunoController: Token refresh failed");
                    isRefreshingToken_ = false;
                    CONFIG.suno().token = "";
                    CONFIG.save(CONFIG.configPath());
                    statusMessage.emitSignal("Suno session expired. Please re-authenticate.");
                }
            });
        }
    }
    
    LOG_ERROR("SunoController: {}", message);
    statusMessage.emitSignal(message);
}

} // namespace vc::suno
