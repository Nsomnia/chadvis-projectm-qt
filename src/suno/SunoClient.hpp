#pragma once
// SunoClient.hpp - Suno AI API Client
// Handles authentication and data fetching

#include "SunoModels.hpp"
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
    bool isAuthenticated() const;

    // Refresh Bearer token using cookie (Clerk API)
    void refreshAuthToken(std::function<void(bool)> callback = nullptr);

    // API Methods
    // Fetch songs from "My Library" (Feed)
    // page: 1-based index
    void fetchLibrary(int page = 1);

    // Fetch aligned lyrics for a clip
    void fetchAlignedLyrics(const std::string& clipId);

    // Fetch projects/workspaces
    void fetchProjects(int page = 1);

    // Fetch specific project clips
    void fetchProject(const std::string& projectId, int page = 1);

    // Signals
    Signal<const std::vector<SunoClip>&> libraryFetched;
    Signal<const std::vector<SunoProject>&> projectsFetched;
    Signal<std::string, std::string> alignedLyricsFetched; // clipId, json
    Signal<std::string> errorOccurred; // Error message

private slots:
    void onLibraryReply(QNetworkReply* reply);
    void onProjectsReply(QNetworkReply* reply);

private:
    QNetworkRequest createRequest(const QString& endpoint);
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
    std::string clerkVersion_{"5.15.0"};

    const QString API_BASE = "https://studio-api.suno.ai";
    const QString CLERK_BASE = "https://clerk.suno.com/v1";
};

} // namespace vc::suno
