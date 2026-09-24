#include "LoopbackListener.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include <limits>

namespace vc::suno::auth::oauth {
namespace {

constexpr auto kSuccessResponse =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 121\r\n"
    "Connection: close\r\n"
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "\r\n"
    "<!doctype html><meta charset=utf-8><title>Sign-in complete</title>"
    "<p>You can close this window and return to the app.</p>";

bool isHeaderName(const QByteArray& name) {
    if (name.isEmpty()) return false;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte >= 0x7f || c == ':') return false;
    }
    return true;
}

QByteArray trimOws(const QByteArray& value) {
    qsizetype first = 0;
    qsizetype last = value.size();
    while (first < last && (value.at(first) == ' ' || value.at(first) == '\t')) ++first;
    while (last > first && (value.at(last - 1) == ' ' || value.at(last - 1) == '\t')) --last;
    return value.mid(first, last - first);
}

} // namespace

LoopbackListener::LoopbackListener(QObject* parent)
    : QObject(parent), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::pendingConnectionAvailable,
            this, &LoopbackListener::acceptPendingConnections);
}

std::expected<void, QString> LoopbackListener::start(const QString& expectedPath) {
    if (expectedPath.isEmpty() || !expectedPath.startsWith(QLatin1Char('/'))) {
        return std::unexpected(QStringLiteral("Callback path is invalid"));
    }

    stop();
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        return std::unexpected(QStringLiteral("Unable to start local callback listener"));
    }

    expectedPath_ = expectedPath;
    port_ = server_->serverPort();
    return {};
}

void LoopbackListener::stop() {
    server_->close();
}

void LoopbackListener::acceptPendingConnections() {
    while (server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        if (!socket) continue;
        requestBuffers_.insert(socket, {});
        completedSockets_.remove(socket);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { readFromSocket(); });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, socket] { finishSocket(socket); });
    }
}

void LoopbackListener::readFromSocket() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    auto it = requestBuffers_.find(socket);
    if (it == requestBuffers_.end()) return;
    if (completedSockets_.contains(socket)) return;

    it.value().append(socket->read(kMaxHeaderBytes + 1));
    const qsizetype headerEnd = it.value().indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (it.value().size() > kMaxHeaderBytes) {
            rejectAndClose(socket, QStringLiteral("Callback request is too large"));
        }
        return;
    }
    if (headerEnd + 4 > kMaxHeaderBytes) {
        rejectAndClose(socket, QStringLiteral("Callback request is too large"));
        return;
    }

    const QByteArray headerBlock = it.value().left(headerEnd + 4);
    QUrl callbackUrl;
    QString safeReason;
    if (!parseRequest(headerBlock, &callbackUrl, &safeReason)) {
        rejectAndClose(socket, safeReason);
        return;
    }

    completedSockets_.insert(socket);
    requestBuffers_.remove(socket);
    emit requestReceived(callbackUrl);

    socket->write(kSuccessResponse);
    socket->flush();
    socket->disconnectFromHost();
    if (socket->bytesToWrite() == 0) socket->close();
}

bool LoopbackListener::parseRequest(const QByteArray& headerBlock, QUrl* callbackUrl,
                                    QString* safeReason) const {
    const auto fail = [safeReason](const char* reason) {
        *safeReason = QString::fromLatin1(reason);
        return false;
    };

    // Include the final header line's CRLF, but omit the blank-line CRLF.
    const QByteArray linesBlock = headerBlock.left(headerBlock.size() - 2);
    QList<QByteArray> rawLines = linesBlock.split('\n');
    if (!rawLines.isEmpty() && rawLines.last().isEmpty()) rawLines.removeLast();
    if (rawLines.isEmpty() || !rawLines.first().endsWith('\r')) {
        return fail("Callback request is malformed");
    }

    QByteArray requestLine = rawLines.first();
    requestLine.chop(1);
    if (requestLine.size() > kMaxRequestLineBytes) {
        return fail("Callback request is too large");
    }

    const qsizetype firstSpace = requestLine.indexOf(' ');
    const qsizetype secondSpace = firstSpace < 0 ? -1 : requestLine.indexOf(' ', firstSpace + 1);
    if (firstSpace <= 0 || secondSpace <= firstSpace + 1 ||
        requestLine.indexOf(' ', secondSpace + 1) >= 0) {
        return fail("Callback request is malformed");
    }

    const QByteArray method = requestLine.left(firstSpace);
    const QByteArray target = requestLine.mid(firstSpace + 1, secondSpace - firstSpace - 1);
    const QByteArray version = requestLine.mid(secondSpace + 1);
    if (method != "GET") return fail("Callback request method was rejected");
    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        return fail("Callback request is malformed");
    }
    if (target.isEmpty() || target.size() > kMaxUrlLengthBytes || target == "*" ||
        !target.startsWith('/') || target.startsWith("//") || target.contains('#')) {
        return fail("Callback request target was rejected");
    }

    const qsizetype queryStart = target.indexOf('?');
    const QByteArray rawPath = target.left(queryStart);
    if (rawPath != expectedPath_.toUtf8()) return fail("Callback request path was rejected");

    const QUrl relativeUrl = QUrl::fromEncoded(target);
    if (!relativeUrl.isValid() || relativeUrl.hasFragment()) {
        return fail("Callback request target was rejected");
    }
    const QUrlQuery query(relativeUrl.query(QUrl::FullyEncoded));
    QSet<QString> queryKeys;
    const auto items = query.queryItems(QUrl::FullyDecoded);
    for (const auto& item : items) {
        if (queryKeys.contains(item.first)) return fail("Callback query was rejected");
        queryKeys.insert(item.first);
    }

    bool hasHost = false;
    bool hasContentLength = false;
    const QByteArray ipv4Host = "127.0.0.1:" + QByteArray::number(port_);
    const QByteArray ipv6Host = "[::1]:" + QByteArray::number(port_);
    for (qsizetype i = 1; i < rawLines.size(); ++i) {
        QByteArray line = rawLines.at(i);
        if (!line.endsWith('\r')) return fail("Callback request is malformed");
        line.chop(1);
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) return fail("Callback request headers were rejected");
        const QByteArray name = line.left(colon);
        const QByteArray value = trimOws(line.mid(colon + 1));
        if (!isHeaderName(name)) return fail("Callback request headers were rejected");
        for (const char c : value) {
            const auto byte = static_cast<unsigned char>(c);
            if ((byte < 0x20 && c != '\t') || byte == 0x7f) {
                return fail("Callback request headers were rejected");
            }
        }

        const QByteArray lowerName = name.toLower();
        if (lowerName == "host") {
            if (hasHost || (value != ipv4Host && value != ipv6Host)) {
                return fail("Callback Host header was rejected");
            }
            hasHost = true;
        } else if (lowerName == "content-length") {
            if (hasContentLength || value.isEmpty()) {
                return fail("Callback request body was rejected");
            }
            quint64 length = 0;
            for (const char c : value) {
                if (c < '0' || c > '9') return fail("Callback request body was rejected");
                const auto digit = static_cast<quint64>(c - '0');
                if (length > (std::numeric_limits<quint64>::max() - digit) / 10) {
                    return fail("Callback request body was rejected");
                }
                length = length * 10 + digit;
            }
            if (length != 0) return fail("Callback request body was rejected");
            hasContentLength = true;
        } else if (lowerName == "transfer-encoding") {
            return fail("Callback request body was rejected");
        }
    }
    if (!hasHost) return fail("Callback Host header was rejected");

    *callbackUrl = QUrl::fromEncoded("http://127.0.0.1:" + QByteArray::number(port_) + target);
    return true;
}

void LoopbackListener::rejectAndClose(QTcpSocket* socket, const QString& safeReason) {
    if (!socket) return;
    requestBuffers_.remove(socket);
    completedSockets_.insert(socket);
    socket->abort();
    emit failed(safeReason);
    finishSocket(socket);
}

void LoopbackListener::finishSocket(QTcpSocket* socket) {
    requestBuffers_.remove(socket);
    completedSockets_.remove(socket);
    if (socket) socket->deleteLater();
}

} // namespace vc::suno::auth::oauth
