#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QVariantList>
#include <QString>
#include <cstddef>
#include "QmlSingletonBridge.hpp"

namespace vc {
namespace suno {
class SunoController;
class SunoClient;
class SunoExploreService;
class SunoNotificationService;
class SunoAudioUploadService;
}
}

namespace qml_bridge {

class SunoBridge : public QObject,
                   public QmlSingletonBridge<SunoBridge, SingletonPolicy::CachedUnparented> {

    friend class QmlSingletonBridge<SunoBridge, SingletonPolicy::CachedUnparented>;
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(int totalClips READ totalClips NOTIFY clipsChanged)
    Q_PROPERTY(bool hasMorePages READ hasMorePages NOTIFY hasMorePagesChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QVariantList chatHistory READ chatHistory NOTIFY chatHistoryChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(bool isAuthenticated READ isAuthenticated NOTIFY authenticationChanged)
    Q_PROPERTY(QString googleLoginState READ googleLoginState NOTIFY googleLoginStateChanged)
    Q_PROPERTY(QString googleLoginError READ googleLoginError NOTIFY googleLoginErrorChanged)
    Q_PROPERTY(QString authFailureKind READ authFailureKind NOTIFY authFailureKindChanged)
    Q_PROPERTY(bool googleLoginAvailable READ googleLoginAvailable NOTIFY googleLoginAvailableChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(int credits READ credits NOTIFY billingInfoChanged)
    Q_PROPERTY(QString planName READ planName NOTIFY billingInfoChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY accountInfoChanged)
    Q_PROPERTY(bool generationAvailable READ generationAvailable CONSTANT)
    Q_PROPERTY(QString generationStatus READ generationStatus NOTIFY generationStatusChanged)
    Q_PROPERTY(bool audioUploadBusy READ audioUploadBusy NOTIFY audioUploadChanged)
    Q_PROPERTY(int audioUploadProgress READ audioUploadProgress NOTIFY audioUploadChanged)
    Q_PROPERTY(QString audioUploadStatus READ audioUploadStatus NOTIFY audioUploadChanged)
    Q_PROPERTY(QString audioUploadError READ audioUploadError NOTIFY audioUploadChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString downloadStatus READ downloadStatus NOTIFY downloadStatusChanged)
    Q_PROPERTY(QVariantList discover READ discover NOTIFY discoverChanged)
    Q_PROPERTY(bool discoverLoading READ discoverLoading NOTIFY discoverLoadingChanged)
    Q_PROPERTY(bool discoverHasMore READ discoverHasMore NOTIFY discoverHasMoreChanged)
    Q_PROPERTY(bool discoverLoaded READ discoverLoaded NOTIFY discoverLoadedChanged)
    Q_PROPERTY(QString discoverError READ discoverError NOTIFY discoverErrorChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
    Q_PROPERTY(bool notificationsLoading READ notificationsLoading NOTIFY notificationsLoadingChanged)
    Q_PROPERTY(QString notificationsError READ notificationsError NOTIFY notificationsErrorChanged)

public:
    explicit SunoBridge(QObject* parent = nullptr);
    ~SunoBridge() override;
    static void setSunoController(vc::suno::SunoController* controller);

    bool loading() const;
    QVariantList clips() const;
    int totalClips() const;
    bool hasMorePages() const;
    int currentPage() const;
    QVariantList chatHistory() const;
    QString filterText() const { return filterText_; }
    void setFilterText(const QString& filter);
    int credits() const;
    QString planName() const;
    QString userName() const;
    bool isAuthenticated() const;
    QString googleLoginState() const;
    QString googleLoginError() const;
    QString authFailureKind() const;
    bool googleLoginAvailable() const;
    QVariantList models() const;
    bool generationAvailable() const;
    QString generationStatus() const;
    bool audioUploadBusy() const;
    int audioUploadProgress() const;
    QString audioUploadStatus() const;
    QString audioUploadError() const;
    QString statusMessage() const;
    QString errorMessage() const;
    QString downloadStatus() const;
    QVariantList discover() const;
    bool discoverLoading() const;
    bool discoverHasMore() const;
    bool discoverLoaded() const;
    QString discoverError() const;
    QVariantList notifications() const;
    int unreadCount() const;
    bool notificationsLoading() const;
    QString notificationsError() const;

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model);
    Q_INVOKABLE void uploadAudio(const QString& localFilePath);
    Q_INVOKABLE void refreshLibrary(int page = 1);
    Q_INVOKABLE void requestNextLibraryPage();
    Q_INVOKABLE void searchLibrary(const QString& searchText);
    Q_INVOKABLE void playClip(const QString& clipId);
    Q_INVOKABLE void sendChatMessage(const QString& message, const QString& workspaceId = {});
    Q_INVOKABLE void fetchChatHistory();
    Q_INVOKABLE void clearLoading();
    Q_INVOKABLE void beginGoogleSignIn();
    Q_INVOKABLE void cancelGoogleSignIn();
    Q_INVOKABLE void signOutSuno();
    Q_INVOKABLE void refreshAccount();
    Q_INVOKABLE void refreshDiscover();
    Q_INVOKABLE void loadMoreDiscover();
    Q_INVOKABLE void refreshNotifications();
    Q_INVOKABLE void markAllNotificationsRead();

signals:
    void loadingChanged();
    void clipsChanged();
    void hasMorePagesChanged();
    void currentPageChanged();
    void chatHistoryChanged();
    void generationStarted();
    void filterTextChanged();
    void billingInfoChanged();
    void accountInfoChanged();
    void authenticationChanged();
    void googleLoginStateChanged();
    void googleLoginErrorChanged();
    void authFailureKindChanged();
    void googleLoginAvailableChanged();
    void googleLoginCallbackReceived();
    void authenticationFailed(const QString& reason);
    void modelsChanged();
    void generationStatusChanged();
    void audioUploadChanged();
    void statusMessageChanged();
    void errorMessageChanged();
    void downloadStatusChanged();
    void discoverChanged();
    void discoverLoadingChanged();
    void discoverHasMoreChanged();
    void discoverLoadedChanged();
    void discoverErrorChanged();
    void notificationsChanged();
    void unreadCountChanged();
    void notificationsLoadingChanged();
    void notificationsErrorChanged();

private slots:
    void onLibraryUpdated();
    void onLibraryFetchFailed(const QString& reason);
    void onAuthenticationFailed(const QString& reason);
    void onDiscoverReset();
    void onDiscoverPageReady();
    void onDiscoverLoadingChanged();
    void onDiscoverHasMoreChanged();
    void onDiscoverFailed(const QString& reason);
    void onNotificationsChanged();
    void onUnreadCountChanged();
    void onNotificationsLoadingChanged();
    void onNotificationsErrorChanged(const QString& reason);

private:
    void wireControllerSignals();
    void updateFilteredClips();
    void updateModelCatalog();
    void setStatusMessage(const QString& message);
    void setErrorMessage(const QString& message);
    void setDownloadStatusForClip(const QString& clipId, const QString& message);
    void ensureExploreService();
    void destroyExploreService();
    void ensureNotificationService();
    void destroyNotificationService();
    void ensureAudioUploadService();
    void destroyAudioUploadService();
    void setDiscoverError(const QString& message);
    void setNotificationsError(const QString& message);
    QString clipTitle(const QString& clipId) const;
    void startLoadingWatchdog();
    void stopLoadingWatchdog();

    static vc::suno::SunoController* s_controller;
    static vc::suno::SunoClient* s_client;

    QVariantList clips_;
    QVariantList allClips_;
    QVariantList chatHistory_;
    QVariantList modelCatalog_;
    QVariantList discover_;
    QVariantList notifications_;
    QString filterText_;
    QString generationStatus_;
    QString statusMessage_;
    QString errorMessage_;
    QString downloadStatus_;
    QString discoverError_;
    QString notificationsError_;
    QString activeDownloadClipId_;
    vc::suno::SunoExploreService* exploreService_{nullptr};
    vc::suno::SunoNotificationService* notificationService_{nullptr};
    vc::suno::SunoAudioUploadService* audioUploadService_{nullptr};
    bool loading_{false};
    bool hasMorePages_{false};
    bool discoverLoading_{false};
    bool discoverHasMore_{false};
    bool discoverLoaded_{false};
    bool notificationsLoading_{false};
    int unreadCount_{0};
    bool controllerSignalsWired_{false};
    int currentPage_{1};
    std::size_t clientErrorConnectionId_{0};
    QTimer searchDebounce_;
    QString searchDebounceText_;
    QTimer loadingWatchdog_;
};

} // namespace qml_bridge
