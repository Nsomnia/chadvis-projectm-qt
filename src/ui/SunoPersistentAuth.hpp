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
     * @brief Create a new web view using the managed persistent profile
     */
    QWebEngineView* createWebView(QWidget* parent = nullptr);

    /**
     * @brief Clear all cookies and session data
     */
    void clearSession();

    /**
     * @brief Sync cookies from persistent storage
     */
    void syncCookies();

    /**
     * @brief Navigate the view to the login page
     */
    void navigateToLogin(QWebEngineView* view);

    /**
     * @brief Navigate the view to Suno.com
     */
    void navigateToSuno(QWebEngineView* view);

    /**
     * @brief Set a bearer token directly
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
