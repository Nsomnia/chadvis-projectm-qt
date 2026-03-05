/**
 * @file TokenServer.hpp
 * @brief Local HTTP server for Chrome extension token communication
 *
 * Listens on 127.0.0.1:38945 for token pushes from browser extensions.
 * Thread-safe, non-blocking, integrates with SunoClient via signals.
 */

#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QByteArray>
#include <functional>
#include <atomic>
#include <mutex>

namespace vc::suno {

/**
 * @brief Local HTTP server that receives authentication tokens
 *        from browser extensions (like SunoSync Chrome Extension).
 *
 * Usage:
 *   TokenServer server;
 *   server.onToken([](const QString& token) { ... });
 *   server.start();
 */
class TokenServer : public QObject {
    Q_OBJECT

public:
    static constexpr const char* DEFAULT_HOST = "127.0.0.1";
    static constexpr quint16 DEFAULT_PORT = 38945;

    explicit TokenServer(QObject* parent = nullptr);
    ~TokenServer() override;

    bool start(const QString& host = DEFAULT_HOST, quint16 port = DEFAULT_PORT);
    void stop();

    bool isRunning() const { return running_.load(); }
    QString currentToken() const;

signals:
    void tokenReceived(const QString& token);
    void serverStarted(const QString& host, quint16 port);
    void serverStopped();
    void errorOccurred(const QString& error);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    void handleRequest(QTcpSocket* client);
    void sendResponse(QTcpSocket* client, int statusCode, const QByteArray& body);
    void sendCorsHeaders(QTcpSocket* client);

    QTcpServer* server_{nullptr};
    std::atomic<bool> running_{false};
    mutable std::mutex tokenMutex_;
    QString currentToken_;
};

} // namespace vc::suno
