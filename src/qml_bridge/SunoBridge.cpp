#include "SunoBridge.hpp"
#include "SunoDownloadSection.hpp"
#include "SunoMutationsSection.hpp"
#include <QStringList>
#include <QVariantMap>
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "suno/ClipParser.hpp"
#include "suno/ClipResolver.hpp"
#include "suno/DownloadQueue.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoAudioUploadService.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoModels.hpp"
#include "suno/SunoNotificationService.hpp"
#include "suno/auth/AuthCoordinator.hpp"
#include "ui/controllers/SunoController.hpp"

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
vc::suno::SunoClient* SunoBridge::s_client = nullptr;

SunoBridge::~SunoBridge() {
    destroyAudioUploadService();
    if (mutations_) {
        mutations_->detach();
    }
    if (downloads_) {
        downloads_->detach();
    }
    delete notificationService_;
    notificationService_ = nullptr;
    delete exploreService_;
    exploreService_ = nullptr;
    s_controller = nullptr;
    s_client = nullptr;
}

namespace {

/// The catalogued surfaces THIS BUILD actually implements — the only place that
/// fact lives.
///
/// Without it every gate resolves `LocallyDisabled`, because the resolver's switch
/// table starts all-off and nothing ever set one. That made a diagnostics panel
/// claim the Library — which plainly works — was "switched off in this build",
/// which is false. The switch now encodes "there is an implementation behind
/// this", so the catalogued-but-unbuilt majority reads as missing code rather
/// than as a forgotten toggle.
///
/// Each entry names a service that genuinely exists and is reached:
///   Library        SunoLibraryManager      (POST /api/feed/v3 paging)
///   Explore        SunoExploreService      (/api/unified/explore)
///   Notifications  SunoNotificationService (list, badge, mark-read)
///   Upload         SunoAudioUploadService  (3-step audio upload)
///   Billing        /api/billing/info/      (credits + plan, read-only)
///   AccountProfile /api/session/ user object
///   MediaDelivery  SunoDownloader + DownloadQueue
///
/// Deliberately NOT enabled, because no call site exists: Generation, Playlists,
/// Projects, CustomModels, Contests, Styles, LyricsCowrite, VideoGeneration,
/// Stems, Sharing, ClipRelations, Rights, AppChrome, MusicPlayer. Their route
/// constants exist and several are `[T1]`, which is not the same as having code
/// — see the three-layer reachability rule in TODO.md.
constexpr vc::suno::FeatureGate kGatesImplementedByThisBuild[]{
        vc::suno::FeatureGate::Library,       vc::suno::FeatureGate::Explore,
        vc::suno::FeatureGate::Notifications, vc::suno::FeatureGate::Upload,
        vc::suno::FeatureGate::Billing,       vc::suno::FeatureGate::AccountProfile,
        vc::suno::FeatureGate::MediaDelivery};

QString generationUnavailableMessage() {
    return QStringLiteral("Generation is unavailable because the required Suno CAPTCHA token flow "
                          "is not supported. No request was sent.");
}

QVariantMap toDiscoverClip(const vc::suno::SunoClip& clip) {
    QVariantMap result;
    result[QStringLiteral("id")] = QString::fromStdString(clip.id);
    result[QStringLiteral("title")] = QString::fromStdString(clip.title);
    result[QStringLiteral("status")] = QString::fromStdString(clip.status);
    result[QStringLiteral("image_url")] =
            vc::suno::ClipParser::selectImageUrl(QString::fromStdString(clip.image_large_url),
                                                 QString::fromStdString(clip.image_url))
                    .value_or(QString());
    result[QStringLiteral("creator")] =
            QString::fromStdString(clip.display_name.empty() ? clip.handle : clip.display_name);
    result[QStringLiteral("created_at")] = QString::fromStdString(clip.created_at);
    result[QStringLiteral("duration")] = QString::fromStdString(clip.metadata.duration);
    return result;
}

QVariantMap
toNotificationVariant(const vc::suno::SunoNotificationService::Notification& notification) {
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
        limits[QStringLiteral("negative_tags")] =
                static_cast<qlonglong>(model.max_lengths.negative_tags);
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
    mutations_ = new SunoMutationsSection(this);
    connect(mutations_, &SunoMutationsSection::availabilityChanged, this,
            &SunoBridge::mutationsAvailableChanged);
    connect(mutations_, &SunoMutationsSection::busyChanged, this,
            &SunoBridge::mutationsBusyChanged);
    connect(mutations_, &SunoMutationsSection::clipSettled, this, &SunoBridge::clipMutationSettled);
    connect(mutations_, &SunoMutationsSection::playlistSettled, this,
            &SunoBridge::playlistMutationSettled);
    connect(mutations_, &SunoMutationsSection::libraryRefreshRecommended, this,
            &SunoBridge::libraryRefreshRecommended);
    // The section reports a refusal or a rejection; the bridge keeps the one
    // error channel it has always had.
    connect(mutations_, &SunoMutationsSection::failure, this,
            [this](const QString& reason) { setErrorMessage(reason); });
    downloads_ = new SunoDownloadSection(this);
    connect(downloads_, &SunoDownloadSection::statusChanged, this,
            &SunoBridge::downloadStatusChanged);
    connect(downloads_, &SunoDownloadSection::statusMessageRequested, this,
            [this](const QString& message) { setStatusMessage(message); });
    connect(downloads_, &SunoDownloadSection::errorMessageRequested, this,
            [this](const QString& message) { setErrorMessage(message); });
    connect(downloads_, &SunoDownloadSection::clipSaved, this, &SunoBridge::clipSaved);
    connect(downloads_, &SunoDownloadSection::clipSaveRefused, this,
            &SunoBridge::clipSaveRefused);
    connect(downloads_, &SunoDownloadSection::clipDownloadSettled, this,
            &SunoBridge::clipDownloadSettled);
    // The library cache is the bridge's, so the section asks for a title
    // instead of keeping a second copy of the list.
    downloads_->setTitleProvider([this](const QString& clipId) { return clipTitle(clipId); });
    if (s_controller) {
        wireControllerSignals();
    }

    searchDebounce_.setSingleShot(true);
    searchDebounce_.setInterval(350);
    connect(&searchDebounce_, &QTimer::timeout, this,
            [this]() { setFilterText(searchDebounceText_); });

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
            // The downloader is a child of the outgoing controller, and
            // disconnecting the controller does not reach its children's
            // signals, so the section drops the borrow explicitly. The next
            // attach() resolves the new controller's own downloader.
            if (bridgeInstance->downloads_) {
                bridgeInstance->downloads_->detach();
            }
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
            // The mutation service is dropped too: it borrows the resolver out of
            // an account that no longer exists.
            if (bridgeInstance->mutations_) {
                bridgeInstance->mutations_->detach();
            }
            if (bridgeInstance->downloads_) {
                bridgeInstance->downloads_->detach();
            }
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
    // The section owns the mutation service. Attaching re-points the gate
    // resolver rather than constructing a new service: the resolver lives inside
    // SunoAccountManager, which is replaced on every sign-in, so a pointer
    // captured at construction would dangle. Called here because this function
    // runs whenever the controller is attached, which is exactly when the
    // account manager may have been swapped.
    if (mutations_) {
        mutations_->attach(s_controller, s_client);
    }
    // Resolved rather than constructed: the downloader (and its one
    // DownloadQueue) belongs to the controller, so this only wires the
    // saved-file signal. Fails closed to a null borrow if it is ever absent.
    // The section owns the downloader borrow and the download-state handling.
    if (downloads_) {
        downloads_->attach(s_controller);
    }
    auto* bridgeInstance = this;

    connect(s_controller, &vc::suno::SunoController::libraryUpdated, bridgeInstance,
            &SunoBridge::onLibraryUpdated);
    connect(s_controller, &vc::suno::SunoController::authenticationRequired, bridgeInstance,
            &SunoBridge::clearLoading);
    connect(s_controller, &vc::suno::SunoController::authenticationFailed, bridgeInstance,
            &SunoBridge::onAuthenticationFailed);
    connect(s_controller, &vc::suno::SunoController::authFailureKindChanged, bridgeInstance,
            [bridgeInstance]() {
                emit bridgeInstance->authFailureKindChanged();
                emit bridgeInstance->googleLoginErrorChanged();
            });
    connect(s_controller, &vc::suno::SunoController::libraryFetchFailed, bridgeInstance,
            &SunoBridge::onLibraryFetchFailed);
    connect(s_controller, &vc::suno::SunoController::sunoError, bridgeInstance,
            &SunoBridge::onLibraryFetchFailed);
    connect(s_controller, &vc::suno::SunoController::statusMessage, bridgeInstance,
            [bridgeInstance](const std::string& message) {
                bridgeInstance->setStatusMessage(QString::fromStdString(message));
            });

    if (auto* lm = s_controller->libraryManager()) {
        connect(lm, &vc::suno::SunoLibraryManager::authenticationRequired, bridgeInstance,
                &SunoBridge::clearLoading);
        connect(lm, &vc::suno::SunoLibraryManager::libraryFetchFailed, bridgeInstance,
                &SunoBridge::onLibraryFetchFailed);
    }

    if (s_client) {
        connect(s_client, &vc::suno::SunoClient::needsReauth, bridgeInstance,
                &SunoBridge::clearLoading);
        connect(s_client, &vc::suno::SunoClient::authStateChanged, bridgeInstance,
                [bridgeInstance]() {
                    if (bridgeInstance->s_client->isAuthenticated()) {
                        bridgeInstance->setErrorMessage({});
                    }
                    emit bridgeInstance->authenticationChanged();
                });
        // Custom Signal<> for generic errors: queued clear so it
        // arrives on the bridge's thread even if emitted off-thread.
        clientErrorConnectionId_ =
                s_client->errorOccurred.connect([bridgeInstance](const std::string& err) {
                    QMetaObject::invokeMethod(
                            bridgeInstance,
                            [bridgeInstance, err]() {
                                bridgeInstance->onLibraryFetchFailed(QString::fromStdString(err));
                            },
                            Qt::QueuedConnection);
                });
    }

    if (auto* coordinator = s_controller->authCoordinator()) {
        // AuthCoordinator owns the canonical state strings.  The bridge
        // mirrors its signals without introducing a second auth state machine.
        connect(coordinator, &vc::suno::auth::AuthCoordinator::stateChanged, bridgeInstance,
                [bridgeInstance](const QString&) {
                    emit bridgeInstance->googleLoginStateChanged();
                });
        connect(coordinator, &vc::suno::auth::AuthCoordinator::errorChanged, bridgeInstance,
                [bridgeInstance](const QString&) {
                    emit bridgeInstance->googleLoginErrorChanged();
                });
    }

    // Account snapshot -> QML properties.
    if (auto* am = s_controller->accountManager()) {
        connect(am, &vc::suno::SunoAccountManager::billingInfoReady, bridgeInstance,
                &SunoBridge::billingInfoChanged);
        connect(am, &vc::suno::SunoAccountManager::accountInfoReady, bridgeInstance,
                [bridgeInstance]() {
                    bridgeInstance->updateModelCatalog();
                    emit bridgeInstance->accountInfoChanged();
                });
        // The catalogue comes from `/api/billing/info/`, so a billing-only
        // refresh (which happens after a generation) also changes it. This is the
        // honest signal for that: `accountInfoReady` means the user object, and
        // emitting it from the billing path would make a billing refresh
        // masquerade as an account refresh.
        connect(am, &vc::suno::SunoAccountManager::billingInfoReady, bridgeInstance,
                [bridgeInstance]() {
                    bridgeInstance->updateModelCatalog();
                    emit bridgeInstance->billingInfoChanged();
                });
        connect(am, &vc::suno::SunoAccountManager::accountError, bridgeInstance,
                [bridgeInstance](const QString& message) {
                    bridgeInstance->setErrorMessage(message);
                });
        // The generation gate's verdict and reason both move when the server flag
        // map changes, so both properties notify from this one signal. Without it
        // the properties would be correct but never re-read — the same
        // invisibility the old CONSTANT declaration had.
        connect(am, &vc::suno::SunoAccountManager::gatesChanged, bridgeInstance,
                [bridgeInstance]() {
                    emit bridgeInstance->generationAvailableChanged();
                    emit bridgeInstance->gateStatusesChanged();
                    // The mutation surface consults the same resolver, so its
                    // availability moves on the same signal. Without this the
                    // property would be correct but never re-read by a QML
                    // binding — the same invisibility the old CONSTANT
                    // declaration had.
                    if (bridgeInstance->mutations_) {
                        bridgeInstance->mutations_->onGatesChanged();
                    }
                });

        // Declare, once, which catalogued surfaces THIS BUILD actually
        // implements. This is the only place that fact lives.
        //
        // Without it every gate resolves `LocallyDisabled`, because the resolver's
        // switch table starts all-off and nothing ever set one. That produced a
        // panel claiming the Library — which plainly works — was "switched off in
        // this build", which is false. The switch now means "there is an
        // implementation behind this", so the catalogued-but-unbuilt majority
        // reads as missing code rather than as a forgotten toggle.
        //
        // Each entry below names a service that genuinely exists and is reached:
        //   Library        SunoLibraryManager   (POST /api/feed/v3 paging)
        //   Explore        SunoExploreService   (/api/unified/explore)
        //   Notifications  SunoNotificationService (list, badge, mark-read)
        //   Upload         SunoAudioUploadService (3-step audio upload)
        //   Billing        /api/billing/info/   (credits + plan, read-only)
        //   AccountProfile /api/session/ user object
        //   MediaDownload  SunoDownloader + DownloadQueue
        //
        // Deliberately NOT enabled, because no call site exists: Generation,
        // Playlists, Projects, CustomModels, Contests, Styles, LyricsCowrite,
        // VideoGeneration, Stems, Sharing, ClipRelations, Rights, AppChrome,
        // MusicPlayer. Their route constants exist and several are `[T1]`, which
        // is not the same as having code — see the three-layer reachability rule.
        for (const vc::suno::FeatureGate gate : kGatesImplementedByThisBuild) {
            // [[nodiscard]]: a refused enable must not be silent. It would mean
            // this build implements the surface but the catalog's evidence floor
            // or an exclusion contradicts that, which is a real inconsistency.
            if (!am->gates().setLocallyEnabled(gate, true)) {
                LOG_WARN("SunoBridge: gate {} is implemented here but the resolver "
                         "refused to enable it; the catalog and the call sites disagree",
                         vc::suno::toString(gate));
            }
        }
    }
    connect(s_controller, &vc::suno::SunoController::chatMessageReceived, bridgeInstance,
            [bridge = bridgeInstance](const QString& response, const QString& workspaceId) {
                QVariantMap assistantMsg;
                assistantMsg["role"] = "assistant";
                assistantMsg["content"] = response;
                assistantMsg["workspaceId"] = workspaceId;
                bridge->chatHistory_.append(assistantMsg);
                emit bridge->chatHistoryChanged();
            });

    connect(s_controller, &vc::suno::SunoController::chatHistoryFetched, bridgeInstance,
            [bridge = bridgeInstance](const QVariantList& sessions) {
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

bool SunoBridge::hasMorePages() const { return hasMorePages_; }

int SunoBridge::currentPage() const { return currentPage_; }

QVariantList SunoBridge::chatHistory() const { return chatHistory_; }

QVariantList SunoBridge::models() const { return modelCatalog_; }

bool SunoBridge::generationAvailable() const {
    // Was `return false` — unconditional, with no reason anywhere. That made it
    // a well-built door to nowhere: SunoPanel.qml renders a real model dropdown
    // and a permanently disabled Generate button, and nothing in the tree could
    // say why. It is now a real verdict from the gate resolver.
    //
    // Default-off is still the correct shipped answer, and now for a reason a
    // user can be shown: generation is gated on an explicit client switch that
    // stays closed pending a human decision on the captcha. The difference is
    // that this can now become `true` when that decision is made, and
    // `generationUnavailableReason()` can say which of the six resolution steps
    // is actually holding it.
    if (!s_controller) {
        return false;
    }
    const auto* accountManager = s_controller->accountManager();
    if (!accountManager) {
        return false;
    }
    return accountManager->gates().isAvailable(vc::suno::FeatureGate::Generation);
}

QString SunoBridge::generationUnavailableReason() const {
    if (!s_controller) {
        return QStringLiteral("Suno is not connected.");
    }
    const auto* accountManager = s_controller->accountManager();
    if (!accountManager) {
        return QStringLiteral("Suno is not connected.");
    }
    return accountManager->gates().evaluate(vc::suno::FeatureGate::Generation).reason;
}

QVariantList SunoBridge::gateStatuses() const {
    QVariantList rows;
    if (!s_controller) {
        return rows;
    }
    const auto* accountManager = s_controller->accountManager();
    if (!accountManager) {
        return rows;
    }
    const auto& gates = accountManager->gates();

    for (const vc::suno::GateVerdict& verdict : gates.evaluateAll()) {
        QVariantMap row;
        row.insert(QStringLiteral("gate"), QString::fromLatin1(vc::suno::toString(verdict.gate)));
        row.insert(QStringLiteral("status"),
                   QString::fromLatin1(vc::suno::toString(verdict.status)));
        row.insert(QStringLiteral("available"), verdict.status == vc::suno::GateStatus::Available);
        row.insert(QStringLiteral("reason"), verdict.reason);

        // The catalog row supplies the human-facing metadata. A verdict whose
        // gate has no catalog entry is reported as EvidenceBlocked by the
        // resolver, and `findFeature` returning null here is handled rather than
        // dereferenced — the row is still emitted, just without a title.
        if (const vc::suno::FeatureDefinition* def = vc::suno::findFeature(verdict.gate)) {
            row.insert(QStringLiteral("title"), QString::fromStdString(std::string(def->title)));
            row.insert(QStringLiteral("area"), QString::fromStdString(std::string(def->area)));
            row.insert(QStringLiteral("evidence"),
                       QString::fromLatin1(vc::suno::hosts::toString(def->evidence)));
            row.insert(QStringLiteral("serverFlags"),
                       QString::fromStdString(std::string(def->serverFlags)));
            row.insert(QStringLiteral("note"), QString::fromStdString(std::string(def->note)));
        }
        rows.append(row);
    }
    return rows;
}

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

QString SunoBridge::downloadStatus() const {
    return downloads_ ? downloads_->status() : QString();
}

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

void SunoBridge::generate(const QString& prompt, const QString& tags, bool instrumental,
                          const QString& model) {
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
    if (downloads_) {
        downloads_->playClip(clipId);
    }
}

void SunoBridge::downloadClip(const QString& clipId) {
    if (downloads_) {
        downloads_->downloadClip(clipId);
    }
}

void SunoBridge::downloadClips(const QStringList& clipIds) {
    if (downloads_) {
        downloads_->downloadClips(clipIds);
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
    setErrorMessage(QStringLiteral(
            "B-Side Chat is unavailable because its endpoints are not capture-backed."));
}

void SunoBridge::fetchChatHistory() {
    setErrorMessage(QStringLiteral(
            "B-Side Chat is unavailable because its endpoints are not capture-backed."));
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
        map["image_url"] =
                vc::suno::ClipParser::selectImageUrl(QString::fromStdString(clip.image_large_url),
                                                     QString::fromStdString(clip.image_url))
                        .value_or(QString());
        map["model_name"] = QString::fromStdString(clip.model_name);
        map["major_model_version"] = QString::fromStdString(clip.major_model_version);
        map["created_at"] = QString::fromStdString(clip.created_at);
        map["play_count"] = static_cast<int>(clip.play_count);
        map["duration"] = QString::fromStdString(clip.metadata.duration);
        map["has_media"] =
                vc::suno::SunoDownloader::selectDownloadUrl(clip, CONFIG.suno().downloadFormat)
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
    return QString::fromLatin1(
            vc::suno::auth::authFailureMessage(s_controller->authFailureKind()));
}

QString SunoBridge::authFailureKind() const {
    if (!s_controller) return QStringLiteral("none");
    return QString::fromLatin1(
            vc::suno::auth::authFailureKindId(s_controller->authFailureKind()));
}

bool SunoBridge::googleLoginAvailable() const {
    return s_controller && s_controller->authCoordinator() &&
           s_controller->authCoordinator()->googleLoginAvailable();
}

void SunoBridge::beginGoogleSignIn() {
    // T0181: opens the system browser on Suno's sign-in page. The OAuth
    // exchange is server-side (Clerk owns the redirect_uri), so there is
    // nothing here to wait for -- the browser is the whole flow, and the
    // credential comes back through the paste field.
    if (!s_controller) return;
    if (auto* coordinator = s_controller->authCoordinator()) {
        QString safeError;
        if (!coordinator->beginGoogleSignIn(&safeError)) {
            emit googleLoginErrorChanged();
        }
    }
}

void SunoBridge::importSessionFromBrowser() {
    // T0199. The complement to beginGoogleSignIn(): the browser half opens the
    // page, this half collects the credential that already lives there. It is
    // a QML-invokable because the user has to press the button -- reading a
    // browser's Safe Storage secret prompts macOS for consent, and that prompt
    // is the feature, not a friction to automate away.
    if (!s_controller) return;
    if (auto* coordinator = s_controller->authCoordinator()) {
        QString safeError;
        if (!coordinator->importSessionFromBrowser(&safeError)) {
            emit googleLoginErrorChanged();
        }
    }
}

void SunoBridge::cancelGoogleSignIn() {
    // The companion to importSessionFromBrowser(): the "Finish in your
    // browser" page stays up until the user abandons it, and before this
    // existed that meant restarting the app. Pure state reset — nothing is
    // running to interrupt.
    if (!s_controller) return;
    if (auto* coordinator = s_controller->authCoordinator()) {
        coordinator->cancelGoogleSignIn();
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
    // Dropped on sign-out, alongside the notification state, for the same reason
    // `FeatureFlags` resets its flag map there: leaving the old service alive
    // would keep a borrowed pointer into an `SunoAccountManager` that is about to
    // be replaced, and would let a mutation surface read as available to an
    // account that no longer exists.
    if (mutations_) {
        mutations_->detach();
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

void SunoBridge::onDiscoverFailed(const QString& reason) { setDiscoverError(reason); }

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
        // `preferredModels()` reads `/api/billing/info/`, not `/api/session/`.
        // Measured against the captured bodies, session `models` is an EMPTY
        // array in every capture, while billing carries the real catalogue with
        // the identical field shape — and billing is the one place the account's
        // own model entitlement is authoritative. The session list is keyed by
        // client identity and its web branch was measured serving a stale set.
        catalog = toQVariantModels(s_controller->accountManager()->preferredModels());
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
    audioUploadService_ =
            new vc::suno::SunoAudioUploadService(s_client, s_client->networkManager(), this);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::stateChanged, this,
            &SunoBridge::audioUploadChanged);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::progressChanged, this,
            &SunoBridge::audioUploadChanged);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::statusChanged, this,
            &SunoBridge::audioUploadChanged);
    connect(audioUploadService_, &vc::suno::SunoAudioUploadService::errorChanged, this,
            &SunoBridge::audioUploadChanged);
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
    connect(exploreService_, &vc::suno::SunoExploreService::reset, this,
            &SunoBridge::onDiscoverReset);
    connect(exploreService_, &vc::suno::SunoExploreService::pageReady, this,
            &SunoBridge::onDiscoverPageReady);
    connect(exploreService_, &vc::suno::SunoExploreService::loadingChanged, this,
            &SunoBridge::onDiscoverLoadingChanged);
    connect(exploreService_, &vc::suno::SunoExploreService::hasMoreChanged, this,
            &SunoBridge::onDiscoverHasMoreChanged);
    connect(exploreService_, &vc::suno::SunoExploreService::failed, this,
            &SunoBridge::onDiscoverFailed);
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
    connect(notificationService_, &vc::suno::SunoNotificationService::notificationsChanged, this,
            &SunoBridge::onNotificationsChanged);
    connect(notificationService_, &vc::suno::SunoNotificationService::unreadCountChanged, this,
            &SunoBridge::onUnreadCountChanged);
    connect(notificationService_, &vc::suno::SunoNotificationService::loadingChanged, this,
            &SunoBridge::onNotificationsLoadingChanged);
    connect(notificationService_, &vc::suno::SunoNotificationService::errorChanged, this,
            &SunoBridge::onNotificationsErrorChanged);
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

// ═══════════════════════════════════════════════════════════════════════════
// Library & playlist mutations
//
// Thin forwarders. Every request body is built inside
// `SunoLibraryMutations` from the captured contracts in master §5.9, so there
// is exactly one owner of the wire format and this file cannot drift from it.
//
// What this layer adds and nothing else:
//   * service lifetime, torn down on sign-out;
//   * routing a settled result to the clip-scoped or playlist-scoped signal;
//   * turning "nothing is attached" into a sentence instead of silence.
//
// It deliberately does NOT assemble JSON, validate enum members that were never
// captured, or write anything to the local database.
// ── The mutation surface ─────────────────────────────────────────────────────
// The service, its lifetime and the routing of a settled result live in
// `SunoMutationsSection`. These getters read through it, so the QML surface
// keeps its names and its meaning.

bool SunoBridge::mutationsAvailable() const {
    return mutations_ && mutations_->available();
}

QString SunoBridge::mutationsUnavailableReason() const {
    if (!mutations_) {
        return QStringLiteral("Library changes need a signed-in Suno account.");
    }
    return mutations_->unavailableReason();
}

bool SunoBridge::mutationsBusy() const { return mutations_ && mutations_->busy(); }

// ── The invokables ───────────────────────────────────────────────────────────
// Each forwards to the section, which owns the service and the routing. The
// names, the arguments and the QML-visible behaviour are unchanged: this is a
// move, not a new API.

void SunoBridge::setClipVisibility(const QString& clipId, const bool isPublic) {
    if (mutations_) {
        mutations_->setClipVisibility(clipId, isPublic);
    }
}

void SunoBridge::setClipRemixPermission(const QString& clipId, const bool canRemix) {
    if (mutations_) {
        mutations_->setClipRemixPermission(clipId, canRemix);
    }
}

void SunoBridge::setClipShowRemixes(const QString& clipId, const bool showRemix) {
    if (mutations_) {
        mutations_->setClipShowRemixes(clipId, showRemix);
    }
}

void SunoBridge::setClipFeedback(const QString& clipId, const QString& reason) {
    if (mutations_) {
        mutations_->setClipFeedback(clipId, reason);
    }
}

void SunoBridge::shareClip(const QString& clipId, const QString& contentType) {
    if (mutations_) {
        mutations_->shareClip(clipId, contentType);
    }
}

void SunoBridge::setPlaylistMetadata(const QString& playlistId, const QString& name,
                                     const QString& description) {
    if (mutations_) {
        mutations_->setPlaylistMetadata(playlistId, name, description);
    }
}

void SunoBridge::updatePlaylistClips(const QString& playlistId, const QString& updateType,
                                     const QVariantMap& metadata) {
    // The string-side mapping and the refusal for an unmapped member live in
    // the section, beside the service that consumes the enum.
    if (mutations_) {
        mutations_->updatePlaylistClips(playlistId, updateType, metadata);
    }
}

void SunoBridge::addClipsToPlaylist(const QString& playlistId, const QStringList& clipIds) {
    if (mutations_) {
        mutations_->addClipsToPlaylist(playlistId, clipIds);
    }
}

void SunoBridge::removeClipsFromPlaylist(const QString& playlistId, const QStringList& clipIds) {
    if (mutations_) {
        mutations_->removeClipsFromPlaylist(playlistId, clipIds);
    }
}

void SunoBridge::reorderPlaylistTracks(const QString& playlistId, const QVariantList& positions) {
    if (mutations_) {
        mutations_->reorderPlaylistTracks(playlistId, positions);
    }
}

void SunoBridge::setPlaylistCoverImage(const QString& playlistId, const QString& imageId) {
    if (mutations_) {
        mutations_->setPlaylistCoverImage(playlistId, imageId);
    }
}

} // namespace qml_bridge
