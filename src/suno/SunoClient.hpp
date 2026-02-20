#pragma once

/**
 * @file SunoClient.hpp
 * @brief Suno AI API Client with automatic token refresh
 *
 * Handles authentication and data fetching for Suno AI services.
 * Implements automatic background token refresh to maintain persistent
 * sessions without requiring user intervention.
 *
 * @section AuthFlow Authentication Flow
 * 1. User provides cookie from browser (contains __session JWT)
 * 2. JWT extracted and used for API calls
 * 3. Background timer refreshes token every 55 minutes
 * 4. On 401 errors, automatic token refresh is attempted
 *
 * @section ThreadSafety Thread Safety
 * All public methods are safe to call from any thread.
 * Signals are emitted on the main thread via Qt::QueuedConnection.
 */

#include "SunoModels.hpp"
#include "SunoLyrics.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>

namespace vc::suno {

class SunoClient : public QObject {
    Q_OBJECT

public:
    explicit SunoClient(QObject* parent = nullptr);
    ~SunoClient() override;

    void setToken(const std::string& token);
    void setCookie(const std::string& cookie);
    std::string getCookie() const { return cookie_; }
    std::string getToken() const { return token_; }
    bool isAuthenticated() const;
    
    void startAutoRefresh();
    void stopAutoRefresh();
    bool isAutoRefreshActive() const { return autoRefreshActive_; }
    
    void refreshAuthToken(std::function<void(bool)> callback = nullptr);

    // API Methods
    void fetchLibrary(int page = 1);
    void fetchAlignedLyrics(const std::string& clipId);
    void initiateWavConversion(const std::string& clipId);
    void pollWavFile(const std::string& clipId, int maxAttempts = 60);
    void fetchProjects(int page = 1);
    void fetchProject(const std::string& projectId, int page = 1);

    Signal<const std::vector<SunoClip>&> libraryFetched;
    Signal<const std::vector<SunoProject>&> projectsFetched;
    Signal<std::string, std::string> alignedLyricsFetched;
    Signal<std::string, std::string> wavConversionReady;
    Signal<std::string> tokenChanged;
    Signal<std::string> errorOccurred;
    Signal<> sessionExpired;

private slots:
    void onLibraryReply(QNetworkReply* reply);
    void onProjectsReply(QNetworkReply* reply);
    void onAutoRefreshTimer();

private:
    QNetworkRequest createRequest(const QString& endpoint);
    QNetworkRequest createAuthenticatedRequest(const QString& endpoint);
    void enqueueRequest(const QNetworkRequest& req,
                        const std::string& method,
                        const QByteArray& data,
                        std::function<void(QNetworkReply*)> callback);
    void handleNetworkError(QNetworkReply* reply);
    void processQueue();
    std::string extractSidFromToken(const std::string& token);

    struct PendingRequest {
        QNetworkRequest request;
        std::string method;
        QByteArray data;
        std::function<void(QNetworkReply*)> callback;
    };

    QNetworkAccessManager* manager_;
    std::deque<PendingRequest> requestQueue_;
    QTimer* queueTimer_;
    QTimer* autoRefreshTimer_;
    std::string token_;
    std::string cookie_;
    std::string clerkSid_;
    std::string clerkVersion_{"5.117.0"};
    bool autoRefreshActive_{false};
    std::chrono::steady_clock::time_point lastRefreshTime_;
    
    static constexpr int TOKEN_REFRESH_INTERVAL_MS = 55 * 60 * 1000;
    static constexpr int TOKEN_MAX_AGE_MS = 60 * 60 * 1000;

    const QString API_BASE = "https://studio-api.prod.suno.com/api";
    const QString CLERK_BASE = "https://clerk.suno.com/v1";
};


} // namespace vc::suno
