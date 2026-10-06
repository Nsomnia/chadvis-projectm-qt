#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <QString>
#include <cstddef>
#include "QmlSingletonBridge.hpp"

namespace vc {
namespace suno {
class SunoController;
class SunoClient;
class SunoDownloader;
class SunoExploreService;
class SunoNotificationService;
class SunoAudioUploadService;
class SunoLibraryMutations;
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
    // Was CONSTANT, which was honest only while the getter was a literal
    // `return false`. The verdict now depends on the server flag map that
    // /api/session/ returns, and that arrives after the bridge exists — so
    // CONSTANT would freeze the first (empty-map) answer and the fix would be
    // invisible to every QML binding. NOTIFY is required for the change to be
    // observable at all.
    Q_PROPERTY(bool generationAvailable READ generationAvailable NOTIFY generationAvailableChanged)
    Q_PROPERTY(QString generationUnavailableReason READ generationUnavailableReason NOTIFY generationAvailableChanged)
    /// Every gated surface with its verdict and the reason it is held.
    ///
    /// This exists because `GateResolver::evaluateAll()` had **no caller** — it
    /// was written for a diagnostics surface that did not exist, which made it
    /// the most obvious first consumer and the least likely to be noticed as
    /// dead. It is the honest answer to "why can't I use that": one row per
    /// product surface, each naming the specific step of the six-step resolution
    /// order holding it, instead of a silent omission.
    ///
    /// One `QVariantMap` per gate: `gate`, `title`, `area`, `status`,
    /// `available`, `evidence`, `serverFlags`, `note`, `reason`. `status` is the
    /// machine-readable verdict (`available` / `server-gated` /
    /// `locally-disabled` / `evidence-blocked` / `excluded`) and `reason` is the
    /// user-facing sentence, so a UI can filter on one and display the other.
    Q_PROPERTY(QVariantList gateStatuses READ gateStatuses NOTIFY gateStatusesChanged)
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

    /// Can this build change the library at all? Both layers must permit: the
    /// client switch inside `SunoLibraryMutations` (off until a success response
    /// is captured) and the owning `FeatureGate` verdict.
    ///
    /// Exists so a UI shows an *explained* disabled control. `clipSaved`/
    /// `clipSaveRefused` established the per-subject result channel; this is its
    /// read-only counterpart and it is what stops the mutation buttons from
    /// being a well-built door to nowhere.
    Q_PROPERTY(bool mutationsAvailable READ mutationsAvailable NOTIFY mutationsAvailableChanged)
    /// Names which of the two layers refused, and why. Never a bare "disabled".
    Q_PROPERTY(QString mutationsUnavailableReason READ mutationsUnavailableReason NOTIFY mutationsAvailableChanged)
    /// True while at least one mutation is waiting for its reply.
    Q_PROPERTY(bool mutationsBusy READ mutationsBusy NOTIFY mutationsBusyChanged)

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
    /// Why generation is unavailable, naming which resolution step is holding it.
    /// Exists so the disabled Generate button can explain itself instead of
    /// being an unexplained dead control.
    QString generationUnavailableReason() const;
    QVariantList gateStatuses() const;
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
    bool mutationsAvailable() const;
    QString mutationsUnavailableReason() const;
    bool mutationsBusy() const;

public slots:
    Q_INVOKABLE void generate(const QString& prompt, const QString& tags, bool instrumental, const QString& model);
    Q_INVOKABLE void uploadAudio(const QString& localFilePath);
    Q_INVOKABLE void refreshLibrary(int page = 1);
    Q_INVOKABLE void requestNextLibraryPage();
    Q_INVOKABLE void searchLibrary(const QString& searchText);
    Q_INVOKABLE void playClip(const QString& clipId);
    Q_INVOKABLE void downloadClip(const QString& clipId);
    Q_INVOKABLE void downloadClips(const QStringList& clipIds);
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

    // ── Library & playlist mutations ──────────────────────────────────────
    // Request bodies are built by `SunoLibraryMutations` from the captured
    // contracts in docs/suno_api/ENDPOINT-INVENTORY.md §5.9. These are thin
    // forwarders: no body is assembled here, so there is exactly one owner of
    // the wire format.
    //
    // **There are deliberately no like/unlike and no trash/restore invokables.**
    // No captured request contract exists for either — see the header of
    // SunoLibraryMutations.hpp and the named-absence block in
    // SunoEndpoints.hpp. Adding one would require inventing a route.
    //
    // Every one of these is a no-op that settles as `refused` while
    // `mutationsAvailable` is false, and each says why. Note that none of them
    // returns a result synchronously: the per-subject signals below are the
    // result channel, for the same reason `clipSaved` exists.
    Q_INVOKABLE void setClipVisibility(const QString& clipId, bool isPublic);
    Q_INVOKABLE void setClipRemixPermission(const QString& clipId, bool canRemix);
    Q_INVOKABLE void setClipShowRemixes(const QString& clipId, bool showRemix);
    /// `feedbackReason` is passed through verbatim. The capture establishes the
    /// field's type and no member list, so nothing here validates it.
    Q_INVOKABLE void setClipFeedback(const QString& clipId, const QString& reason);
    /// Mints a share link server-side. **Nothing reads a URL out of the
    /// response**, because no success response was ever captured — so this
    /// reports that the request was accepted and the UI must not present a link.
    Q_INVOKABLE void shareClip(const QString& clipId, const QString& contentType);
    Q_INVOKABLE void setPlaylistMetadata(const QString& playlistId, const QString& name,
                                         const QString& description);
    /// `updateType` is one of the four captured enum members, as a string:
    /// `"add"`, `"remove"`, `"remove_by_id"`, `"reorder"`. Anything else is
    /// refused rather than guessed.
    ///
    /// `metadata` is forwarded verbatim. Its contents were **not** captured, so
    /// no key inside it is named or defaulted here.
    Q_INVOKABLE void updatePlaylistClips(const QString& playlistId, const QString& updateType,
                                         const QVariantMap& metadata);
    Q_INVOKABLE void addClipsToPlaylist(const QString& playlistId, const QStringList& clipIds);
    Q_INVOKABLE void removeClipsFromPlaylist(const QString& playlistId, const QStringList& clipIds);
    Q_INVOKABLE void reorderPlaylistTracks(const QString& playlistId, const QVariantList& positions);
    Q_INVOKABLE void setPlaylistCoverImage(const QString& playlistId, const QString& imageId);

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
    /// The generation gate verdict (and therefore the reason string) may have
    /// changed, because the server flag map arrived or the account went away.
    void generationAvailableChanged();
    /// The full gate verdict list may have changed (session flag map arrived, or
    /// the account went away). Separate from `generationAvailableChanged` so a
    /// diagnostics surface does not have to couple to one gate's verdict.
    void gateStatusesChanged();
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
    /// One clip's audio is on disk, tagged and sidecarred. The per-clip result
    /// channel for a download: unlike downloadStatus, which is a single string
    /// describing the most recent event, this fires for every clip of a batch
    /// and carries the path that was written.
    void clipSaved(const QString& clipId, const QString& savedPath);
    /// One clip was REFUSED, with the reason, carrying the id.
    ///
    /// The counterpart `clipSaved` was missing. Without it a batch refusal
    /// collapses into a single aggregate error string with no id attached, so a
    /// UI cannot tell which clips failed and every rejected card sits on
    /// "Saving" forever while one of them silently did nothing. `reason` is the
    /// download layer's own sentence — including the server's download
    /// entitlement decision — passed through verbatim rather than summarised.
    void clipSaveRefused(const QString& clipId, const QString& reason);

    // ── Mutation result channel ───────────────────────────────────────────
    // ONE signal per subject, carrying the id, mirroring `clipSaved` /
    // `clipSaveRefused`. A batch of 40 clips added to a playlist produces one
    // `playlistMutationSettled` carrying the playlist id and an outcome naming
    // what happened to the batch; the per-clip verbs produce one per clip. There
    // is deliberately NO aggregate "N of M failed" string, for the reason
    // recorded on `clipSaveRefused`: an unattributable summary leaves every
    // failing card stuck while one of them silently did nothing.

    /// One clip-scoped mutation reached a terminal state.
    /// `verb` is the machine-readable name (`set-visibility`, `toggle-remixes`,
    /// ...); `outcome` is `accepted` / `refused` / `rejected` / `failed`.
    ///
    /// **`accepted` means the server returned 2xx. It does NOT mean the clip's
    /// state is now what was asked for** — no success response body has ever
    /// been captured, so nothing was read from it and nothing was written
    /// locally. A UI must re-read the library rather than assume.
    void clipMutationSettled(const QString& clipId, const QString& verb,
                             const QString& outcome, const QString& reason);
    /// The same, for the six playlist-scoped verbs, keyed by playlist id.
    void playlistMutationSettled(const QString& playlistId, const QString& verb,
                                 const QString& outcome, const QString& reason);
    /// Fired alongside an `accepted` result. The honest consequence of not
    /// knowing the server's response shape: the only way to learn the real state
    /// is to re-read the library, and a UI should be told to do it rather than
    /// left showing a value it guessed.
    void libraryRefreshRecommended();
    void mutationsAvailableChanged();
    void mutationsBusyChanged();

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
    /// Owns the one `SunoLibraryMutations`. Created lazily like the other
    /// services, and **destroyed on sign-out** for the same reason the
    /// notification service is: an object that outlives the account must not
    /// keep a resolver pointer into a destroyed `SunoAccountManager`.
    void ensureMutationService();
    void destroyMutationService();
    /// Re-publishes `mutationsAvailable` after the resolver's verdict moves.
    void onMutationsAvailabilityChanged();
    /// The single place a settled mutation becomes a QML signal. Routes by verb
    /// so a clip id and a playlist id never land on the same signal, and pairs
    /// every `accepted` with `libraryRefreshRecommended`.
    ///
    /// Takes primitives rather than `vc::suno::MutationResult` so this header
    /// keeps its convention of forward-declaring the `vc::suno` types instead of
    /// including them. The one `MutationResult` is read in the lambda that
    /// connects the service signal.
    void onMutationSettled(int verb, const QString& subjectId, int outcome,
                           const QString& reason);
    /// Invoked by the mutation invokables. Ensures the service exists and then
    /// reports the one reason nothing can be sent, so a QML call on a signed-out
    /// app produces a sentence instead of silence.
    [[nodiscard]] vc::suno::SunoLibraryMutations* ensureMutations();
    void downloadInto(const QStringList& clipIds);
    /// The single SunoDownloader, borrowed from SunoController. Null (and the
    /// caller must fail closed) when no controller is attached.
    vc::suno::SunoDownloader* ensureDownloader();
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
    /// The one `SunoLibraryMutations`, or null. Null is the fail-closed answer
    /// and is also what a signed-out app sees.
    vc::suno::SunoLibraryMutations* mutationService_{nullptr};
    /// Not owned here. Re-resolved from the QObject tree by ensureDownloader()
    /// whenever the controller changes, so a controller swap cannot leave a
    /// dangling pointer or a stale signal connection behind.
    vc::suno::SunoDownloader* downloader_{nullptr};
    /// The clip whose download was requested as "save and play". Kept apart
    /// from activeDownloadClipId_, which is the single status slot and is
    /// latched by any event: a save-only request must never look like a play
    /// request, or a batch would report the first clip it latches as "now
    /// playing" and then filter out the rest of its own results.
    QString playRequestClipId_;
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
