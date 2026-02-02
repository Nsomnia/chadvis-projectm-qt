#pragma once

#include <QObject>
#include <QTcpServer>
#include <QUrl>
#include <QDesktopServices>
#include <QTcpSocket>
#include <QTimer>
#include <QPointer>

namespace chadvis {

class SystemBrowserAuth : public QObject {
    Q_OBJECT

public:
    explicit SystemBrowserAuth(QObject* parent = nullptr);
    ~SystemBrowserAuth();

    /**
     * @brief Start the authentication flow using the system browser
     * Opens suno.com/login with a redirect_uri pointing to localhost
     */
    void startAuth();
    
    /**
     * @brief Cancel any active authentication flow
     */
    void cancel();
    
    /**
     * @brief Check if authentication is currently in progress
     */
    bool isAuthenticating() const;

signals:
    /**
     * @brief Emitted when a session token/cookie is captured
     * @param cookieString The __session cookie value
     */
    void authSuccess(const QString& cookieString);
    
    /**
     * @brief Emitted when authentication fails or is cancelled
     */
    void authFailed(const QString& reason);

private slots:
    void onNewConnection();
    void onTimeout();
    void onSocketReadyRead();

private:
    void handleAuthCallback(QTcpSocket* socket, const QByteArray& request);
    void sendSuccessPage(QTcpSocket* socket);
    void sendErrorPage(QTcpSocket* socket, const QString& error);

    QTcpServer* server_{nullptr};
    QTimer* timeoutTimer_{nullptr};
    int port_{0};
    QString state_; // CSRF protection
    
    static constexpr int AUTH_TIMEOUT_MS = 180000; // 3 minutes
};

} // namespace chadvis
