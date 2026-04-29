#include "SystemBrowserAuth.hpp"
#include "suno/SunoEndpoints.hpp"
#include "core/Logger.hpp"
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QRandomGenerator>

namespace vc::ui {

SystemBrowserAuth::SystemBrowserAuth(QObject* parent)
    : QObject(parent)
    , server_(new QTcpServer(this))
    , timeoutTimer_(new QTimer(this))
{
    connect(server_, &QTcpServer::newConnection, this, &SystemBrowserAuth::onNewConnection);
    connect(timeoutTimer_, &QTimer::timeout, this, &SystemBrowserAuth::onTimeout);
    timeoutTimer_->setSingleShot(true);
}

SystemBrowserAuth::~SystemBrowserAuth() {
    cancel();
}

bool SystemBrowserAuth::isAuthenticating() const {
    return server_->isListening();
}

void SystemBrowserAuth::startAuth() {
    cancel();
    
    // Generate a random state for CSRF protection
    QByteArray stateBytes(16, 0);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(stateBytes.data()), stateBytes.size() / sizeof(quint32));
    state_ = stateBytes.toHex();
    
    // Try to listen on ports 8080-8090, or let OS choose
    // Suno/Clerk might whitelist specific ports, so prefer standard ones if possible
    QList<int> ports = {8080, 8888, 3000, 4200, 0}; 
    
    bool listening = false;
    for (int port : ports) {
        if (server_->listen(QHostAddress::LocalHost, port)) {
            port_ = server_->serverPort();
            listening = true;
            break;
        }
    }
    
    if (!listening) {
        LOG_ERROR("SystemBrowserAuth: Could not start local callback server");
        emit authFailed("Could not start local server");
        return;
    }
    
    LOG_INFO("SystemBrowserAuth: Listening on port {}", port_);
    
    // Construct the login URL with multiple Clerk redirect parameters to ensure capture
    // Clerk supports redirect_url, after_sign_in_url, and after_sign_up_url.
    // We use /sign-in which typically handles existing sessions by redirecting immediately.
    QString callbackUrl = QString("http://localhost:%1/callback").arg(port_);
    QString encodedCallback = QUrl::toPercentEncoding(callbackUrl);
    
    // We provide multiple variants to cover different Clerk/Suno routing logic
    QString loginUrl = QString::fromUtf8(vc::suno::endpoints::WEB_BASE.data(), static_cast<int>(vc::suno::endpoints::WEB_BASE.size()))
                       + QString::fromUtf8(vc::suno::endpoints::SIGN_IN.data(), static_cast<int>(vc::suno::endpoints::SIGN_IN.size()))
                       + QString("?redirect_url=%1&after_sign_in_url=%2&after_sign_up_url=%3")
                       .arg(encodedCallback, encodedCallback, encodedCallback);
    
    // Some implementations use 'next' or 'return_to'
    loginUrl += "&next=" + encodedCallback;
    
    LOG_INFO("SystemBrowserAuth: Opening URL {}", loginUrl.toStdString());
    
    timeoutTimer_->start(AUTH_TIMEOUT_MS);
    
    if (!QDesktopServices::openUrl(QUrl(loginUrl))) {
        cancel();
        emit authFailed("Could not open system browser");
    }
}

void SystemBrowserAuth::cancel() {
    timeoutTimer_->stop();
    if (server_->isListening()) {
        server_->close();
    }
}

void SystemBrowserAuth::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, &SystemBrowserAuth::onSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void SystemBrowserAuth::onSocketReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    
    // Read whatever is available
    QByteArray request = socket->readAll();
    // In a real scenario we might need to buffer until \r\n\r\n but for simple GETs this usually works
    
    handleAuthCallback(socket, request);
}

void SystemBrowserAuth::handleAuthCallback(QTcpSocket* socket, const QByteArray& request) {
    QString requestStr = QString::fromUtf8(request);
    
    // Parse the GET request line
    QRegularExpression re("GET ([^ ]+) HTTP");
    auto match = re.match(requestStr);
    
    if (!match.hasMatch()) {
        // Might be a partial read or invalid request
        return;
    }
    
    QString path = match.captured(1);
    // QUrl needs a scheme/host to parse the query correctly
    QUrl url("http://localhost" + path);
    QUrlQuery query(url);
    
    LOG_INFO("SystemBrowserAuth: Received callback path: {}", path.toStdString());
    
    // Check for error parameters
    if (query.hasQueryItem("error")) {
        QString error = query.queryItemValue("error");
        QString desc = query.queryItemValue("error_description");
        sendErrorPage(socket, error + ": " + desc);
        emit authFailed(error);
        return;
    }
    
    // Clerk usually returns a ticket or code
    // Or sometimes it sets a cookie on the redirect domain (which won't help us here unless we are the domain)
    // However, if we get a "token" or "session" param, we can use it.
    
    // Check for "token", "code", or "session"
    QString token;
    if (query.hasQueryItem("token")) token = query.queryItemValue("token");
    else if (query.hasQueryItem("code")) token = query.queryItemValue("code");
    else if (query.hasQueryItem("__session")) token = query.queryItemValue("__session");
    
    if (token.isEmpty()) {
        // If no token in URL, we might need to instruct the user to copy-paste
        // Or inject JS into the success page to read localStorage (not possible here as we are the server)
        sendErrorPage(socket, "No token received in callback. Suno might not support direct redirects.");
        emit authFailed("No token in callback");
        return;
    }
    
    sendSuccessPage(socket);
    cancel(); // Stop server
    
    emit authSuccess(token);
}

void SystemBrowserAuth::sendSuccessPage(QTcpSocket* socket) {
    QString html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Suno Authentication</title>
    <style>
        body { font-family: sans-serif; background: #1a1a1a; color: #fff; text-align: center; padding: 50px; }
        .success { color: #00ff88; font-size: 48px; margin-bottom: 20px; }
    </style>
</head>
<body>
    <div class="success">✓</div>
    <h1>Authentication Successful</h1>
    <p>You can close this tab and return to ChadVis ProjectM.</p>
    <script>setTimeout(window.close, 3000);</script>
</body>
</html>
)";

    QByteArray response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Connection: close\r\n"
                          "\r\n" + html.toUtf8();
    
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void SystemBrowserAuth::sendErrorPage(QTcpSocket* socket, const QString& error) {
    QString html = QString(R"(
<!DOCTYPE html>
<html>
<head>
    <title>Authentication Failed</title>
    <style>
        body { font-family: sans-serif; background: #1a1a1a; color: #fff; text-align: center; padding: 50px; }
        .error { color: #ff4444; font-size: 48px; margin-bottom: 20px; }
        p { color: #aaa; }
    </style>
</head>
<body>
    <div class="error">✗</div>
    <h1>Authentication Failed</h1>
    <p>%1</p>
</body>
</html>
)").arg(error.toHtmlEscaped());

    QByteArray response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Connection: close\r\n"
                          "\r\n" + html.toUtf8();
    
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void SystemBrowserAuth::onTimeout() {
    if (server_->isListening()) {
        LOG_WARN("SystemBrowserAuth: Timed out waiting for callback");
        cancel();
        emit authFailed("Authentication timed out");
    }
}

} // namespace vc::ui
