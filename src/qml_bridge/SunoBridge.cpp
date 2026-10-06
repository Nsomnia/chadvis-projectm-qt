#include "SunoBridge.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/ClipParser.hpp"
#include "suno/ClipResolver.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoClient.hpp"
#include "suno/auth/AuthCoordinator.hpp"
#include "suno/DownloadQueue.hpp"
#include "suno/SunoDownloader.hpp"
#include "suno/SunoExploreService.hpp"
#include "suno/SunoNotificationService.hpp"
#include "suno/SunoAudioUploadService.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoLibraryMutations.hpp"
#include "suno/SunoModels.hpp"
#include <QStringList>
#include <QVariantMap>

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
vc::suno::SunoClient* SunoBridge::s_client = nullptr;

SunoBridge::~SunoBridge() {
    destroyAudioUploadService();
    destroyMutationService();
    delete notificationService_;
    notificationService_ = nullptr;
    delete exploreService_;
    exploreService_ = nullptr;
    if (downloader_) {
        disconnect(downloader_, nullptr, this, nullptr);
        downloader_ = nullptr;
    }
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
        vc::suno::FeatureGate::Library,        vc::suno::FeatureGate::Explore,
        vc::suno::FeatureGate::Notifications,  vc::suno::FeatureGate::Upload,
        vc::suno::FeatureGate::Billing,        vc::suno::FeatureGate::AccountProfile,
        vc::suno::FeatureGate::MediaDelivery};

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
    result[QStringLiteral("image_url")] = vc::suno::ClipParser::selectImageUrl(
            QString::fromStdString(clip.image_large_url),
            QString::fromStdString(clip.image_url))
            .value_or(QString());
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
            // The downloader is a child of the outgoing controller, and
            // disconnecting the controller does not reach its children's
            // signals, so drop the borrow explicitly. The next ensureDownloader()
            // resolves the new controller's own downloader.
            if (bridgeInstance->downloader_) {
                QObject::disconnect(bridgeInstance->downloader_, nullptr, bridgeInstance, nullptr);
                bridgeInstance->downloader_ = nullptr;
            }
            bridgeInstance->playRequestClipId_.clear();
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
            if (bridgeInstance->downloader_) {
                QObject::disconnect(bridgeInstance->downloader_, nullptr, bridgeInstance, nullptr);
                bridgeInstance->downloader_ = nullptr;
            }
            bridgeInstance->playRequestClipId_.clear();
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
    ensureMutationService();
    // Re-point rather than construct: the resolver lives inside
    // SunoAccountManager, which is replaced on every sign-in, so a pointer
    // captured at construction would dangle. Called here because this function
    // runs whenever the controller is attached, which is exactly when the
    // account manager may have been swapped.
    if (mutationService_ && s_controller->accountManager()) {
        mutationService_->setGateResolver(&s_controller->accountManager()->gates());
    }
    // Resolved rather than constructed: the downloader (and its one
    // DownloadQueue) belongs to the controller, so this only wires the
    // saved-file signal. Fails closed to a null borrow if it is ever absent.
    ensureDownloader();
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
                // Only an explicit play request suppresses other clips' events.
                // A save-only request leaves this empty, so a batch's results
                // are never filtered out by the play path's bookkeeping.
                if (!bridgeInstance->playRequestClipId_.isEmpty() &&
                    clipId != bridgeInstance->playRequestClipId_) {
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
                case vc::suno::DownloadState::Completed: {
                    // Save and play are separate requests that share one queue
                    // event, so the wording has to come from the request rather
                    // than be hardcoded: a silent save must never claim the
                    // transport moved.
                    const bool wasPlayRequest = bridgeInstance->playRequestClipId_ == clipId;
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, wasPlayRequest
                                        ? QStringLiteral("Downloaded %1; now playing").arg(title)
                                        : QStringLiteral("Saved %1 to your downloads").arg(title));
                    if (wasPlayRequest) {
                        bridgeInstance->playRequestClipId_.clear();
                        bridgeInstance->activeDownloadClipId_.clear();
                    }
                    break;
                }
                case vc::suno::DownloadState::FailedRetryable:
                case vc::suno::DownloadState::FailedPermanent:
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, QStringLiteral("Download failed for %1").arg(title));
                    if (bridgeInstance->playRequestClipId_ == clipId) {
                        bridgeInstance->playRequestClipId_.clear();
                        bridgeInstance->activeDownloadClipId_.clear();
                    }
                    break;
                case vc::suno::DownloadState::Cancelled:
                    bridgeInstance->setDownloadStatusForClip(
                            clipId, QStringLiteral("Download cancelled for %1").arg(title));
                    if (bridgeInstance->playRequestClipId_ == clipId) {
                        bridgeInstance->playRequestClipId_.clear();
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
        // The catalogue comes from `/api/billing/info/`, so a billing-only
        // refresh (which happens after a generation) also changes it. This is the
        // honest signal for that: `accountInfoReady` means the user object, and
        // emitting it from the billing path would make a billing refresh
        // masquerade as an account refresh.
        connect(am, &vc::suno::SunoAccountManager::billingInfoReady,
                bridgeInstance, [bridgeInstance]() {
                    bridgeInstance->updateModelCatalog();
                    emit bridgeInstance->billingInfoChanged();
                });
        connect(am, &vc::suno::SunoAccountManager::accountError,
                bridgeInstance, [bridgeInstance](const QString& message) {
                    bridgeInstance->setErrorMessage(message);
                });
        // The generation gate's verdict and reason both move when the server flag
        // map changes, so both properties notify from this one signal. Without it
        // the properties would be correct but never re-read — the same
        // invisibility the old CONSTANT declaration had.
        connect(am, &vc::suno::SunoAccountManager::gatesChanged,
                bridgeInstance, [bridgeInstance]() {
                    emit bridgeInstance->generationAvailableChanged();
                    emit bridgeInstance->gateStatusesChanged();
                    // The mutation surface consults the same resolver, so its
                    // availability moves on the same signal. Without this the
                    // property would be correct but never re-read by a QML
                    // binding — the same invisibility the old CONSTANT
                    // declaration had.
                    bridgeInstance->onMutationsAvailabilityChanged();
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
        row.insert(QStringLiteral("gate"),
                   QString::fromLatin1(vc::suno::toString(verdict.gate)));
        row.insert(QStringLiteral("status"),
                   QString::fromLatin1(vc::suno::toString(verdict.status)));
        row.insert(QStringLiteral("available"),
                   verdict.status == vc::suno::GateStatus::Available);
        row.insert(QStringLiteral("reason"), verdict.reason);

        // The catalog row supplies the human-facing metadata. A verdict whose
        // gate has no catalog entry is reported as EvidenceBlocked by the
        // resolver, and `findFeature` returning null here is handled rather than
        // dereferenced — the row is still emitted, just without a title.
        if (const vc::suno::FeatureDefinition* def =
                    vc::suno::findFeature(verdict.gate)) {
            row.insert(QStringLiteral("title"), QString::fromStdString(
                                                       std::string(def->title)));
            row.insert(QStringLiteral("area"), QString::fromStdString(
                                                      std::string(def->area)));
            row.insert(QStringLiteral("evidence"),
                       QString::fromLatin1(vc::suno::hosts::toString(def->evidence)));
            row.insert(QStringLiteral("serverFlags"),
                       QString::fromStdString(std::string(def->serverFlags)));
            row.insert(QStringLiteral("note"),
                       QString::fromStdString(std::string(def->note)));
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
    // Marked separately from activeDownloadClipId_, which is the single
    // status slot and is latched by any download event: only an explicit
    // play request may suppress other clips' events or be reported as
    // "now playing".
    playRequestClipId_ = clipId;
    const QString title = clipTitle(clipId);
    setStatusMessage(QStringLiteral("Preparing %1 for playback").arg(title));
    setDownloadStatusForClip(clipId,
                             QStringLiteral("Preparing %1 for playback").arg(title));
    if (!s_controller->playClipById(clipId.toStdString())) {
        playRequestClipId_.clear();
        activeDownloadClipId_.clear();
        setErrorMessage(QStringLiteral("This clip has no playable media yet."));
    }
}

void SunoBridge::downloadClip(const QString& clipId) {
    downloadInto({clipId});
}

void SunoBridge::downloadClips(const QStringList& clipIds) {
    downloadInto(clipIds);
}

/// Save-only by construction: nothing in this path can enqueue, jump, or
/// otherwise touch the transport, so a batch cannot hijack playback. Playback
/// stays the separate, explicit playClip() request.
void SunoBridge::downloadInto(const QStringList& clipIds) {
    auto* downloader = ensureDownloader();
    if (!downloader || !s_controller) {
        setErrorMessage(
            QStringLiteral("Downloads are unavailable because no download service is attached."));
        return;
    }
    if (clipIds.isEmpty()) {
        return;
    }

    setErrorMessage({});

    int accepted = 0;
    int rejected = 0;
    int unknown = 0;
    QString firstRejection;
    for (const QString& clipId : clipIds) {
        const auto clip = vc::suno::resolveClip(s_controller->clips(), s_controller->db(),
                                                clipId.toStdString());
        if (!clip) {
            ++unknown;
            emit clipSaveRefused(
                    clipId, QStringLiteral("This clip is no longer in the library, so it "
                                           "cannot be saved."));
            continue;
        }
        auto result = downloader->download(*clip);
        if (!result) {
            ++rejected;
            if (firstRejection.isEmpty()) {
                firstRejection = result.error();
            }
            // Per-clip, carrying the id. Without it a batch refusal collapses
            // into one aggregate string and the UI cannot tell WHICH clips
            // failed — so every rejected card stays stuck on "Saving" while one
            // of them silently did nothing.
            emit clipSaveRefused(clipId, result.error());
            continue;
        }
        ++accepted;
    }

    if (accepted > 0) {
        setStatusMessage(accepted == 1 ? QStringLiteral("Saving 1 clip to disk")
                                       : QStringLiteral("Saving %1 clips to disk").arg(accepted));
    }

    // Report why a request was refused rather than leaving a dead button: the
    // clip has no capture-backed media, or it is not in the loaded library.
    QString message;
    if (rejected == 1) {
        message = firstRejection;
    } else if (rejected > 1) {
        message = QStringLiteral("%1 clips could not be saved. First reason: %2")
                      .arg(rejected)
                      .arg(firstRejection);
    }
    if (unknown > 0) {
        QString missing;
        if (unknown == 1) {
            missing = QStringLiteral("1 clip is not in the loaded library.");
        } else {
            missing = QStringLiteral("%1 clips are not in the loaded library.").arg(unknown);
        }
        message = message.isEmpty() ? missing : QStringLiteral("%1 %2").arg(missing, message);
    }
    if (!message.isEmpty()) {
        setErrorMessage(message);
    }
}

vc::suno::SunoDownloader* SunoBridge::ensureDownloader() {
    if (!s_controller) {
        downloader_ = nullptr;
        return nullptr;
    }
    // Asked of the controller directly rather than found via findChild. The
    // controller owns the one SunoDownloader (and therefore the one
    // DownloadQueue), so an accessor is the honest way to reach it: findChild
    // resolves by object tree, which fails silently and without a diagnostic if
    // the parent ever changes, and it also matches any other downloader that
    // happened to be parented somewhere. Still re-resolved rather than cached
    // blindly, so a controller swap cannot leave a dangling pointer or a
    // duplicate signal connection behind.
    auto* found = s_controller->downloader();
    if (found == downloader_) {
        return downloader_;
    }
    if (downloader_) {
        disconnect(downloader_, nullptr, this, nullptr);
    }
    downloader_ = found;
    if (downloader_) {
        connect(downloader_, &vc::suno::SunoDownloader::fileSaved, this, &SunoBridge::clipSaved);
    }
    return downloader_;
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
    map["image_url"] = vc::suno::ClipParser::selectImageUrl(
            QString::fromStdString(clip.image_large_url),
            QString::fromStdString(clip.image_url))
            .value_or(QString());
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
    // Dropped on sign-out, alongside the notification state, for the same reason
    // `FeatureFlags` resets its flag map there: leaving the old service alive
    // would keep a borrowed pointer into an `SunoAccountManager` that is about to
    // be replaced, and would let a mutation surface read as available to an
    // account that no longer exists.
    destroyMutationService();
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
namespace {

using vc::suno::MutationOutcome;
using vc::suno::MutationResult;
using vc::suno::MutationVerb;
using vc::suno::PlaylistUpdateType;

/// True for the five clip-scoped verbs. The remaining six act on a playlist.
///
/// This is the routing table that keeps `clipMutationSettled` and
/// `playlistMutationSettled` honest: a playlist id must never arrive on a
/// signal a card delegate is listening to, or a clip card would update from a
/// playlist write.
[[nodiscard]] bool isClipScoped(const MutationVerb verb) noexcept
{
    switch (verb) {
        case MutationVerb::SetVisibility:
        case MutationVerb::ToggleRemixPermission:
        case MutationVerb::ToggleShowRemixes:
        case MutationVerb::UpdateFeedbackState:
        case MutationVerb::ShareLink:
            return true;
        case MutationVerb::PlaylistSetMetadata:
        case MutationVerb::PlaylistUpdateClips:
        case MutationVerb::PlaylistTracksAdd:
        case MutationVerb::PlaylistTracksRemove:
        case MutationVerb::PlaylistTracksReorder:
        case MutationVerb::PlaylistCoverImage:
            return false;
    }
    return false;
}

/// `update_type` arrives from QML as a string. The four members ARE captured, so
/// this map is evidence-backed — and an unrecognised string returns nullopt
/// rather than defaulting to one of them, because a silent `add` where the user
/// asked for something else is the worst available outcome.
[[nodiscard]] std::optional<PlaylistUpdateType> playlistUpdateTypeFrom(
        const QString& text)
{
    if (text == QLatin1String("add")) {
        return PlaylistUpdateType::Add;
    }
    if (text == QLatin1String("remove")) {
        return PlaylistUpdateType::Remove;
    }
    if (text == QLatin1String("remove_by_id")) {
        return PlaylistUpdateType::RemoveById;
    }
    if (text == QLatin1String("reorder")) {
        return PlaylistUpdateType::Reorder;
    }
    return std::nullopt;
}

} // namespace

vc::suno::SunoLibraryMutations* SunoBridge::ensureMutations()
{
    if (mutationService_ && s_client) {
        return mutationService_;
    }
    ensureMutationService();
    return mutationService_;
}

void SunoBridge::ensureMutationService()
{
    if (mutationService_ || !s_client) {
        return;
    }
    mutationService_ = new vc::suno::SunoLibraryMutations(s_client, this);
    // Borrowed, not owned, and it must follow the account: the resolver lives in
    // SunoAccountManager, which is replaced on sign-in. Re-pointing on every
    // wireControllerSignals() means a stale pointer is impossible, and a null
    // one (no account manager yet) fails closed inside the service.
    if (s_controller && s_controller->accountManager()) {
        mutationService_->setGateResolver(&s_controller->accountManager()->gates());
    }
    connect(mutationService_, &vc::suno::SunoLibraryMutations::availabilityChanged,
            this, &SunoBridge::onMutationsAvailabilityChanged);
    connect(mutationService_, &vc::suno::SunoLibraryMutations::mutationSettled,
            this, [this](const MutationResult& result) {
                onMutationSettled(static_cast<int>(result.verb), result.subjectId,
                                  static_cast<int>(result.outcome), result.reason);
            });
}

void SunoBridge::destroyMutationService()
{
    if (mutationService_) {
        disconnect(mutationService_, nullptr, this, nullptr);
        delete mutationService_;
        mutationService_ = nullptr;
        emit mutationsBusyChanged();
        emit mutationsAvailableChanged();
    }
}

void SunoBridge::onMutationsAvailabilityChanged()
{
    emit mutationsAvailableChanged();
    // The resolver's verdict moved, so `mutationsBusy` did not necessarily move
    // with it; this is only about the availability pair, and the busy signal is
    // driven by the service's own completion. Deliberately not emitted here —
    // a NOTIFY with no value change would make QML bindings re-read for nothing.
}

void SunoBridge::onMutationSettled(const int verb, const QString& subjectId, const int outcome,
                                   const QString& reason)
{
    const auto verbEnum = static_cast<MutationVerb>(verb);
    const auto outcomeEnum = static_cast<MutationOutcome>(outcome);
    const QString verbName = QString::fromLatin1(vc::suno::toString(verbEnum));
    const QString outcomeName = QString::fromLatin1(vc::suno::toString(outcomeEnum));

    if (isClipScoped(verbEnum)) {
        emit clipMutationSettled(subjectId, verbName, outcomeName, reason);
    } else {
        emit playlistMutationSettled(subjectId, verbName, outcomeName, reason);
    }

    if (outcomeEnum == MutationOutcome::Accepted) {
        // The one thing an `accepted` can honestly tell the UI: the library must
        // be re-read. Nothing was parsed out of the response and nothing was
        // written locally, so the displayed state is stale until a fetch.
        emit libraryRefreshRecommended();
    } else if (!reason.isEmpty()) {
        // A refusal or rejection also deserves a visible home, or the control
        // just silently does nothing. Reuses the existing single-error slot
        // rather than inventing a second status channel a UI would have to poll.
        setErrorMessage(reason);
    }
    emit mutationsBusyChanged();
}

bool SunoBridge::mutationsAvailable() const
{
    return mutationService_ && mutationService_->isAvailable();
}

QString SunoBridge::mutationsUnavailableReason() const
{
    if (!mutationService_) {
        return QStringLiteral(
                "Library changes need a signed-in Suno account.");
    }
    return mutationService_->unavailableReason();
}

bool SunoBridge::mutationsBusy() const
{
    return mutationService_ && mutationService_->isBusy();
}

// ── The invokables ───────────────────────────────────────────────────────────
// Each is `if (auto* m = ensureMutations()) m->…`, so a signed-out app gets the
// service's own one-sentence refusal on `clipMutationSettled` rather than a
// silent no-op. Returning the `[[nodiscard]] bool` is deliberate and ignored
// here: the result already arrived as a signal, so the bool carries no
// information the caller lacks — and discarding it here is safe precisely
// because the settlement path is unconditional.

void SunoBridge::setClipVisibility(const QString& clipId, const bool isPublic)
{
    if (auto* m = ensureMutations()) {
        (void)m->setClipVisibility(clipId, isPublic);
    }
}

void SunoBridge::setClipRemixPermission(const QString& clipId, const bool canRemix)
{
    if (auto* m = ensureMutations()) {
        (void)m->setClipRemixPermission(clipId, canRemix);
    }
}

void SunoBridge::setClipShowRemixes(const QString& clipId, const bool showRemix)
{
    if (auto* m = ensureMutations()) {
        (void)m->setClipShowRemixes(clipId, showRemix);
    }
}

void SunoBridge::setClipFeedback(const QString& clipId, const QString& reason)
{
    if (auto* m = ensureMutations()) {
        (void)m->setClipFeedbackState(clipId, reason);
    }
}

void SunoBridge::shareClip(const QString& clipId, const QString& contentType)
{
    if (auto* m = ensureMutations()) {
        (void)m->requestShareLink(clipId, contentType);
    }
}

void SunoBridge::setPlaylistMetadata(const QString& playlistId, const QString& name,
                                     const QString& description)
{
    if (auto* m = ensureMutations()) {
        (void)m->setPlaylistMetadata(playlistId, name, description);
    }
}

void SunoBridge::updatePlaylistClips(const QString& playlistId, const QString& updateType,
                                     const QVariantMap& metadata)
{
    auto* m = ensureMutations();
    if (!m) {
        return;
    }
    // An unrecognised updateType is refused by the service, which is where the
    // captured member list lives. Refusing here instead would duplicate that
    // list in a second file, and the two would drift.
    const auto type = playlistUpdateTypeFrom(updateType);
    if (!type) {
        (void)m->updatePlaylistClips(playlistId, PlaylistUpdateType::Add, {});
        return;
    }
    // QVariantMap -> QJsonObject, forwarded verbatim: nothing inside is named,
    // defaulted or interpreted, because the capture establishes only that
    // `metadata` is an object.
    QJsonObject json;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        json.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    (void)m->updatePlaylistClips(playlistId, *type, json);
}

void SunoBridge::addClipsToPlaylist(const QString& playlistId, const QStringList& clipIds)
{
    if (auto* m = ensureMutations()) {
        (void)m->addClipsToPlaylist(playlistId, clipIds);
    }
}

void SunoBridge::removeClipsFromPlaylist(const QString& playlistId, const QStringList& clipIds)
{
    if (auto* m = ensureMutations()) {
        (void)m->removeClipsFromPlaylist(playlistId, clipIds);
    }
}

void SunoBridge::reorderPlaylistTracks(const QString& playlistId,
                                       const QVariantList& positions)
{
    auto* m = ensureMutations();
    if (!m) {
        return;
    }
    // Elements forwarded as given. The capture says `positions` is an array and
    // nothing about its elements, so no element is validated or reshaped here.
    QJsonArray json;
    for (const QVariant& position : positions) {
        json.append(QJsonValue::fromVariant(position));
    }
    (void)m->reorderPlaylistTracks(playlistId, json);
}

void SunoBridge::setPlaylistCoverImage(const QString& playlistId, const QString& imageId)
{
    if (auto* m = ensureMutations()) {
        (void)m->setPlaylistCoverImage(playlistId, imageId);
    }
}

} // namespace qml_bridge
