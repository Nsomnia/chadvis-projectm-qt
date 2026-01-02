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
    bool isAuthenticated() const;

    // API Methods
    // Fetch songs from "My Library" (Feed)
    // page: 1-based index
    void fetchLibrary(int page = 1);

    // Fetch projects/workspaces
    void fetchProjects(int page = 1);

    // Fetch specific project clips
    void fetchProject(const std::string& projectId, int page = 1);

    // Signals
    Signal<const std::vector<SunoClip>&> libraryFetched;
    Signal<const std::vector<SunoProject>&> projectsFetched;
    Signal<std::string> errorOccurred; // Error message

private slots:
    void onLibraryReply(QNetworkReply* reply);
    void onProjectsReply(QNetworkReply* reply);

private:
    QNetworkRequest createRequest(const QString& endpoint);
    void handleNetworkError(QNetworkReply* reply);

    QNetworkAccessManager* manager_;
    std::string token_;

    const QString API_BASE = "https://studio-api.prod.suno.com/api";
};

} // namespace vc::suno
