#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <expected>

class QTcpServer;
class QTcpSocket;

namespace vc::suno::auth::oauth {

/// Minimal HTTP/1.1 origin-server transport for a single OAuth callback.
class LoopbackListener : public QObject {
    Q_OBJECT

public:
    static constexpr qsizetype kMaxRequestLineBytes = 8 * 1024;
    static constexpr qsizetype kMaxHeaderBytes = 16 * 1024;
    static constexpr qsizetype kMaxUrlLengthBytes = 4 * 1024;

    explicit LoopbackListener(QObject* parent = nullptr);
    ~LoopbackListener() override = default;

    [[nodiscard]] std::expected<void, QString> start(const QString& expectedPath);
    void stop();

    [[nodiscard]] quint16 port() const { return port_; }
    [[nodiscard]] QString expectedPath() const { return expectedPath_; }

signals:
    void requestReceived(const QUrl& callbackUrl);
    void failed(const QString& reason);

private:
    void acceptPendingConnections();
    void readFromSocket();
    void rejectAndClose(QTcpSocket* socket, const QString& safeReason);
    void finishSocket(QTcpSocket* socket);
    [[nodiscard]] bool parseRequest(const QByteArray& headerBlock, QUrl* callbackUrl,
                                    QString* safeReason) const;

    QTcpServer* server_;
    QHash<QTcpSocket*, QByteArray> requestBuffers_;
    QSet<QTcpSocket*> completedSockets_;
    QString expectedPath_;
    quint16 port_ = 0;
};

} // namespace vc::suno::auth::oauth
