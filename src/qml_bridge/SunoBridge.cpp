#include "SunoBridge.hpp"
#include "core/Config.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoClient.hpp"
#include "suno/auth/AuthCoordinator.hpp"
#include "suno/DownloadQueue.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoNotificationService.hpp"
#include "suno/SunoAudioUploadService.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoModels.hpp"
#include <QStringList>
#include <QVariantMap>

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
vc::suno::SunoClient* SunoBridge::s_client = nullptr;

SunoBridge::~SunoBridge() {
    destroyAudioUploadService();
    delete notificationService_;
    notificationService_ = nullptr;
    delete exploreService_;
    exploreService_ = nullptr;
}

namespace {

QString authFailureKindString(vc::suno::auth::AuthFailureKind kind) {
    using Kind = vc::suno::auth::AuthFailureKind;
    switch (kind) {
    case Kind::None:
        return QStringLiteral("none");
    case Kind::NoActiveSession:
        return QStringLiteral("noSession");
    case Kind::RejectedCredential:
        return QStringLiteral("rejected");
    case Kind::ProtocolMismatch:
        return QStringLiteral("protocol");
    case Kind::MalformedResponse:
        return QStringLiteral("malformed");
    }
    return QStringLiteral("none");
}

QString synthesizedAuthFailureError(
        vc::suno::auth::AuthFailureKind kind) {
    using Kind = vc::suno::auth::AuthFailureKind;
    switch (kind) {
    case Kind::NoActiveSession:
        return QStringLiteral("No active Suno session was found. Paste a valid bearer token or the complete Clerk cookie header in Account settings.");
    case Kind::RejectedCredential:
        return QStringLiteral("Suno rejected the stored credential. Paste a fresh bearer token or complete Clerk cookie header in Account settings.");
    case Kind::ProtocolMismatch:
        return QStringLiteral("Suno sign-in hit an unexpected authentication response. Try again; if it persists, refresh the captured sign-in flow.");
    case Kind::MalformedResponse:
        return QStringLiteral("Suno sign-in returned an unreadable authentication response. Try again; if it persists, refresh the captured sign-in flow.");
    case Kind::None:
        break;
    }
    return {};
}

QString generationUnavailableMessage() {
    return QStringLiteral(
            "Generation is unavailable because the required Suno CAPTCHA token flow is not supported. No request was sent.");
}

QVariantMap toDiscoverClip(const vc::suno::SunoClip& clip) {
    QVariantMap result;
    result[QStringLiteral("id")] = QString::fromStdString(clip.id);
    result[QStringLiteral("title")] = QString::fromStdString(clip.title);
    result[QStringLiteral("status")] = QString::fromStdString(clip.status);
    result[QStringLiteral("image_url")] = QString::fromStdString(
            clip.image_large_url.empty() ? clip.image_url : clip.image_large_url);
    result[QStringLiteral("creator")] = QString::fromStdString(
            clip.display_name.empty() ? clip.handle : clip.display_name);
    result[QStringLiteral("created_at")] = QString::fromStdString(clip.created_at);
    result[QStringLiteral("duration")] = QString::fromStdString(clip.metadata.duration);
    return result;
}

QVariantMap toNotificationVariant(const vc::suno::SunoNotificationService::Notification& notification) {
    QVariantMap author;
    author[QStringLiteral("user_id")] = notification.author.userId;
    author[QStringLiteral("display_name")] = notification.author.displayName;
    author[QStringLiteral("handle")] = notification.author.handle;

    QVariantMap content;
    content[QStringLiteral("id")] = notification.contentId;
    content[QStringLiteral("ancillary_id")] = notification.contentAncillaryId;
    content[QStringLiteral("type")] = notification.contentType;
    content[QStringLiteral("title")] = notification.contentTitle;
    content[QStringLiteral("message")] = notification.contentMessage;

    QVariantMap result;
    result[QStringLiteral("id")] = notification.id;
    result[QStringLiteral("type")] = notification.type;
    result[QStringLiteral("caption")] = notification.caption;
    result[QStringLiteral("created_at")] = notification.createdAt;
    result[QStringLiteral("updated_at")] = notification.createdAt;
    result[QStringLiteral("is_read")] = notification.read;
    result[QStringLiteral("read")] = notification.read;
    result[QStringLiteral("author")] = author;
    result[QStringLiteral("author_name")] = notification.author.displayName;
    result[QStringLiteral("author_handle")] = notification.author.handle;
    result[QStringLiteral("user_profiles")] = QVariantList{author};
    result[QStringLiteral("content")] = content;
    result[QStringLiteral("content_id")] = notification.contentId;
    result[QStringLiteral("content_ancillary_id")] = notification.contentAncillaryId;
    result[QStringLiteral("content_type")] = notification.contentType;
    result[QStringLiteral("content_title")] = notification.contentTitle;
    result[QStringLiteral("content_message")] = notification.contentMessage;
    result[QStringLiteral("priority")] = notification.priority;
    result[QStringLiteral("total_users")] = notification.totalUsers;
    return result;
}

QVariantList toQVariantModels(const QList<vc::suno::SunoModelInfo>& models) {
    QVariantList result;
    result.reserve(models.size());
    for (const auto& model : models) {
        QStringList capabilities;
        capabilities.reserve(static_cast<int>(model.capabilities.size()));
        for (const auto& capability : model.capabilities) {
            capabilities.push_back(QString::fromStdString(capability));
        }

        QVariantMap limits;
        limits[QStringLiteral("title")] = static_cast<qlonglong>(model.max_lengths.title);
        limits[QStringLiteral("prompt")] = static_cast<qlonglong>(model.max_lengths.prompt);
        limits[QStringLiteral("tags")] = static_cast<qlonglong>(model.max_lengths.tags);
        limits[QStringLiteral("negative_tags")] = static_cast<qlonglong>(model.max_lengths.negative_tags);
        limits[QStringLiteral("gpt_description_prompt")] =
                static_cast<qlonglong>(model.max_lengths.gpt_description_prompt);

        QVariantMap entry;
        entry[QStringLiteral("name")] = QString::fromStdString(model.name);
        entry[QStringLiteral("external_key")] = QString::fromStdString(model.external_key);
        entry[QStringLiteral("description")] = QString::fromStdString(model.description);
        entry[QStringLiteral("capabilities")] = capabilities;
        entry[QStringLiteral("limits")] = limits;
        entry[QStringLiteral("can_use")] = model.can_use;
        entry[QStringLiteral("is_default_model")] = model.is_default_model;
        entry[QStringLiteral("is_default_free_model")] = model.is_default_free_model;
        result.push_back(entry);
    }
    return result;
}

} // namespace

SunoBridge::SunoBridge(QObject* parent) : QObject(parent) {
    setInstance(this);
    generationStatus_ = generationUnavailableMessage();
    if (s_controller) {
        wireControllerSignals();
    }

    searchDebounce_.setSingleShot(true);
    searchDebounce_.setInterval(350);
    connect(&searchDebounce_, &QTimer::timeout, this, [this]() {
        setFilterText(searchDebounceText_);
    });

    loadingWatchdog_.setSingleShot(true);
    loadingWatchdog_.setInterval(15000);
    connect(&loadingWatchdog_, &QTimer::timeout, this, [this]() {
        if (loading_) {
            clearLoading();
        }
    });
}

void SunoBridge::setSunoController(vc::suno::SunoController* controller) {
    auto* bridgeInstance = instance();
    if (bridgeInstance && bridgeInstance->controllerSignalsWired_) {
        auto* previousController = bridgeInstance->s_controller;
        if (previousController && previousController != controller) {
            bridgeInstance->destroyAudioUploadService();
            bridgeInstance->destroyNotificationService();
            bridgeInstance->destroyExploreService();
            QObject::disconnect(previousController, nullptr, bridgeInstance, nullptr);
            if (auto* lm = previousController->libraryManager()) {
                QObject::disconnect(lm, nullptr, bridgeInstance, nullptr);
            }
            if (auto* oldClient = bridgeInstance->s_client) {
                QObject::disconnect(oldClient, nullptr, bridgeInstance, nullptr);
                oldClient->errorOccurred.disconnect(bridgeInstance->clientErrorConnectionId_);
            }
            if (auto* am = previousController->accountManager()) {
                QObject::disconnect(am, nullptr, bridgeInstance, nullptr);
            }
            if (auto* coordinator = previousController->authCoordinator()) {
                QObject::disconnect(coordinator, nullptr, bridgeInstance, nullptr);
            }
            bridgeInstance->controllerSignalsWired_ = false;
        }
    }

    s_controller = controller;
    s_client = s_controller ? s_controller->client() : nullptr;
    if (bridgeInstance) {
        if (s_controller) {
            bridgeInstance->wireControllerSignals();
        } else {
            bridgeInstance->destroyAudioUploadService();
            bridgeInstance->destroyNotificationService();
            bridgeInstance->destroyExploreService();
            bridgeInstance->updateModelCatalog();
            emit bridgeInstance->authenticationChanged();
            emit bridgeInstance->googleLoginStateChanged();
            emit bridgeInstance->googleLoginErrorChanged();
            emit bridgeInstance->authFailureKindChanged();
            emit bridgeInstance->googleLoginAvailableChanged();
        }
    }
}

void SunoBridge::wireControllerSignals() {
    if (controllerSignalsWired_ || !s_controller) {
        return;
    }

    s_client = s_controller->client();
    ensureNotificationService();
    ensureExploreService();
    ensureAudioUploadService();
    auto* bridgeInstance = this;

    connect(s_controller, &vc::suno::SunoController::libraryUpdated,
            bridgeInstance, &SunoBridge::onLibraryUpdated);
    connect(s_controller, &vc::suno::SunoController::authenticationRequired,
            bridgeInstance, &SunoBridge::clearLoading);
    connect(s_controller, &vc::suno::SunoController::authenticationFailed,
            bridgeInstance, &SunoBridge::onAuthenticationFailed);
    connect(s_controller, &vc::suno::SunoController::authFailureKindChanged,
            bridgeInstance, [bridgeInstance]() {
                emit bridgeInstance->authFailureKindChanged();
                emit bridgeInstance->googleLoginErrorChanged();
            });
    connect(s_controller, &vc::suno::SunoController::libraryFetchFailed,
            bridgeInstance, &SunoBridge::onLibraryFetchFailed);
    connect(s_controller, &vc::suno::SunoController::sunoError,
            bridgeInstance, &SunoBridge::onLibraryFetchFailed);
    connect(s_controller, &vc::suno::SunoController::statusMessage,
            bridgeInstance, [bridgeInstance](const std::string& message) {
                bridgeInstance->setStatusMessage(QString::fromStdString(message));
            });
    connect(s_controller, &vc::suno::SunoController::downloadStateChanged,
            bridgeInstance, [bridgeInstance](const QString& clipId, int state, int percent) {
                if (!bridgeInstance->activeDownloadClipId_.isEmpty() &&
                    clipId != bridgeInstance->activeDownloadClipId_) {
                    return;
                }
                const QString title = bridgeInstance->clipTitle(clipId);
                switch (static_cast<vc::suno::DownloadState>(state)) {
                case vc::suno::DownloadState::Queued:
                    bridgeInstance->setDownloadStatusForClip(clipId, QStringLiteral("Queued for download"));
                    break;
                case vc::suno::DownloadState::Downloading:
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, QStringLiteral("Downloading %1… %2%").arg(title).arg(qMax(0, percent)));
                    break;
                case vc::suno::DownloadState::Completed:
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, QStringLiteral("Downloaded %1; now playing").arg(title));
                    if (bridgeInstance->activeDownloadClipId_ == clipId) {
                        bridgeInstance->activeDownloadClipId_.clear();
                    }
                    break;
                case vc::suno::DownloadState::FailedRetryable:
                case vc::suno::DownloadState::FailedPermanent:
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, QStringLiteral("Download failed for %1").arg(title));
                    if (bridgeInstance->activeDownloadClipId_ == clipId) {
                        bridgeInstance->activeDownloadClipId_.clear();
                    }
                    break;
                case vc::suno::DownloadState::Cancelled:
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, QStringLiteral("Download cancelled for %1").arg(title));
                    if (bridgeInstance->activeDownloadClipId_ == clipId) {
                        bridgeInstance->activeDownloadClipId_.clear();
                    }
                    break;
                }
            });

    if (auto* lm = s_controller->libraryManager()) {
        connect(lm, &vc::suno::SunoLibraryManager::authenticationRequired,
                bridgeInstance, &SunoBridge::clearLoading);
        connect(lm, &vc::suno::SunoLibraryManager::libraryFetchFailed,
                bridgeInstance, &SunoBridge::onLibraryFetchFailed);
    }

    if (s_client) {
        connect(s_client, &vc::suno::SunoClient::needsReauth,
                bridgeInstance, &SunoBridge::clearLoading);
        connect(s_client, &vc::suno::SunoClient::authStateChanged,
                bridgeInstance, [bridgeInstance]() {
                    if (bridgeInstance->s_client->isAuthenticated()) {
                        bridgeInstance->setErrorMessage({});
                    }
                    emit bridgeInstance->authenticationChanged();
                });
        // Custom Signal<> for generic errors: queued clear so it
        // arrives on the bridge's thread even if emitted off-thread.
        clientErrorConnectionId_ = s_client->errorOccurred.connect([bridgeInstance](const std::string& err) {
            QMetaObject::invokeMethod(bridgeInstance,
                [bridgeInstance, err]() { bridgeInstance->onLibraryFetchFailed(QString::fromStdString(err)); },
                Qt::QueuedConnection);
        });
    }

    if (auto* coordinator = s_controller->authCoordinator()) {
        // AuthCoordinator owns the canonical state strings.  The bridge
        // mirrors its signals without introducing a second auth state machine.
        connect(coordinator, &vc::suno::auth::AuthCoordinator::stateChanged,
                bridgeInstance, [bridgeInstance](const QString&) {
                    emit bridgeInstance->googleLoginStateChanged();
                });
        connect(coordinator, &vc::suno::auth::AuthCoordinator::errorChanged,
                bridgeInstance, [bridgeInstance](const QString&) {
                    emit bridgeInstance->googleLoginErrorChanged();
                });
        connect(coordinator, &vc::suno::auth::AuthCoordinator::callbackReceived,
                bridgeInstance, &SunoBridge::googleLoginCallbackReceived);
    }

    // Account snapshot -> QML properties.
    if (auto* am = s_controller->accountManager()) {
        connect(am, &vc::suno::SunoAccountManager::billingInfoReady,
                bridgeInstance, &SunoBridge::billingInfoChanged);
        connect(am, &vc::suno::SunoAccountManager::accountInfoReady,
                bridgeInstance, [bridgeInstance]() {
                    bridgeInstance->updateModelCatalog();
                    emit bridgeInstance->accountInfoChanged();
                });
        connect(am, &vc::suno::SunoAccountManager::accountError,
                bridgeInstance, [bridgeInstance](const QString& message) {
                    bridgeInstance->setErrorMessage(message);
                });
    }
    connect(s_controller, &vc::suno::SunoController::chatMessageReceived, bridgeInstance, [bridge = bridgeInstance](const QString& response, const QString& workspaceId) {
        QVariantMap assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = response;
        assistantMsg["workspaceId"] = workspaceId;
        bridge->chatHistory_.append(assistantMsg);
        emit bridge->chatHistoryChanged();
    });

    connect(s_controller, &vc::suno::SunoController::chatHistoryFetched, bridgeInstance, [bridge = bridgeInstance](const QVariantList& sessions) {
        bridge->chatHistory_ = sessions;
        emit bridge->chatHistoryChanged();
    });

    controllerSignalsWired_ = true;

    updateModelCatalog();
    emit authenticationChanged();
    emit googleLoginStateChanged();
    emit googleLoginErrorChanged();
    emit authFailureKindChanged();
    emit googleLoginAvailableChanged();
}

bool SunoBridge::loading() const { return loading_; }

QVariantList SunoBridge::clips() const { return clips_; }

int SunoBridge::totalClips() const {
  return allClips_.size();
}

bool SunoBridge::hasMorePages() const {
  return hasMorePages_;
}

int SunoBridge::currentPage() const {
  return currentPage_;
}

QVariantList SunoBridge::chatHistory() const { return chatHistory_; }

QVariantList SunoBridge::models() const { return modelCatalog_; }

bool SunoBridge::generationAvailable() const { return false; }

QString SunoBridge::generationStatus() const { return generationStatus_; }

bool SunoBridge::audioUploadBusy() const {
    return audioUploadService_ && audioUploadService_->isUploading();
}

int SunoBridge::audioUploadProgress() const {
    return audioUploadService_ ? audioUploadService_->progress() : 0;
}

QString SunoBridge::audioUploadStatus() const {
    return audioUploadService_ ? audioUploadService_->status() : QString();
}

QString SunoBridge::audioUploadError() const {
    return audioUploadService_ ? audioUploadService_->error() : QString();
}

QString SunoBridge::statusMessage() const { return statusMessage_; }

QString SunoBridge::errorMessage() const { return errorMessage_; }

QString SunoBridge::downloadStatus() const { return downloadStatus_; }

QVariantList SunoBridge::discover() const { return discover_; }

bool SunoBridge::discoverLoading() const { return discoverLoading_; }

bool SunoBridge::discoverHasMore() const { return discoverHasMore_; }

bool SunoBridge::discoverLoaded() const { return discoverLoaded_; }

QString SunoBridge::discoverError() const { return discoverError_; }

QVariantList SunoBridge::notifications() const { return notifications_; }

int SunoBridge::unreadCount() const { return unreadCount_; }

bool SunoBridge::notificationsLoading() const { return notificationsLoading_; }

QString SunoBridge::notificationsError() const { return notificationsError_; }

void SunoBridge::setFilterText(const QString& filter) {
    if (filterText_ == filter) return;
    filterText_ = filter;
    updateFilteredClips();
    emit filterTextChanged();
}

void SunoBridge::generate(const QString& prompt, const QString& tags,
                          bool instrumental, const QString& model) {
    Q_UNUSED(prompt)
    Q_UNUSED(tags)
    Q_UNUSED(instrumental)
    Q_UNUSED(model)
    const QString message = generationUnavailableMessage();
    if (generationStatus_ != message) {
        generationStatus_ = message;
        emit generationStatusChanged();
    }
}

void SunoBridge::uploadAudio(const QString& localFilePath) {
    ensureAudioUploadService();
    if (!audioUploadService_) {
        setErrorMessage(QStringLiteral("Audio upload is unavailable."));
        return;
    }
    audioUploadService_->start(localFilePath);
}

void SunoBridge::refreshLibrary(int page) {
    if (s_controller) {
        setErrorMessage({});
        setStatusMessage(QStringLiteral("Refreshing library"));
        loading_ = true;
        emit loadingChanged();
        startLoadingWatchdog();
        s_controller->refreshLibrary(page);
    }
}

void SunoBridge::requestNextLibraryPage() {
    if (!s_controller || loading_) {
        return;
    }
    auto* lm = s_controller->libraryManager();
    if (!lm->hasMorePages()) {
        return;
    }
    setErrorMessage({});
    loading_ = true;
    emit loadingChanged();
    startLoadingWatchdog();
    lm->requestNextPage();
}

void SunoBridge::searchLibrary(const QString& searchText) {
    searchDebounceText_ = searchText;
    searchDebounce_.start();
}

void SunoBridge::playClip(const QString& clipId) {
    if (clipId.isEmpty() || !s_controller) {
        setErrorMessage(QStringLiteral("This clip has no playable media yet."));
        return;
    }

    setErrorMessage({});
    activeDownloadClipId_ = clipId;
    const QString title = clipTitle(clipId);
    setStatusMessage(QStringLiteral("Preparing %1 for playback").arg(title));
    setDownloadStatusForClip(clipId,
                             QStringLiteral("Preparing %1 for playback").arg(title));
    if (!s_controller->playClipById(clipId.toStdString())) {
        activeDownloadClipId_.clear();
        setErrorMessage(QStringLiteral("This clip has no playable media yet."));
    }
}

int SunoBridge::credits() const {
    if (!s_controller || !s_controller->accountManager()) return 0;
    const auto& billing = s_controller->accountManager()->billing();
    return billing ? static_cast<int>(billing->credits) : 0;
}

QString SunoBridge::planName() const {
    if (!s_controller || !s_controller->accountManager()) return {};
    const auto& billing = s_controller->accountManager()->billing();
    return billing ? QString::fromStdString(billing->plan.name) : QString();
}

QString SunoBridge::userName() const {
    if (!s_controller || !s_controller->accountManager()) return {};
    const auto& user = s_controller->accountManager()->user();
    if (!user) return {};
    return QString::fromStdString(!user->display_name.empty() ? user->display_name
                                                              : user->username);
}

void SunoBridge::sendChatMessage(const QString& message, const QString& workspaceId) {
    Q_UNUSED(message)
    Q_UNUSED(workspaceId)
    setErrorMessage(QStringLiteral("B-Side Chat is unavailable because its endpoints are not capture-backed."));
}

void SunoBridge::fetchChatHistory() {
    setErrorMessage(QStringLiteral("B-Side Chat is unavailable because its endpoints are not capture-backed."));
}

void SunoBridge::onLibraryUpdated() {
  if (!s_controller) return;

  allClips_.clear();
  const auto& clips = s_controller->clips();
  for (const auto& clip : clips) {
    QVariantMap map;
    map["id"] = QString::fromStdString(clip.id);
    map["title"] = QString::fromStdString(clip.title);
    map["status"] = QString::fromStdString(clip.status);
    map["image_url"] = QString::fromStdString(
            !clip.image_large_url.empty() ? clip.image_large_url : clip.image_url);
    map["audio_url"] = QString::fromStdString(clip.audio_url);
    map["model_name"] = QString::fromStdString(clip.model_name);
    map["major_model_version"] = QString::fromStdString(clip.major_model_version);
    map["created_at"] = QString::fromStdString(clip.created_at);
    map["play_count"] = static_cast<int>(clip.play_count);
    map["duration"] = QString::fromStdString(clip.metadata.duration);
    map["has_media"] = vc::suno::SunoDownloader::selectDownloadUrl(
                               clip, CONFIG.suno().downloadFormat)
                               .has_value();

    QVariantMap meta;
    meta["tags"] = QString::fromStdString(clip.metadata.tags);
    meta["prompt"] = QString::fromStdString(clip.metadata.prompt);
    meta["lyrics"] = QString::fromStdString(clip.metadata.lyrics);
    map["metadata"] = meta;

    allClips_.append(map);
  }

  // Update pagination state from library manager
  if (auto* lm = s_controller->libraryManager()) {
    const bool hadMore = hasMorePages_;
    hasMorePages_ = lm->hasMorePages();
    if (hadMore != hasMorePages_) {
      emit hasMorePagesChanged();
    }
    const int prevPage = currentPage_;
    currentPage_ = lm->currentPage();
    if (prevPage != currentPage_) {
      emit currentPageChanged();
    }
  }

  updateFilteredClips();
  // Keep spinner active while auto-pagination is still fetching.
  // Full sync drives multiple feed/v3 pages; loading clears only when
  // the library manager reports exhaustion.
  if (!hasMorePages_) {
    loading_ = false;
    emit loadingChanged();
    stopLoadingWatchdog();
  } else {
    // Still more pages — keep loading true and refresh watchdog for
    // the next auto-page (rate-limited ~1 Hz).
    if (!loading_) {
      loading_ = true;
      emit loadingChanged();
    }
    startLoadingWatchdog();
  }
  emit authenticationChanged();
}

bool SunoBridge::isAuthenticated() const {
    return s_controller && s_client &&
           s_client->authState() == vc::suno::auth::AuthState::ActiveValid;
}

QString SunoBridge::googleLoginState() const {
    if (isAuthenticated()) {
        return QStringLiteral("authenticated");
    }
    if (!s_controller || !s_controller->authCoordinator()) {
        return QStringLiteral("signedOut");
    }
    return s_controller->authCoordinator()->googleLoginState();
}

QString SunoBridge::googleLoginError() const {
    if (!s_controller) return {};
    if (auto* coordinator = s_controller->authCoordinator()) {
        const QString coordinatorError = coordinator->googleLoginError();
        if (!coordinatorError.isEmpty()) {
            return coordinatorError;
        }
    }
    return synthesizedAuthFailureError(s_controller->authFailureKind());
}

QString SunoBridge::authFailureKind() const {
    if (!s_controller) return QStringLiteral("none");
    return authFailureKindString(s_controller->authFailureKind());
}

bool SunoBridge::googleLoginAvailable() const {
    return s_controller && s_controller->authCoordinator() &&
           s_controller->authCoordinator()->googleLoginAvailable();
}

void SunoBridge::beginGoogleSignIn() {
    if (!s_controller) return;
    if (auto* coordinator = s_controller->authCoordinator()) {
        QString safeError;
        // With no capture-approved launch, the coordinator takes its
        // policy-error path and never calls QDesktopServices.
        (void)coordinator->beginGoogleSignIn(&safeError);
    }
}

void SunoBridge::cancelGoogleSignIn() {
    if (s_controller) {
        if (auto* coordinator = s_controller->authCoordinator()) {
            coordinator->cancelGoogleSignIn();
        }
    }
}

void SunoBridge::signOutSuno() {
    // LOCAL ONLY: no capture-proven remote Suno logout endpoint exists.
    // This clears local credentials and stops local refresh work only.
    if (s_controller) {
        if (auto* coordinator = s_controller->authCoordinator()) {
            coordinator->signOutSuno();
        }
    }
}

void SunoBridge::refreshAccount() {
    if (s_controller) {
        s_controller->refreshAccount();
    }
}

void SunoBridge::refreshDiscover() {
    ensureExploreService();
    if (exploreService_) {
        exploreService_->refresh();
    }
}

void SunoBridge::loadMoreDiscover() {
    if (exploreService_) {
        setDiscoverError({});
        exploreService_->loadMore();
    }
}

void SunoBridge::refreshNotifications() {
    ensureNotificationService();
    if (notificationService_) {
        notificationService_->refresh();
    }
}

void SunoBridge::markAllNotificationsRead() {
    if (notificationService_) {
        notificationService_->markAllRead();
    }
}

void SunoBridge::onAuthenticationFailed(const QString& reason) {
    setErrorMessage(reason);
    clearLoading();
    emit authenticationFailed(reason);
}

void SunoBridge::clearLoading() {
    if (!loading_) {
        stopLoadingWatchdog();
        return;
    }
    loading_ = false;
    emit loadingChanged();
    stopLoadingWatchdog();
    emit authenticationChanged();
}

void SunoBridge::onLibraryFetchFailed(const QString& reason) {
    if (!reason.isEmpty()) {
        setErrorMessage(reason);
    }
    const bool hadMore = hasMorePages_;
    hasMorePages_ = false;
    if (hadMore) emit hasMorePagesChanged();
    if (loading_) {
        loading_ = false;
        emit loadingChanged();
    }
    stopLoadingWatchdog();
    emit authenticationChanged();
    // Keep clips as-is so empty-state can distinguish auth vs. truly empty.
}

void SunoBridge::onDiscoverReset() {
    discover_.clear();
    emit discoverChanged();
    if (discoverLoaded_) {
        discoverLoaded_ = false;
        emit discoverLoadedChanged();
    }
    setDiscoverError({});
}

void SunoBridge::onDiscoverPageReady() {
    if (!exploreService_) {
        return;
    }
    QVariantList feeds;
    const auto& sourceFeeds = exploreService_->feeds();
    feeds.reserve(sourceFeeds.size());
    for (const auto& feed : sourceFeeds) {
        QVariantList clips;
        clips.reserve(feed.clips.size());
        for (const auto& clip : feed.clips) {
            clips.push_back(toDiscoverClip(clip));
        }
        QVariantMap entry;
        entry[QStringLiteral("id")] = feed.id;
        entry[QStringLiteral("label")] = feed.label;
        entry[QStringLiteral("clips")] = clips;
        feeds.push_back(entry);
    }
    discover_ = feeds;
    emit discoverChanged();
    if (!discoverLoaded_) {
        discoverLoaded_ = true;
        emit discoverLoadedChanged();
    }
    setDiscoverError({});
}

void SunoBridge::onDiscoverLoadingChanged() {
    const bool loading = exploreService_ && exploreService_->isLoading();
    if (discoverLoading_ == loading) {
        return;
    }
    discoverLoading_ = loading;
    emit discoverLoadingChanged();
}

void SunoBridge::onDiscoverHasMoreChanged() {
    const bool hasMore = exploreService_ && exploreService_->hasMore();
    if (discoverHasMore_ == hasMore) {
        return;
    }
    discoverHasMore_ = hasMore;
    emit discoverHasMoreChanged();
}

void SunoBridge::onDiscoverFailed(const QString& reason) {
    setDiscoverError(reason);
}

void SunoBridge::onNotificationsChanged() {
    if (!notificationService_) {
        return;
    }
    QVariantList values;
    values.reserve(notificationService_->notifications().size());
    for (const auto& notification : notificationService_->notifications()) {
        values.push_back(toNotificationVariant(notification));
    }
    notifications_ = values;
    emit notificationsChanged();
}

void SunoBridge::onUnreadCountChanged() {
    const int count = notificationService_ ? notificationService_->unreadCount() : 0;
    if (unreadCount_ == count) {
        return;
    }
    unreadCount_ = count;
    emit unreadCountChanged();
}

void SunoBridge::onNotificationsLoadingChanged() {
    const bool loading = notificationService_ && notificationService_->isLoading();
    if (notificationsLoading_ == loading) {
        return;
    }
    notificationsLoading_ = loading;
    emit notificationsLoadingChanged();
}

void SunoBridge::onNotificationsErrorChanged(const QString& reason) {
    setNotificationsError(reason);
}

void SunoBridge::updateModelCatalog() {
    QVariantList catalog;
    if (s_controller && s_controller->accountManager()) {
        catalog = toQVariantModels(s_controller->accountManager()->models());
    }
    if (modelCatalog_ == catalog) {
        return;
    }
    modelCatalog_ = catalog;
    emit modelsChanged();
}

void SunoBridge::setStatusMessage(const QString& message) {
    if (statusMessage_ == message) {
        return;
    }
    statusMessage_ = message;
    emit statusMessageChanged();
}

void SunoBridge::setErrorMessage(const QString& message) {
    if (errorMessage_ == message) {
        return;
    }
    errorMessage_ = message;
    emit errorMessageChanged();
}

void SunoBridge::ensureAudioUploadService() {
    if (audioUploadService_ || !s_client) {
        return;
    }
    audioUploadService_ = new vc::suno::SunoAudioUploadService(
            s_client, s_client->networkManager(), this);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::stateChanged,
            this, &SunoBridge::audioUploadChanged);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::progressChanged,
            this, &SunoBridge::audioUploadChanged);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::statusChanged,
            this, &SunoBridge::audioUploadChanged);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::errorChanged,
            this, &SunoBridge::audioUploadChanged);
}

void SunoBridge::destroyAudioUploadService() {
    if (audioUploadService_) {
        disconnect(audioUploadService_, nullptr, this, nullptr);
        delete audioUploadService_;
        audioUploadService_ = nullptr;
        emit audioUploadChanged();
    }
}

void SunoBridge::ensureExploreService() {
    if (exploreService_ || !s_client) {
        return;
    }
    exploreService_ = new vc::suno::SunoExploreService(s_client, this);
    connect(exploreService_, &vc::suno::SunoExploreService::reset,
            this, &SunoBridge::onDiscoverReset);
    connect(exploreService_, &vc::suno::SunoExploreService::pageReady,
            this, &SunoBridge::onDiscoverPageReady);
    connect(exploreService_, &vc::suno::SunoExploreService::loadingChanged,
            this, &SunoBridge::onDiscoverLoadingChanged);
    connect(exploreService_, &vc::suno::SunoExploreService::hasMoreChanged,
            this, &SunoBridge::onDiscoverHasMoreChanged);
    connect(exploreService_, &vc::suno::SunoExploreService::failed,
            this, &SunoBridge::onDiscoverFailed);
}

void SunoBridge::destroyExploreService() {
    if (exploreService_) {
        disconnect(exploreService_, nullptr, this, nullptr);
        delete exploreService_;
        exploreService_ = nullptr;
    }
    if (!discover_.isEmpty()) {
        discover_.clear();
        emit discoverChanged();
    }
    if (discoverLoaded_) {
        discoverLoaded_ = false;
        emit discoverLoadedChanged();
    }
    if (discoverHasMore_) {
        discoverHasMore_ = false;
        emit discoverHasMoreChanged();
    }
    if (discoverLoading_) {
        discoverLoading_ = false;
        emit discoverLoadingChanged();
    }
    setDiscoverError({});
}

void SunoBridge::setDiscoverError(const QString& message) {
    if (discoverError_ == message) {
        return;
    }
    discoverError_ = message;
    emit discoverErrorChanged();
}

void SunoBridge::ensureNotificationService() {
    if (notificationService_ || !s_client) {
        return;
    }
    notificationService_ = new vc::suno::SunoNotificationService(s_client, this);
    connect(notificationService_, &vc::suno::SunoNotificationService::notificationsChanged,
            this, &SunoBridge::onNotificationsChanged);
    connect(notificationService_, &vc::suno::SunoNotificationService::unreadCountChanged,
            this, &SunoBridge::onUnreadCountChanged);
    connect(notificationService_, &vc::suno::SunoNotificationService::loadingChanged,
            this, &SunoBridge::onNotificationsLoadingChanged);
    connect(notificationService_, &vc::suno::SunoNotificationService::errorChanged,
            this, &SunoBridge::onNotificationsErrorChanged);
}

void SunoBridge::destroyNotificationService() {
    if (notificationService_) {
        disconnect(notificationService_, nullptr, this, nullptr);
        delete notificationService_;
        notificationService_ = nullptr;
    }
    if (!notifications_.isEmpty()) {
        notifications_.clear();
        emit notificationsChanged();
    }
    if (unreadCount_ != 0) {
        unreadCount_ = 0;
        emit unreadCountChanged();
    }
    if (notificationsLoading_) {
        notificationsLoading_ = false;
        emit notificationsLoadingChanged();
    }
    setNotificationsError({});
}

void SunoBridge::setNotificationsError(const QString& message) {
    if (notificationsError_ == message) {
        return;
    }
    notificationsError_ = message;
    emit notificationsErrorChanged();
}

void SunoBridge::setDownloadStatusForClip(const QString& clipId, const QString& message) {
    if (activeDownloadClipId_.isEmpty()) {
        activeDownloadClipId_ = clipId;
    }
    if (downloadStatus_ == message) {
        return;
    }
    downloadStatus_ = message;
    emit downloadStatusChanged();
}

QString SunoBridge::clipTitle(const QString& clipId) const {
    for (const auto& clipValue : allClips_) {
        const auto clip = clipValue.toMap();
        if (clip.value(QStringLiteral("id")).toString() == clipId) {
            const QString title = clip.value(QStringLiteral("title")).toString();
            return title.isEmpty() ? QStringLiteral("clip") : title;
        }
    }
    return QStringLiteral("clip");
}

void SunoBridge::startLoadingWatchdog() {
    if (loadingWatchdog_.isActive()) loadingWatchdog_.stop();
    loadingWatchdog_.start();
}

void SunoBridge::stopLoadingWatchdog() {
    if (loadingWatchdog_.isActive()) loadingWatchdog_.stop();
}

void SunoBridge::updateFilteredClips() {
    if (filterText_.isEmpty()) {
        clips_ = allClips_;
    } else {
        clips_.clear();
        const QString filter = filterText_.toLower();
        for (const auto& clipVar : allClips_) {
            const QVariantMap clip = clipVar.toMap();
            const QVariantMap metadata = clip.value(QStringLiteral("metadata")).toMap();
            const QString title = clip.value(QStringLiteral("title")).toString().toLower();
            const QString tags = metadata.value(QStringLiteral("tags")).toString().toLower();
            const QString prompt = metadata.value(QStringLiteral("prompt")).toString().toLower();

            if (title.contains(filter) || tags.contains(filter) || prompt.contains(filter)) {
                clips_.append(clip);
            }
        }
    }
    emit clipsChanged();
}

} // namespace qml_bridge
