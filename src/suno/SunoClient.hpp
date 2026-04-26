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

    const QString API_BASE = QString::fromUtf8(vc::suno::endpoints::API_BASE.data(), static_cast<int>(vc::suno::endpoints::API_BASE.size()));
    const QString CLERK_BASE = QString::fromUtf8(vc::suno::endpoints::CLERK_BASE.data(), static_cast<int>(vc::suno::endpoints::CLERK_BASE.size()));
};

} // namespace vc::suno
