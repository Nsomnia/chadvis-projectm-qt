#include "SunoBridge.hpp"
#include "ui/controllers/SunoController.hpp"
#include "suno/SunoAccountManager.hpp"
#include "suno/SunoClient.hpp"
#include "suno/SunoLibraryManager.hpp"
#include "suno/SunoModels.hpp"
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>

namespace qml_bridge {

vc::suno::SunoController* SunoBridge::s_controller = nullptr;
vc::suno::SunoClient* SunoBridge::s_client = nullptr;

SunoBridge::SunoBridge(QObject* parent) : QObject(parent) {
    setInstance(this);
    if (s_controller) {
        wireControllerSignals();
    }

    // 350 ms debounce for server-side library search (QML may fire per
    // keystroke; we coalesce before hitting feed/v3).
    searchDebounce_.setSingleShot(true);
    searchDebounce_.setInterval(350);
    connect(&searchDebounce_, &QTimer::timeout, this, [this]() {
        if (!s_controller) return;
        loading_ = true;
        emit loadingChanged();
        startLoadingWatchdog();
        s_controller->libraryManager()->setSearchText(searchDebounceText_);
        s_controller->libraryManager()->refreshLibrary(1);
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
            bridgeInstance->controllerSignalsWired_ = false;
        }
    }

    s_controller = controller;
    s_client = s_controller ? s_controller->client() : nullptr;
    if (bridgeInstance) {
        bridgeInstance->wireControllerSignals();
    }
}

void SunoBridge::wireControllerSignals() {
    if (controllerSignalsWired_ || !s_controller) {
        return;
    }

    s_client = s_controller->client();
    auto* bridgeInstance = this;

    connect(s_controller, &vc::suno::SunoController::libraryUpdated,
            bridgeInstance, &SunoBridge::onLibraryUpdated);

    // Any terminal failure must clear the spinner: auth guards,
    // 401 exhaustion, and generic network errors all funnel here.
    connect(s_controller, &vc::suno::SunoController::authenticationRequired,
            bridgeInstance, &SunoBridge::clearLoading);
    connect(s_controller, &vc::suno::SunoController::authenticationFailed,
            bridgeInstance, &SunoBridge::clearLoading);
    connect(s_controller, &vc::suno::SunoController::libraryFetchFailed,
            bridgeInstance, &SunoBridge::onLibraryFetchFailed);
    connect(s_controller, &vc::suno::SunoController::sunoError,
            bridgeInstance, &SunoBridge::onLibraryFetchFailed);

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
                bridgeInstance, &SunoBridge::authenticationChanged);
        // Custom Signal<> for generic errors: queued clear so it
        // arrives on the bridge's thread even if emitted off-thread.
        clientErrorConnectionId_ = s_client->errorOccurred.connect([bridgeInstance](const std::string& err) {
            QMetaObject::invokeMethod(bridgeInstance,
                [bridgeInstance, err]() { bridgeInstance->onLibraryFetchFailed(QString::fromStdString(err)); },
                Qt::QueuedConnection);
        });
    }

    // Account snapshot -> QML properties.
    if (auto* am = s_controller->accountManager()) {
        connect(am, &vc::suno::SunoAccountManager::billingInfoReady,
                bridgeInstance, &SunoBridge::billingInfoChanged);
        connect(am, &vc::suno::SunoAccountManager::accountInfoReady,
                bridgeInstance, &SunoBridge::accountInfoChanged);
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

void SunoBridge::setFilterText(const QString& filter) {
    if (filterText_ == filter) return;
    filterText_ = filter;
    updateFilteredClips();
    emit filterTextChanged();
}

void SunoBridge::generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model) {
    if (s_client) {
        s_client->generate(prompt.toStdString(), tags.toStdString(), instrumental, model.toStdString());
        emit generationStarted();
    }
}

void SunoBridge::refreshLibrary(int page) {
    if (s_controller) {
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
    loading_ = true;
    emit loadingChanged();
    startLoadingWatchdog();
    lm->requestNextPage();
}

void SunoBridge::searchLibrary(const QString& searchText) {
    searchDebounceText_ = searchText;
    searchDebounce_.start(); // debounced in the constructor's timeout lambda
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
    QVariantMap userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = message;
    chatHistory_.append(userMsg);
    emit chatHistoryChanged();

    if (s_controller) {
        s_controller->sendChatMessage(message, workspaceId);
    }
}

void SunoBridge::fetchChatHistory() {
    if (s_controller) {
        s_controller->fetchChatHistory();
    }
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
    if (!s_controller || !s_controller->client()) return false;
    return s_controller->client()->authState() == vc::suno::auth::AuthState::ActiveValid;
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
    Q_UNUSED(reason);
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
        QString filter = filterText_.toLower();
        for (const auto& clipVar : allClips_) {
            QVariantMap clip = clipVar.toMap();
            QString title = clip["title"].toString().toLower();
            QString tags = clip["metadata"].toMap()["tags"].toString().toLower();
            
            if (title.contains(filter) || tags.contains(filter)) {
                clips_.append(clip);
            }
        }
    }
    emit clipsChanged();
}

} // namespace qml_bridge
