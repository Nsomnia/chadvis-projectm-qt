#pragma once
// SunoClient.hpp - thin Suno API surface: rate-limited queue + endpoints.
//
// ALL auth knowledge lives in suno/auth/ (ClerkAuthClient, CredentialStore,
// JwtUtils, AuthHeaders). This class only orchestrates:
//   - restore/migrate credentials on startup (keychain, never TOML),
//   - keep a bearer fresh (proactive timer + uniform single 401 retry),
//   - stamp canonical studio-api headers onto outgoing requests.

#include "SunoModels.hpp"
#include "SunoLyrics.hpp"
#include "SunoEndpoints.hpp"
#include "HttpPolicy.hpp"
#include "auth/AuthTypes.hpp"
#include "auth/ClerkAuthClient.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <atomic>
#include <cstddef>
#include <deque>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QUrl;

namespace vc::suno {

namespace auth {
class CredentialStore;
}

/// Serializes potentially-blocking credential-store operations on one private
/// QThread owned by SunoClient. Restore requests are coalesced and every
/// completion is explicitly hopped back to the caller's QObject thread.
///
/// This class is public as a narrow, injectable test seam: tests can prove the
/// backend runs off-thread, the GUI hop is queued, and concurrent restores do
/// not double-read without touching the real macOS keychain.
class CredentialStoreWorker final {
public:
    enum class Operation {
        Restore,
        Store,
        Remove,
    };

    struct Request {
        Operation operation = Operation::Restore;
        QString key;
        QString secret;

        bool loadBearer = false;
        bool migrateLegacy = false;
        QString legacyCredential;
        bool legacyWasCookie = true;
    };

    struct Outcome {
        std::optional<QString> cookie;
        std::optional<QString> bearer;
        bool migratedLegacy = false;
        bool legacyMigrationFailed = false;
        bool legacyWasCookie = true;
        bool restoreDiscarded = false;
        bool reusedResult = false;
    };

    using Backend = std::function<Outcome(Request)>;
    using Completion = std::function<void(Outcome)>;

    CredentialStoreWorker(QObject* guiReceiver, Backend backend);
    ~CredentialStoreWorker();
    CredentialStoreWorker(const CredentialStoreWorker&) = delete;
    CredentialStoreWorker& operator=(const CredentialStoreWorker&) = delete;

    /// Returns true only when this call started the sole in-flight restore.
    /// Concurrent requests return false and reuse that restore.
    [[nodiscard]] bool requestRestore(Request request, Completion completion);

    /// Asynchronously upsert / remove a record. Operations are serialized with
    /// restore reads and never execute on guiReceiver's thread.
    void store(QString key, QString secret);
    void remove(QString key);

    [[nodiscard]] bool isRestoreInFlight() const { return restoreInFlight_; }
    void discardPendingRestore();

    [[nodiscard]] static Qt::ConnectionType completionConnectionType() {
        return kCompletionConnectionType;
    }

private:
    void enqueue(Request request);

    QObject* guiReceiver_;
    Backend backend_;
    std::unique_ptr<QThread> workerThread_;
    QObject* worker_ = nullptr;
    bool restoreInFlight_ = false;
    bool restoreDiscarded_ = false;
    quint64 restoreGeneration_ = 0;
    std::vector<Completion> restoreCompletions_;

    static constexpr Qt::ConnectionType kCompletionConnectionType = Qt::QueuedConnection;
};

class SunoClient : public QObject {
    Q_OBJECT

public:
    using ReplyFactory = std::function<QNetworkReply*(
            const QNetworkRequest&, const std::string&, const QByteArray&)>;

    explicit SunoClient(
            QString deviceId = {},
            QObject* parent = nullptr,
            CredentialStoreWorker::Backend credentialStoreBackend = {},
            ReplyFactory replyFactory = {});
    ~SunoClient() override;

    // ── Credentials ─────────────────────────────────────────────────────
    /// Set/replace the captured Cookie header. Persists to CredentialStore
    /// ("suno/default") and kicks a bearer fetch when it changed.
    void setCookie(const std::string& cookie);
    /// Set a raw bearer JWT directly (e.g. pasted into settings).
    void setToken(const std::string& token);
    std::string getCookie() const { return credentials_.cookieHeader.toStdString(); }
    QString configuredCredential() const { return configuredCredential_; }
    QString token() const { return bearer_.jwt; }
    bool isAuthenticated() const;

    /// Re-read secrets from CredentialStore (the settings panel may have
    /// replaced them) and apply. No-op when nothing changed.
    void reloadStoredCredentials();

    /// Local-only sign-out: clear both persisted credential records and stop
    /// refresh work. This never claims or performs a remote Suno logout.
    void clearLocalCredentials();

    // ── API Methods ─────────────────────────────────────────────────────
    /// POST /api/feed/v3 (captured contract): cursor-based library page.
    /// nullopt cursor = first page. Truth about exhaustion lands in
    /// nextCursor()/hasMorePages() once the reply parses.
    void fetchLibraryPage(std::optional<QString> cursor, int limit = 20,
                          const QString& searchText = {});
    /// Cursor state from the most recent feed reply.
    const QString& nextCursor() const { return nextCursor_; }
    bool hasMorePages() const { return hasMore_; }
    void fetchAlignedLyrics(const std::string& clipId);
    void initiateWavConversion(const std::string& clipId);
    void pollWavFile(const std::string& clipId, int maxAttempts = 60);
    /// Stop polling wav-conversion status for clipId (user navigated away).
    void cancelPoll(const std::string& clipId);

    void generate(const std::string& prompt, const std::string& tags,
                  bool makeInstrumental = false,
                  const std::string& model = "chirp-v3.5");

    /// Run an authenticated request through the rate-limiting queue.
    /// Waits for a bearer when only a cookie is available; a 401 response is
    /// retried exactly once behind the scenes. The callback owns the reply.
    void enqueueAuthenticatedRequest(const QString& endpoint,
                                     const std::string& method,
                                     const QByteArray& data,
                                     std::function<void(QNetworkReply*)> callback,
                                     bool retryOnUnauthorized = true);

    /// The ONE way to read the body of a reply this client issued.
    ///
    /// Every reply tracked here is armed with an `http::BodyReader` that is
    /// pumped from `readyRead`, so the bytes are moved out of Qt's buffer *as
    /// they arrive* and a body past the class cap aborts the transfer mid-stream
    /// rather than after the fact. This accessor then performs a final drain (a
    /// reply can deliver its last bytes with no `readyRead` of its own) and
    /// returns what the reader holds.
    ///
    /// It exists as a single accessor rather than leaving `reply->readAll()`
    /// available because the two are mutually exclusive on a pumped reply, and
    /// the failure is silent: **`readAll()` on a reply that has already been
    /// pumped returns an empty QByteArray**, which is indistinguishable from a
    /// server that sent nothing. A half-converted call site therefore produces
    /// "Explore returned invalid JSON" rather than an error, and there is no way
    /// to tell the two apart from the outside. Going through one accessor makes
    /// the empty-body path unreachable by construction rather than by discipline.
    ///
    /// The error string names the limit (e.g. "studio API response exceeded the
    /// 16777216 byte response cap (at least 16777217 bytes received)") because a
    /// body that breached its cap is *aborted*, so `reply->errorString()` says
    /// only "Operation canceled" -- which teaches a user nothing.
    ///
    /// Contract: repeatable while the reply is tracked (each call drains, then
    /// returns the same bytes). After untracking, a reply whose body breached its
    /// cap reports that diagnosis exactly once and then reports itself as unknown
    /// -- the diagnosis is consumed by the handler that surfaces it.
    [[nodiscard]] std::expected<QByteArray, QString> readTrackedBody(QNetworkReply* reply);

    /// Number of replies currently holding a pumped body buffer. Non-zero means
    /// up to `http::maxResponseBytes` is retained per reply, so this is the
    /// number that says whether a long-lived client is accumulating readers.
    [[nodiscard]] std::size_t trackedReaderCount() const { return bodyReaders_.size(); }

    /// Breaches carried past the teardown of a reply that finished, waiting to be
    /// reported by `readTrackedBody`. Each entry is a `PolicyFailure`, not a body,
    /// and dead keys are pruned on every untrack, so this cannot grow unbounded.
    [[nodiscard]] std::size_t carriedFailureCount() const { return bodyFailures_.size(); }

    QNetworkAccessManager* networkManager() { return manager_; }

    // ── Auth state (see auth::AuthState) ────────────────────────────────
    auth::AuthState authState() const { return authState_; }
    /// Last Clerk/storage failure classification, published on this QObject's
    /// thread. The atomic keeps diagnostic reads safe without exposing enum
    /// internals through the QML bridge.
    [[nodiscard]] auth::AuthFailureKind authFailureKind() const noexcept {
        return authFailureKind_.load(std::memory_order_acquire);
    }
    const QString& deviceId() const { return deviceId_; }

    // Custom signals for non-QObject consumers (managers use these).
    Signal<const std::vector<SunoClip>&> libraryFetched;
    Signal<const std::vector<SunoClip>&> generationStarted;
    Signal<std::string, std::string> alignedLyricsFetched;
    Signal<std::string, std::string> wavConversionReady;
    Signal<std::string> tokenChanged;
    Signal<std::string> errorOccurred;

signals:
    /// Touch/retry chain exhausted; user must supply fresh credentials.
    void needsReauth();
    void authStateChanged();
    /// Emitted when the last auth failure changes or clears. The value is
    /// intentionally not an enum argument; consumers map it to a stable string.
    void authFailureKindChanged();
    void credentialChanged();
    void credentialInvalidated();
    void credentialRestoreCompleted();

private:
    struct PendingRequest {
        QNetworkRequest request;
        std::string method;
        QByteArray data;
        std::function<void(QNetworkReply*)> callback;
        bool retriedAuth = false; ///< Already given its single 401 retry.
        bool retryOnUnauthorized = true;
        quint64 epoch = 0;
    };

    struct AuthWaiter {
        quint64 epoch = 0;
        std::function<void()> proceed;
    };

    // Startup
    enum class RestoreMode {
        Startup,
        Reload,
    };
    void restoreSession(RestoreMode mode);
    void applyRestoreResult(RestoreMode mode, CredentialStoreWorker::Outcome result);

    // Auth orchestration
    void setState(auth::AuthState state);
    void setAuthFailureKind(auth::AuthFailureKind kind);
    void applyBearer(const auth::BearerToken& token);
    void scheduleProactiveRefresh();
    void ensureFreshBearer(bool force = false);
    void flushAuthWaiters();
    void invalidateRequestEpoch(const QString& reason = {});
    void resetClerkClient();
    void dropPendingAuthWork(const QString& reason);
    void onBearerReadyInternal(const auth::BearerToken& token);
    void onClerkAuthFailedInternal(const QString& reason,
                                  auth::AuthFailureKind kind);
    bool hasCredentials() const;
    bool isTrackedReply(const QNetworkReply* reply) const;
    void trackReply(QNetworkReply* reply);
    void removeTrackedReply(QNetworkReply* reply);
    void abortTrackedReplies();

    // Bounded response bodies. The reader is armed from readyRead so the cap stops
    // the bytes *arriving*; the failure is carried out of the reader at untrack so
    // the one diagnostic that matters survives the teardown that destroys it.
    void armBodyReader(QNetworkReply* reply);
    void pumpTrackedBody(QNetworkReply* reply);
    void pruneCarriedFailures();

    // Request plumbing
    std::optional<QUrl> resolveStudioApiUrl(const QString& endpoint) const;
    std::optional<QNetworkRequest> createAuthenticatedRequest(
            const QUrl& url, const std::string& method, const QByteArray& data);
    void rejectAuthenticatedRequest(const QString& reason);
    void enqueueRequest(QNetworkRequest req, const std::string& method,
                        QByteArray data,
                        std::function<void(QNetworkReply*)> callback,
                        bool retriedAuth = false,
                        quint64 epoch = 0,
                        bool retryOnUnauthorized = true);
    void processQueue();
    void handleReplyFinished(QNetworkReply* reply, PendingRequest&& pending);
    void withValidToken(std::function<void()> proceed);
    void handleJsonReply(QNetworkReply* reply,
                         std::function<void(const QJsonDocument&)> handler);
    void handleNetworkError(QNetworkReply* reply);

    // Reply handlers (existing API surface)
    void onLibraryReply(QNetworkReply* reply);
    void onGenerateReply(QNetworkReply* reply);
    void onWavConversionInitiated(const std::string& clipId, QNetworkReply* reply);

    QNetworkAccessManager* manager_;
    ReplyFactory replyFactory_;
    std::deque<PendingRequest> requestQueue_;
    QTimer* queueTimer_;
    quint64 requestEpoch_ = 1;
    std::vector<QPointer<QNetworkReply>> activeReplies_;

    // One reader per tracked reply. Keyed by raw pointer and strictly shorter
    // lived than the reply: armed in trackReply(), released in
    // removeTrackedReply(), cleared wholesale in abortTrackedReplies().
    QHash<QNetworkReply*, http::BodyReader> bodyReaders_;

    /// A breach being carried past the teardown of the reader that detected it.
    struct CarriedBodyFailure {
        http::PolicyFailure failure;
        /// Set the moment `readTrackedBody` hands this diagnosis to a caller.
        ///
        /// This exists because of one **observed** ordering, not a suspected one.
        /// `PumpingReply::abort()` emits `finished()` synchronously, so on a breach
        /// the whole sequence runs inside one `feed()` call:
        ///
        ///   1. `readyRead` -> `pumpTrackedBody` -> reader refuses -> `reply->abort()`
        ///   2. `finished()` lands in `handleReplyFinished`, which arms its untrack
        ///      `qScopeGuard` and has NOT run it yet, then calls the callback
        ///   3. the callback calls `readTrackedBody`, which finds the reader STILL
        ///      PRESENT and serves the sticky failure from there -- diagnosis #1.
        ///      `bodyFailures_` is still empty at this point, so consuming it here
        ///      suppresses nothing.
        ///   4. the callback returns, the guard fires, and `removeTrackedReply`
        ///      re-inserts the very failure that was just reported
        ///   5. the next ask finds it and reports the identical sentence: #2.
        ///
        /// Instrumentation of exactly that path (`fprintf` probes in
        /// `readTrackedBody` and `removeTrackedReply`, since removed) printed:
        ///
        ///   ENTER carriedFromMap=0 readerPresent=1 / RETURN-sticky
        ///   removeTrackedReply INSERT carried failure
        ///   ENTER carriedFromMap=1 readerPresent=0 / RETURN-carried
        ///
        /// So the flag is set by step 3 and honoured by step 4. It lives here,
        /// beside the failure it describes, rather than in a parallel container so
        /// that the two cannot drift out of step.
        bool delivered{false};
    };

    /// Reply -> the breach to report for it, and whether it has been reported.
    ///
    /// The copy exists because the reader holding the sticky `PolicyFailure` is
    /// destroyed on untrack, and that failure is the ONLY thing that names the
    /// limit -- without it, a reply aborted precisely so the user could be told
    /// why reports nothing but "Operation canceled". Keyed on a raw pointer, so an
    /// entry is consumed by the first read that asks about its own reply (see
    /// `readTrackedBody`) and pruned when the key dies; it is never read twice and
    /// never outlives the reply it names.
    QHash<QNetworkReply*, CarriedBodyFailure> bodyFailures_;

    // Auth subsystem
    auth::ClerkAuthClient* clerk_;
    std::unique_ptr<CredentialStoreWorker> credentialStoreWorker_;
    auth::Credentials credentials_;
    QString configuredCredential_;
    auth::BearerToken bearer_;
    auth::AuthState authState_ = auth::AuthState::Disconnected;
    std::atomic<auth::AuthFailureKind> authFailureKind_{auth::AuthFailureKind::None};
    QString lastActiveSessionId_;
    QString deviceId_;
    QTimer* refreshTimer_;       ///< Proactive touch at expiry-minus-margin.
    bool touchInFlight_ = false; ///< One Clerk exchange at a time.
    bool refreshAfterRestore_ = false;
    quint64 authExchangeEpoch_ = 0;

    // Uniform 401 handling
    std::deque<PendingRequest> retryQueue_;
    std::vector<AuthWaiter> authWaiters_;

    // Cancellable wav polling
    QSet<QString> cancelledPolls_;

    // Feed pagination truth (from the last /feed/v3 envelope)
    QString nextCursor_;
    bool hasMore_ = false;

    const QString API_BASE = qstr(vc::suno::endpoints::API_BASE);
};

} // namespace vc::suno
