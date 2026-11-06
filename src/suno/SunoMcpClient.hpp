#pragma once
// SunoMcpClient.hpp — a minimal MCP client for https://api.suno.com/mcp.
//
// Suno's official MCP server speaks JSON-RPC 2.0 over HTTP with an SSE-capable
// response body (docs/suno_api/MCP.md). This class implements exactly the three
// messages a chat surface needs — initialize, tools/list, tools/call — plus the
// `notifications/initialized` the spec requires in between. It is deliberately
// NOT a general MCP runtime: no sampling, no roots, no resources.
//
// Token handling is the caller's: the client takes a bearer and sends it. The
// token is MCP-scoped (resource https://api.suno.com/mcp), so this client must
// never be used for studio-api routes — those keep SunoClient's `suno/default`
// credential. Two credentials, two audiences, no crossing.
//
// Response bodies may arrive as `application/json` or as `text/event-stream`
// (`event: message\ndata: {…}`); both are parsed by one function so a server
// switching framing cannot look like a protocol failure.
//
// Fail closed: a reply missing `result` and `error` is a protocol error, not an
// empty success; a tool result carrying `isError: true` is surfaced as an error
// even though the transport succeeded.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

#include "suno/HttpPolicy.hpp"

class QNetworkAccessManager;
class QNetworkReply;

namespace vc::suno {

class SunoMcpClient final : public QObject
{
    Q_OBJECT

public:
    struct Tool {
        QString name;
        QString description;
        QJsonObject inputSchema;
    };

    /// The protocol revision this client speaks. The server answers its own
    /// supported revision in `initialize`; a mismatch is reported, not guessed.
    static constexpr const char* kProtocolVersion = "2025-06-18";

    explicit SunoMcpClient(QNetworkAccessManager* networkManager, QObject* parent = nullptr);
    ~SunoMcpClient() override;

    void setEndpoint(const QUrl& endpoint);
    void setBearerToken(const QString& bearer);

    /// initialize → notifications/initialized → tools/list. Emits toolsListed()
    /// or failed(). Re-entrant calls while a handshake runs are refused.
    void connectAndListTools();

    /// Call one tool. Requires a completed handshake (the server's session id
    /// is part of the request). Emits toolResult() or failed().
    void callTool(const QString& name, const QJsonObject& arguments);

    [[nodiscard]] bool isConnected() const { return connected_; }

    // ── Pure helpers (unit-tested) ─────────────────────────────────────────

    /// Parse a JSON-RPC body that may be plain JSON or one SSE event stream.
    /// Returns the last `data:` payload's object, or nullopt when neither
    /// framing yields a JSON object.
    [[nodiscard]] static std::optional<QJsonObject> parseRpcBody(const QByteArray& body,
                                                                 const QByteArray& contentType);

    /// `initialize` request object for the given id.
    [[nodiscard]] static QJsonObject makeInitialize(int id, const QString& clientName,
                                                    const QString& clientVersion);

    /// `tools/call` request object for the given id.
    [[nodiscard]] static QJsonObject makeToolCall(int id, const QString& name,
                                                  const QJsonObject& arguments);

    /// Extract `result.tools` into Tool entries, tolerating a missing schema.
    [[nodiscard]] static QList<Tool> parseTools(const QJsonObject& rpcResponse);

signals:
    void connected();
    void toolsListed(const QList<vc::suno::SunoMcpClient::Tool>& tools);
    void toolResult(const QString& toolName, const QJsonObject& result);
    void failed(const QString& safeError);

private:
    void postRpc(const QJsonObject& payload, bool initializeRequest = false);
    void onReply(QNetworkReply* reply, const QString& phase,
                 const http::BodyReader& reader);

    QNetworkAccessManager* nam_{nullptr};
    QUrl endpoint_{QStringLiteral("https://api.suno.com/mcp")};
    QString bearer_;
    QString sessionId_;
    QString lastToolName_;
    QList<Tool> tools_;
    int nextId_{1};
    bool connected_{false};
    bool handshakeInFlight_{false};
};

} // namespace vc::suno

Q_DECLARE_METATYPE(vc::suno::SunoMcpClient::Tool)
