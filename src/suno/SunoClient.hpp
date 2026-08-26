#pragma once
// SunoClient.hpp - Suno AI API Client
// Handles authentication and data fetching

#include "SunoModels.hpp"
#include "SunoLyrics.hpp"
#include "SunoEndpoints.hpp"
#include "util/Result.hpp"
#include "util/Signal.hpp"
#include "util/Types.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>
#include <deque>
#include <functional>
#include <memory>

namespace vc::suno {

class SunoClient : public QObject {
    Q_OBJECT

public:
    explicit SunoClient(QObject* parent = nullptr);
    ~SunoClient() override;

    // Configuration
    void setToken(const std::string& token);
    void setCookie(const std::string& cookie);
    std::string getCookie() const { return cookie_; }
    QString token() const { return QString::fromStdString(token_); }
    bool isAuthenticated() const;

    // Refresh Bearer token using cookie (Clerk API)
    void refreshAuthToken(std::function<void(bool)> callback = nullptr);

    // API Methods
    void fetchLibrary(int page = 1);
    void fetchAlignedLyrics(const std::string& clipId);
    void initiateWavConversion(const std::string& clipId);
    void pollWavFile(const std::string& clipId, int maxAttempts = 60);

    // Generation (v2/v3-web)
    void generate(const std::string& prompt, const std::string& tags, bool makeInstrumental = false, const std::string& model = "chirp-v3.5");

    /// Run an authenticated request through the rate-limiting queue.
    /// Refreshes the token first when only a cookie is available.
    /// This method does NOT parse responses; the callback owns the reply.
    void enqueueAuthenticatedRequest(const QString& endpoint,
                                     const std::string& method,
                                     const QByteArray& data,
                                     std::function<void(QNetworkReply*)> callback);

    QNetworkAccessManager* networkManager() { return manager_; }

    // Signals
    Signal<const std::vector<SunoClip>&> libraryFetched;
    Signal<const std::vector<SunoClip>&> generationStarted; // returned clips with pending status
    Signal<std::string, std::string> alignedLyricsFetched;
    Signal<std::string, std::string> wavConversionReady;
    Signal<std::string> tokenChanged;
    Signal<std::string> errorOccurred;

private slots:
    void onLibraryReply(QNetworkReply* reply);
    void onGenerateReply(QNetworkReply* reply);

private:
    QNetworkRequest createAuthenticatedRequest(const QString& endpoint);
    void enqueueRequest(const QNetworkRequest& req,
                        const std::string& method,
                        const QByteArray& data,
                        std::function<void(QNetworkReply*)> callback);
    /// Refresh-then-proceed gate: run proceed() immediately with a valid token,
    /// otherwise refresh from cookie first (single copy of the pattern).
    void withValidToken(std::function<void()> proceed);
    /// Shared reply preamble: deleteLater + error routing + JSON parsing.
    /// The handler is only invoked on success; failures go to handleNetworkError.
    void handleJsonReply(QNetworkReply* reply,
                         std::function<void(const QJsonDocument&)> handler);
    static std::vector<SunoClip> parseClipArray(const QJsonArray& array);
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
    std::string token_;
    std::string cookie_;
    std::string clerkSid_;
    std::string clerkVersion_{vc::suno::endpoints::CLERK_VERSION};

    const QString API_BASE = qstr(vc::suno::endpoints::API_BASE);
    const QString CLERK_BASE = qstr(vc::suno::endpoints::CLERK_BASE);
};

} // namespace vc::suno
