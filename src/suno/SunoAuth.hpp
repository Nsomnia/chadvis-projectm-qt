#pragma once

/**
 * @file SunoAuth.hpp
 * @brief Suno AI authentication utilities
 *
 * Handles Clerk-based authentication for Suno AI services.
 * Extracted from SunoClient for single responsibility.
 *
 * @section Features
 * - JWT token extraction from cookies
 * - Clerk session management
 * - Token refresh functionality
 * - Auto-refresh timer management
 */

#include <QObject>
#include <QTimer>
#include <QString>
#include <functional>
#include <chrono>

class QNetworkAccessManager;
class QNetworkReply;

namespace vc::suno {

struct AuthState {
    std::string token;
    std::string cookie;
    std::string clerkSid;
    std::string clerkVersion{"5.117.0"};
    std::chrono::steady_clock::time_point lastRefresh;
    bool isValid() const { return !token.empty() || !cookie.empty(); }
};

class SunoAuth : public QObject {
    Q_OBJECT

public:
    explicit SunoAuth(QNetworkAccessManager* manager, QObject* parent = nullptr);
    ~SunoAuth() override;

    void setCookie(const std::string& cookie);
    void setToken(const std::string& token);
    
    const AuthState& state() const { return state_; }
    std::string token() const { return state_.token; }
    std::string cookie() const { return state_.cookie; }
    bool isAuthenticated() const { return state_.isValid(); }
    
    void refresh(std::function<void(bool)> callback = nullptr);
    
    void startAutoRefresh(int intervalMs = DEFAULT_REFRESH_INTERVAL);
    void stopAutoRefresh();
    bool isAutoRefreshActive() const { return autoRefreshActive_; }

signals:
    void tokenChanged(const std::string& token);
    void sessionExpired();
    void refreshFailed(const std::string& error);

private slots:
    void onAutoRefreshTimer();

private:
    void extractTokenFromCookie();
    std::string extractSidFromToken(const std::string& token);
    void fetchSessionId(std::function<void(bool)> callback);
    void refreshToken(std::function<void(bool)> callback);

    QNetworkAccessManager* manager_;
    QTimer* autoRefreshTimer_;
    AuthState state_;
    bool autoRefreshActive_{false};
    
    static constexpr int DEFAULT_REFRESH_INTERVAL = 55 * 60 * 1000;
    static constexpr const char* CLERK_BASE = "https://clerk.suno.com/v1";
};

} // namespace vc::suno
