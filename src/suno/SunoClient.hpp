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
#include "auth/AuthTypes.hpp"
#include "auth/ClerkAuthClient.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

namespace vc::suno {

namespace auth {
class CredentialStore;
}

class SunoClient : public QObject {
    Q_OBJECT

public:
    explicit SunoClient(QString deviceId = {}, QObject* parent = nullptr);
    ~SunoClient() override;

    // ── Credentials ─────────────────────────────────────────────────────
    /// Set/replace the captured Cookie header. Persists to CredentialStore
    /// ("suno/default") and kicks a bearer fetch when it changed.
    void setCookie(const std::string& cookie);
    /// Set a raw bearer JWT directly (e.g. pasted into settings).
    void setToken(const std::string& token);
    std::string getCookie() const { return credentials_.cookieHeader.toStdString(); }
    QString token() const { return bearer_.jwt; }
    bool isAuthenticated() const;

    /// Re-read secrets from CredentialStore (the settings panel may have
    /// replaced them) and apply. No-op when nothing changed.
    void reloadStoredCredentials();

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

    // Generation (v2/v3-web)
    void generate(const std::string& prompt, const std::string& tags,
                  bool makeInstrumental = false,
                  const std::string& model = "chirp-v3.5");

    /// Run an authenticated request through the rate-limiting queue.
    /// Waits for a bearer when only a cookie is available; a 401 response is
    /// retried exactly once behind the scenes. The callback owns the reply.
    void enqueueAuthenticatedRequest(const QString& endpoint,
                                     const std::string& method,
                                     const QByteArray& data,
                                     std::function<void(QNetworkReply*)> callback);

    QNetworkAccessManager* networkManager() { return manager_; }

    // ── Auth state (see auth::AuthState) ────────────────────────────────
    auth::AuthState authState() const { return authState_; }
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

private:
    struct PendingRequest {
        QNetworkRequest request;
        std::string method;
        QByteArray data;
        std::function<void(QNetworkReply*)> callback;
        bool retriedAuth = false; ///< Already given its single 401 retry.
    };

    // Startup
    void restoreSession();
    void migrateLegacyConfigCredentials(auth::CredentialStore& store);

    // Auth orchestration
    void setState(auth::AuthState state);
    void applyBearer(const auth::BearerToken& token);
    void scheduleProactiveRefresh();
    void ensureFreshBearer(bool force = false);
    void flushAuthWaiters();
    void dropPendingAuthWork(const QString& reason);
    void onBearerReadyInternal(const auth::BearerToken& token);
    void onClerkAuthFailedInternal(const QString& reason);
    bool hasCredentials() const;

    // Request plumbing
    QNetworkRequest createAuthenticatedRequest(const QString& endpoint);
    void enqueueRequest(QNetworkRequest req, const std::string& method,
                        QByteArray data,
                        std::function<void(QNetworkReply*)> callback,
                        bool retriedAuth = false);
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
    std::deque<PendingRequest> requestQueue_;
    QTimer* queueTimer_;

    // Auth subsystem
    auth::ClerkAuthClient* clerk_;
    auth::Credentials credentials_;
    auth::BearerToken bearer_;
    auth::AuthState authState_ = auth::AuthState::Disconnected;
    QString lastActiveSessionId_;
    QString deviceId_;
    QTimer* refreshTimer_;       ///< Proactive touch at expiry-minus-margin.
    bool touchInFlight_ = false; ///< One Clerk exchange at a time.

    // Uniform 401 handling
    std::deque<PendingRequest> retryQueue_;              ///< Intercepted on 401.
    std::vector<std::function<void()>> authWaiters_;     ///< withValidToken gate.

    // Cancellable wav polling
    QSet<QString> cancelledPolls_;

    // Feed pagination truth (from the last /feed/v3 envelope)
    QString nextCursor_;
    bool hasMore_ = false;

    const QString API_BASE = qstr(vc::suno::endpoints::API_BASE);
};

} // namespace vc::suno
