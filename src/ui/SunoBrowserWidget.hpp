/**
 * @file SunoBrowserWidget.hpp
 * @brief Non-modal browser widget for Suno authentication
 *
 * Provides an embedded, non-blocking alternative to SunoCookieDialog.
 * Can be docked or used as a floating panel while the main UI remains interactive.
 *
 * @version 1.0.0
 */

#pragma once

#include <QWidget>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTimer>
#include <QNetworkCookie>
#include <QRegularExpression>

namespace chadvis {

/**
 * @brief Cookie data extracted from Suno authentication
 */
struct SunoCookieData {
    QString sessionCookie;
    QString clientCookie;
    QString bearerToken;
    bool isValid() const {
        return !sessionCookie.isEmpty() && !clientCookie.isEmpty();
    }
};

/**
 * @brief Non-modal browser widget for Suno authentication
 * 
 * This widget provides a non-blocking authentication flow using QWebEngineView.
 * Unlike SunoCookieDialog which is modal and blocks the UI, this widget can be:
 * - Embedded in the main window (e.g., in a dock or panel)
 * - Floated as a separate window
 * - Hidden/shown without disrupting the main UI
 */
class SunoBrowserWidget : public QWidget {
    Q_OBJECT

public:
    explicit SunoBrowserWidget(QWidget* parent = nullptr);
    ~SunoBrowserWidget() override;

    /**
     * @brief Navigate to Suno website
     */
    void navigateToSuno();

    /**
     * @brief Navigate to specific URL
     */
    void navigateToUrl(const QUrl& url);

    /**
     * @brief Get extracted cookie data
     */
    SunoCookieData cookieData() const { return cookieData_; }

    /**
     * @brief Check if authentication is complete
     */
    bool isAuthenticated() const { return cookieData_.isValid(); }

    /**
     * @brief Set embedded mode vs windowed mode
     * @param embedded true for panel-style, false for separate window
     */
    void setEmbeddedMode(bool embedded);

    /**
     * @brief Clear cookies and reset state
     */
    void clearAuthentication();

signals:
    /**
     * @brief Emitted when authentication is successful
     */
    void authenticated(const SunoCookieData& data);

    /**
     * @brief Emitted when user cancels authentication
     */
    void cancelled();

    /**
     * @brief Emitted when page load starts
     */
    void loadStarted();

    /**
     * @brief Emitted when page load completes
     */
    void loadFinished(bool success);

    /**
     * @brief Emitted when cookie extraction status changes
     */
    void statusMessage(const QString& message);

public slots:
    /**
     * @brief Extract cookies from browser
     */
    void extractCookies();

    /**
     * @brief Toggle between embedded and windowed mode
     */
    void toggleWindowMode();

private slots:
    void onLoadStarted();
    void onLoadFinished(bool success);
    void onLoadProgress(int progress);
    void onUrlChanged(const QUrl& url);
    void onCookieAdded(const QNetworkCookie& cookie);
    void checkForAuthCompletion();
    void onExtractClicked();
    void onClearClicked();
    void onReloadClicked();

private:
    void setupUI();
    void setupWebEngine();
    void extractTokensFromPage();
    bool isSunoDomain(const QUrl& url) const;
    void updateStatus(const QString& message);

    QWebEngineView* webView_;
    QWebEngineProfile* profile_;
    
    // UI Elements
    QVBoxLayout* mainLayout_;
    QHBoxLayout* toolbarLayout_;
    QLabel* statusLabel_;
    QProgressBar* progressBar_;
    QLineEdit* urlEdit_;
    QPushButton* extractButton_;
    QPushButton* clearButton_;
    QPushButton* reloadButton_;
    QPushButton* windowModeButton_;
    
    // State
    SunoCookieData cookieData_;
    bool embeddedMode_ = true;
    bool loadInProgress_ = false;
    QTimer* authCheckTimer_;
    
    // Cookie storage during extraction
    QString pendingSessionCookie_;
    QString pendingClientCookie_;
};

} // namespace chadvis
