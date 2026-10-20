#include "SunoController.hpp"
#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "lyrics/LyricsData.hpp"
#include "lyrics/LyricsSync.hpp"

#include "suno/ClipResolver.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoLyricsManager.hpp"
#include "suno/auth/AuthCoordinator.hpp"
#include "util/FileUtils.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QUuid>

namespace vc::suno {

namespace {

/// Stable per-install Device-Id for the studio-api header set. Generated once
/// and persisted as a plain (non-secret) UUID in config.toml [suno].
QString resolveOrCreateDeviceId() {
    // Const read first: binding the mutable accessor would mark the config
    // dirty on every call, rewriting the file at quit for no change (T0022).
    const auto& cfg = CONFIG.suno();
    if (!cfg.deviceId.empty()) {
        return QString::fromStdString(cfg.deviceId);
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    CONFIG.sunoMut().deviceId = id.toStdString();
    std::ignore = CONFIG.save(CONFIG.configPath());
    LOG_INFO("SunoController: generated new Device-Id {}", id.toStdString());
    return id;
}

} // namespace

SunoController::SunoController(AudioEngine* audioEngine, LyricsSync* lyricsSync, QObject* parent)
    : QObject(parent), audioEngine_(audioEngine), lyricsSync_(lyricsSync),
      client_(std::make_unique<SunoClient>(resolveOrCreateDeviceId())),
      // The controller is created on the GUI thread; parent the coordinator
      // there so its loopback/QTimer children share that thread and lifetime.
      authCoordinator_(std::make_unique<auth::AuthCoordinator>(client_.get(), this)) {
    // Initialize Database
    fs::path dataDir = file::dataDir();
    (void)file::ensureDir(dataDir);
    fs::path dbPath = dataDir / "suno_library.db";
    if (auto result = db_.init(dbPath.string()); !result) {
        LOG_ERROR("SunoController: Failed to initialize Suno database: {}", result.error().message);
    }

    // Initialize Managers
    accountManager_ = std::make_unique<SunoAccountManager>(client_.get(), this);
    libraryManager_ = std::make_unique<SunoLibraryManager>(client_.get(), db_, this);

    auto networkManager =
            new QNetworkAccessManager(this); // Owned by SunoController (or QObject tree)
    downloader_ = std::make_unique<SunoDownloader>(client_.get(), db_, audioEngine_, networkManager,
                                                   this);

    // Forward queue progress so bridges/QML can consume it later.
    connect(downloader_.get(), &SunoDownloader::downloadStateChanged, this,
            &SunoController::downloadStateChanged);
    connect(downloader_.get(), &SunoDownloader::downloadQueueIdle, this,
            &SunoController::downloadQueueIdle);
    connect(downloader_.get(), &SunoDownloader::playbackReady, this, [this](const QString& clipId) {
        activeClipId_ = clipId.toStdString();
        activateClipLyrics(activeClipId_);
    });

    lyricsManager_ = std::make_unique<SunoLyricsManager>(client_.get(), db_, this);

    orchestrator_ = std::make_unique<SunoOrchestrator>(client_.get(), this);
    connect(orchestrator_.get(), &SunoOrchestrator::messageReceived, this,
            &SunoController::chatMessageReceived);
    connect(orchestrator_.get(), &SunoOrchestrator::historyFetched, this,
            &SunoController::chatHistoryFetched);
    connect(orchestrator_.get(), &SunoOrchestrator::errorOccurred, this,
            &SunoController::chatError);

    // --- Connect Signals ---

    // Auth state (client runs restore/migrate in its constructor)
    //
    // The import wait is tracked here, not in the coordinator: the
    // coordinator's state stays "browserImported" after a successful
    // verification (the bridge synthesises "authenticated" from the
    // client), so the coordinator alone cannot tell a verification that
    // is still pending from one that succeeded an hour ago. This flag
    // is set by the coordinator's own state transitions and cleared by
    // the client's verdict — ActiveValid (verified) or Disconnected
    // (abandoned) — so a later NeedsReauth from an expired bearer never
    // masquerades as a failed import.
    connect(authCoordinator_.get(), &auth::AuthCoordinator::stateChanged, this,
            [this](const QString& state) {
                importVerificationPending_ = (state == QStringLiteral("browserImported"));
            });
    connect(client_.get(), &SunoClient::needsReauth, this, [this]() {
        // A verification that fails while an import is waiting must land
        // on the card: without this the card keeps saying "Imported.
        // Verifying" for a verification that already failed, because the
        // coordinator never hears about SunoClient's verdict.
        if (importVerificationPending_ && authCoordinator_) {
            authCoordinator_->noteVerificationFailed();
        }
        emit authenticationRequired();
    });
    connect(client_.get(), &SunoClient::authFailureKindChanged, this,
            &SunoController::authFailureKindChanged);
    connect(client_.get(), &SunoClient::authStateChanged, this, [this]() {
        switch (client_->authState()) {
            case auth::AuthState::ActiveValid:
                emit statusMessage("Suno authentication active");
                emit authenticationSuccess();
                // The import's verification succeeded: the wait is over,
                // and a later NeedsReauth must not be mistaken for a
                // failed import (see the pending-flag note above).
                importVerificationPending_ = false;
                // Session catalog + billing bootstrap (once per activation).
                if (accountManager_) {
                    accountManager_->refreshAll();
                }
                break;
            case auth::AuthState::NeedsReauth:
                if (accountManager_) {
                    accountManager_->clearSnapshots();
                }
                break;
            case auth::AuthState::Disconnected:
                // A disconnect while an import is waiting (a sign-out
                // landing here from another path) abandons the wait
                // instead of stranding it on "Imported. Verifying".
                importVerificationPending_ = false;
                if (authCoordinator_ &&
                    authCoordinator_->googleLoginState() == QStringLiteral("browserImported")) {
                    authCoordinator_->cancelGoogleSignIn();
                }
                if (accountManager_) {
                    accountManager_->clearSnapshots();
                }
                break;
        }
    });

    // Cheap billing refresh after a generation kicks off (credits change as
    // the clip is submitted); delayed so the backend has settled its ledger.
    // generationStarted is a custom Signal<> (not Qt), so use .connect().
    client_->generationStarted.connect([this](const std::vector<SunoClip>&) {
        if (!accountManager_) return;
        QTimer::singleShot(std::chrono::seconds(15), this,
                           [this]() { accountManager_->refreshBilling(); });
    });

    // Library Manager
    connect(libraryManager_.get(), &SunoLibraryManager::statusMessage, this,
            &SunoController::statusMessage);
    connect(libraryManager_.get(), &SunoLibraryManager::libraryUpdated, this,
            [this](const std::vector<SunoClip>& clips) {
                emit libraryUpdated(clips);

                // Lyrics are requested for the NEW slice only: libraryUpdated
                // carries the whole accumulated list after every page, and the
                // list is append-ordered during a sync, so scanning from the
                // previously seen size issues exactly one pass per clip — the
                // old whole-list scan re-issued every prior page's queries on
                // every page (k×20 getAlignedLyrics calls by page k). A smaller
                // list means a fresh sync cleared it: rescan from zero.
                const std::size_t firstNew =
                        clips.size() < lastLyricsScanSize_ ? std::size_t{0} : lastLyricsScanSize_;
                lastLyricsScanSize_ = clips.size();
                for (std::size_t i = firstNew; i < clips.size(); ++i) {
                    const auto& clip = clips[i];
                    auto lyricsRes = db_.getAlignedLyrics(clip.id);
                    if (lyricsRes.isErr() || lyricsRes.value().empty()) {
                        lyricsManager_->queueLyricsFetch(clip.id);
                    }
                }
            });
    connect(libraryManager_.get(), &SunoLibraryManager::authenticationRequired, this,
            &SunoController::authenticationRequired);
    connect(libraryManager_.get(), &SunoLibraryManager::libraryFetchFailed, this,
            &SunoController::libraryFetchFailed);

    // Forward SunoClient custom errorOccurred to a Qt signal so Bridges
    // can clear spinners on any terminal network/auth failure.
    client_->errorOccurred.connect([this](const std::string& err) {
        // ClerkAuthClient's AuthFailureKind is not exposed by the
        // public SunoClient API.  SunoClient has already sanitized the
        // failure text, so preserve that reason instead of relabeling
        // every NeedsReauth transition as an expired session.
        const bool authFailure = client_->authState() == auth::AuthState::NeedsReauth;
        // Emit on the Qt thread; queued to avoid re-entrancy with managers.
        //
        // T0036: this used to ALSO emit libraryFetchFailed. But
        // errorOccurred is the global broadcast -- it fires for a failed
        // single-lyrics request, a failed upload, anything -- so every
        // unrelated failure told the library view its fetch had died and
        // cleared its spinner, leaving the library permanently "empty"
        // with no error of its own. SunoLibraryManager already emits a
        // properly scoped libraryFetchFailed for genuine library failures
        // (SunoLibraryManager.cpp:27,38,54,98), and the Bridge already
        // connects that directly at SunoBridge.cpp:388, so the fan-out
        // bought nothing and cost correctness. Only sunoError and the
        // auth transition belong on a global signal.
        QMetaObject::invokeMethod(
                this,
                [this, qmsg = QString::fromStdString(err), authFailure]() {
                    emit sunoError(qmsg);
                    if (authFailure) {
                        emit authenticationFailed(qmsg);
                    }
                },
                Qt::QueuedConnection);
    });

    // Lyrics Manager
    connect(lyricsManager_.get(), &SunoLyricsManager::statusMessage, this,
            &SunoController::statusMessage);
    connect(lyricsManager_.get(), &SunoLyricsManager::lyricsFetched, this,
            [this](const std::string& id, const std::string& json) {
                QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
                const auto lyrics = parseLyricsForClip(id, json);

                if (auto result = db_.saveAlignedLyrics(id, json); !result) {
                    LOG_WARN("SunoController: Failed to persist aligned lyrics for {}: {}", id,
                             result.error().message);
                }
                emit clipUpdated(id);

                if (lyrics) {
                    directLyricsCache_.try_emplace(id, *lyrics);
                    if (id == activeClipId_ && !CONFIG.suno().debugLyrics) {
                        publishLyrics(id, *lyrics);
                        LOG_INFO(
                                "SunoController: Immediately displayed lyrics for current track {}",
                                id);
                    }
                }

                if (CONFIG.suno().saveLyrics) {
                    downloader_->saveLyricsSidecar(id, json, doc,
                                                   libraryManager_->accumulatedClips());
                }
            });

    // Connect to track changes. The payload is an optional index: a nullopt
    // emission means the selected track was removed or the queue was cleared, and
    // dropping the lyrics is the correct response to that too.
    audioEngine_->playlist().currentChanged.connect(
            [this](std::optional<size_t>) { onTrackChanged(); });

    // Initial Library Refresh if authenticated
    if (client_->isAuthenticated()) {
        accountManager_->refreshAll();
        QTimer::singleShot(2000, this, [this]() { refreshLibrary(1); });
    }

    // Handle Debug Lyrics
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
                    setDebugLyrics(lyrics);
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
                        setDebugLyrics(lyrics);
                    }
                }
            }
        }
    }
}

SunoController::~SunoController() = default;

void SunoController::downloadAndPlay(const SunoClip& clip) { downloader_->downloadAndPlay(clip); }

bool SunoController::playClipById(const std::string& clipId) {
    if (clipId.empty()) {
        return false;
    }

    auto clip = resolveClip(libraryManager_->accumulatedClips(), db_, clipId);
    if (!clip) {
        return false;
    }

    if (!SunoDownloader::selectDownloadUrl(*clip, CONFIG.suno().downloadFormat)) {
        return false;
    }

    downloader_->downloadAndPlay(*clip);
    return true;
}

Result<AlignedLyrics> SunoController::getLyrics(const std::string& clipId) {
    const auto jsonRes = db_.getAlignedLyrics(clipId);
    if (jsonRes.isErr() || jsonRes.value().empty()) {
        return Result<AlignedLyrics>::err("No aligned lyrics found");
    }

    const auto lyrics = parseLyricsForClip(clipId, jsonRes.value());
    if (!lyrics) {
        return Result<AlignedLyrics>::err("Invalid aligned lyrics");
    }

    return Result<AlignedLyrics>::ok(AlignedLyrics::fromLyricsData(*lyrics));
}

void SunoController::refreshLibrary(int page) { libraryManager_->refreshLibrary(page); }

void SunoController::syncDatabase(bool forceAuth) { libraryManager_->syncDatabase(forceAuth); }

void SunoController::requestAuthentication() {
    // No system-browser flow anymore: surface the requirement to QML, which
    // points users at the settings panel to paste fresh credentials.
    emit authenticationRequired();
}

void SunoController::refreshAccount() { client_->reloadStoredCredentials(); }

void SunoController::sendChatMessage(const QString& message, const QString& workspaceId) {
    if (orchestrator_) orchestrator_->sendMessage(message, workspaceId);
}

void SunoController::fetchChatHistory() {
    if (orchestrator_) orchestrator_->fetchHistory();
}

const std::vector<SunoClip>& SunoController::clips() const {
    return libraryManager_->accumulatedClips();
}

void SunoController::setDebugLyrics(const AlignedLyrics& lyrics) {
    if (lyricsSync_) {
        lyricsSync_->loadLyrics(lyrics.toLyricsData());
    }
}

void SunoController::onTrackChanged() {
    if (CONFIG.suno().debugLyrics) {
        return;
    }

    activeClipId_.clear();
    if (lyricsSync_) {
        lyricsSync_->clear();
    }
}

void SunoController::activateClipLyrics(const std::string& clipId) {
    if (CONFIG.suno().debugLyrics) {
        return;
    }
    if (clipId.empty() || clipId != activeClipId_) {
        return;
    }

    const auto cacheIt = directLyricsCache_.find(clipId);
    if (cacheIt != directLyricsCache_.end()) {
        if (lyricsSync_) {
            lyricsSync_->loadLyrics(cacheIt->second);
        }
        return;
    }

    const auto clip = resolveClip(libraryManager_->accumulatedClips(), db_, clipId);
    if (!clip) {
        return;
    }

    const auto jsonRes = db_.getAlignedLyrics(clipId);
    if (jsonRes.isOk() && !jsonRes.value().empty()) {
        if (const auto data = parseLyricsForClip(clipId, jsonRes.value())) {
            publishLyrics(clipId, *data);
            return;
        }
    }

    const auto item = audioEngine_->playlist().currentItem();
    if (item && !item->isRemote) {
        const auto directory = item->path.parent_path();
        const auto stem = item->path.stem().string();

        const auto srt = file::readText(directory / (stem + ".srt"));
        if (srt.isOk() && !srt.value().empty()) {
            auto data = LyricsFactory::fromSrt(srt.value());
            if (!data.empty()) {
                data.songId = clipId;
                data.title = clip->title;
                data.artist = clip->display_name;
                publishLyrics(clipId, std::move(data));
                return;
            }
        }

        const auto json = file::readText(directory / (stem + ".json"));
        if (json.isOk() && !json.value().empty()) {
            if (const auto data = LyricsAligner::parseCapturedSunoLyrics(json.value(), *clip)) {
                publishLyrics(clipId, *data);
                return;
            }
        }
    }

    if (client_->isAuthenticated()) {
        lyricsManager_->queueLyricsFetch(clipId);
    }
}

void SunoController::publishLyrics(const std::string& clipId, LyricsData lyrics) {
    if (CONFIG.suno().debugLyrics || clipId.empty() || clipId != activeClipId_ || lyrics.empty()) {
        return;
    }

    auto it = directLyricsCache_.insert_or_assign(clipId, std::move(lyrics)).first;
    if (lyricsSync_) {
        lyricsSync_->loadLyrics(it->second);
    }
}

std::optional<LyricsData> SunoController::parseLyricsForClip(const std::string& clipId,
                                                             const std::string& json) {
    auto clip = resolveClip(libraryManager_->accumulatedClips(), db_, clipId);
    if (!clip) {
        clip = SunoClip{};
        clip->id = clipId;
    }
    return LyricsAligner::parseCapturedSunoLyrics(json, *clip);
}

} // namespace vc::suno
