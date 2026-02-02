/**
 * @file SunoPersistentAuth.hpp
 * @brief Persistent authentication manager for Suno AI
 *
 * Manages persistent browser sessions using Qt WebEngine's cookie storage.
 * Stores session data in the user's config directory to maintain login state
 * across application restarts.
 *
 * @author ChadVis Agent
 * @version 1.0.0
 */

#pragma once

#include <QObject>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <QWebEngineCookieStore>
#include <QTimer>
#include <memory>
#include <functional>

namespace chadvis {

/**
 * @brief Authentication state information
 */
struct SunoAuthState {
    QString sessionCookie;
    QString clientCookie;
    QString bearerToken;
    QString userId;
    bool isValid() const {
        return !sessionCookie.isEmpty() && !clientCookie.isEmpty();
    }
};

/**
 * @brief Persistent authentication manager for Suno
 *
 * This class manages a persistent WebEngine profile that stores cookies
 * in the user's config directory. This allows the Suno session to survive
 * application restarts without requiring the user to re-authenticate.
 */
class SunoPersistentAuth : public QObject {
    Q_OBJECT

public:
    explicit SunoPersistentAuth(QObject* parent = nullptr);
    ~SunoPersistentAuth() override;

    /**
     * @brief Initialize the persistent profile and storage
     */
    void initialize();

    /**
     * @brief Check if we have a valid authenticated session
     */
    bool isAuthenticated() const;

    /**
     * @brief Get current authentication state
     */
    SunoAuthState authState() const { return authState_; }

    /**
     * @brief Get the persistent profile for use in WebEngine views
     */
    QWebEngineProfile* profile() const { return profile_; }

    /**
     * @brief Create a new WebEngine view using the persistent profile
     */
    QWebEngineView* createWebView(QWidget* parent = nullptr);

    /**
     * @brief Navigate to Suno login page
     */
    void navigateToLogin(QWebEngineView* view);

    /**
     * @brief Navigate to Suno main page
     */
    void navigateToSuno(QWebEngineView* view);

    /**
     * @brief Clear all authentication data and cookies
     */
    void logout();

    /**
     * @brief Extract and sync cookies from the profile
     * 
     * This scans the cookie store for __session and __client cookies
     * and updates the auth state.
     */
    void syncCookies();

    /**
     * @brief Set the bearer token directly
     */
    void setBearerToken(const QString& token);

signals:
    /**
     * @brief Emitted when authentication is detected (cookies found)
     */
    void authenticated(const SunoAuthState& state);

    /**
     * @brief Emitted when authentication is lost or logout
     */
    void loggedOut();

    /**
     * @brief Emitted when auth state changes
     */
    void authStateChanged(const SunoAuthState& state);

    /**
     * @brief Status message for UI display
     */
    void statusMessage(const QString& message);

private slots:
    void onCookieAdded(const QNetworkCookie& cookie);
    void onCookieRemoved(const QNetworkCookie& cookie);
    void checkAuthStatus();

private:
    void setupPersistentStorage();
    void extractJwtFromCookie(const QString& cookieValue);
    QString getStoragePath() const;
    QString getCachePath() const;

    QWebEngineProfile* profile_ = nullptr;
    SunoAuthState authState_;
    QTimer* authCheckTimer_ = nullptr;
    bool initialized_ = false;
};

} // namespace chadvis
