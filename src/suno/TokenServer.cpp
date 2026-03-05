/**
 * @file TokenServer.cpp
 * @brief Local HTTP server implementation for browser extension communication
 */

#include "TokenServer.hpp"
#include "core/Logger.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>

namespace vc::suno {

TokenServer::TokenServer(QObject* parent)
    : QObject(parent)
{
}

TokenServer::~TokenServer() {
    stop();
}

bool TokenServer::start(const QString& host, quint16 port) {
    if (running_.load()) {
        LOG_WARN("TokenServer already running");
        return true;
    }

    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &TokenServer::onNewConnection);

    QHostAddress address(host);
    if (!server_->listen(address, port)) {
        LOG_ERROR("TokenServer failed to start: {}", server_->errorString().toStdString());
        emit errorOccurred(server_->errorString());
        return false;
    }

    running_.store(true);
    LOG_INFO("TokenServer started on {}:{}", host.toStdString(), port);
    emit serverStarted(host, port);
    return true;
}

void TokenServer::stop() {
    if (!running_.load()) return;

    server_->close();
    running_.store(false);
    LOG_INFO("TokenServer stopped");
    emit serverStopped();
}

QString TokenServer::currentToken() const {
    std::lock_guard<std::mutex> lock(tokenMutex_);
    return currentToken_;
}

void TokenServer::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QTcpSocket* client = server_->nextPendingConnection();
        connect(client, &QTcpSocket::readyRead, this, &TokenServer::onReadyRead);
        connect(client, &QTcpSocket::disconnected, this, &TokenServer::onClientDisconnected);
    }
}

void TokenServer::onReadyRead() {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    handleRequest(client);
}

void TokenServer::onClientDisconnected() {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (client) {
        client->deleteLater();
    }
}

void TokenServer::handleRequest(QTcpSocket* client) {
    QByteArray requestData = client->readAll();
    QString request(requestData);

    // Parse HTTP request
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) {
        sendResponse(client, 400, R"({"error":"Invalid request"})");
        return;
    }

    // Parse first line: METHOD PATH HTTP/1.1
    QStringList firstLine = lines[0].split(" ");
    if (firstLine.size() < 2) {
        sendResponse(client, 400, R"({"error":"Invalid request line"})");
        return;
    }

    QString method = firstLine[0];
    QString path = firstLine[1];

    // Handle CORS preflight
    if (method == "OPTIONS") {
        sendResponse(client, 204, "");
        return;
    }

    // GET /status - Health check
    if (method == "GET" && path == "/status") {
        sendResponse(client, 200, R"({"running":true,"app":"ChadVis"})");
        return;
    }

    // POST /token - Receive token
    if (method == "POST" && path == "/token") {
        // Find JSON body
        int bodyStart = requestData.indexOf("\r\n\r\n");
        if (bodyStart < 0) {
            sendResponse(client, 400, R"({"error":"No body"})");
            return;
        }

        QByteArray body = requestData.mid(bodyStart + 4);

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            sendResponse(client, 400, R"({"error":"Invalid JSON"})");
            return;
        }

        QJsonObject json = doc.object();
        QString token = json["token"].toString();

        if (token.isEmpty()) {
            sendResponse(client, 400, R"({"error":"No token provided"})");
            return;
        }

        // Store and emit
        {
            std::lock_guard<std::mutex> lock(tokenMutex_);
            currentToken_ = token;
        }

        LOG_INFO("Token received from browser extension ({} chars)", token.length());
        emit tokenReceived(token);

        sendResponse(client, 200, R"({"success":true})");
        return;
    }

    // Unknown path
    sendResponse(client, 404, R"({"error":"Not found"})");
}

void TokenServer::sendCorsHeaders(QTcpSocket* client) {
    client->write("Access-Control-Allow-Origin: *\r\n");
    client->write("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
    client->write("Access-Control-Allow-Headers: Content-Type\r\n");
}

void TokenServer::sendResponse(QTcpSocket* client, int statusCode, const QByteArray& body) {
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " ";

    switch (statusCode) {
        case 200: response += "OK"; break;
        case 201: response += "Created"; break;
        case 204: response += "No Content"; break;
        case 400: response += "Bad Request"; break;
        case 404: response += "Not Found"; break;
        case 500: response += "Internal Server Error"; break;
        default: response += "Unknown"; break;
    }
    response += "\r\n";

    response += "Content-Type: application/json\r\n";
    sendCorsHeaders(client);

    if (statusCode != 204 && !body.isEmpty()) {
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }

    response += "\r\n";

    if (statusCode != 204 && !body.isEmpty()) {
        response += body;
    }

    client->write(response);
    client->flush();
    client->disconnectFromHost();
}

} // namespace vc::suno
