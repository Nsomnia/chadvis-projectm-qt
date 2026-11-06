#include "suno/SunoMcpClient.hpp"

#include "core/Logger.hpp"
#include "suno/HttpPolicy.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSharedPointer>

namespace vc::suno {

namespace {
constexpr int kJsonRpcError = -32000;
}

SunoMcpClient::SunoMcpClient(QNetworkAccessManager* networkManager, QObject* parent)
    : QObject(parent), nam_(networkManager)
{
}

SunoMcpClient::~SunoMcpClient() = default;

void SunoMcpClient::setEndpoint(const QUrl& endpoint) { endpoint_ = endpoint; }

void SunoMcpClient::setBearerToken(const QString& bearer) { bearer_ = bearer; }

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

std::optional<QJsonObject> SunoMcpClient::parseRpcBody(const QByteArray& body,
                                                       const QByteArray& contentType)
{
    const auto asObject = [](const QByteArray& candidate) -> std::optional<QJsonObject> {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(candidate, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return std::nullopt;
        }
        return document.object();
    };

    if (contentType.contains("text/event-stream")) {
        // One event per blank line; the payload is the `data:` line(s). Take the
        // LAST complete object — an SSE stream may carry progress events first.
        std::optional<QJsonObject> last;
        const QList<QByteArray> events = body.split('\n');
        QByteArray data;
        for (const QByteArray& rawLine : events) {
            const QByteArray line = rawLine.trimmed();
            if (line.startsWith("data:")) {
                data += line.mid(5).trimmed();
            } else if (line.isEmpty() && !data.isEmpty()) {
                if (auto object = asObject(data)) {
                    last = object;
                }
                // A JSON-RPC batch array would be a protocol surprise here; a
                // non-object payload is skipped rather than guessed at.
                data.clear();
            }
        }
        if (!data.isEmpty()) {
            if (auto object = asObject(data)) {
                last = object;
            }
        }
        return last;
    }

    return asObject(body);
}

QJsonObject SunoMcpClient::makeInitialize(int id, const QString& clientName,
                                          const QString& clientVersion)
{
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), clientName);
    clientInfo.insert(QStringLiteral("version"), clientVersion);

    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"), QString::fromLatin1(kProtocolVersion));
    params.insert(QStringLiteral("capabilities"), QJsonObject{});
    params.insert(QStringLiteral("clientInfo"), clientInfo);

    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), id);
    request.insert(QStringLiteral("method"), QStringLiteral("initialize"));
    request.insert(QStringLiteral("params"), params);
    return request;
}

QJsonObject SunoMcpClient::makeToolCall(int id, const QString& name, const QJsonObject& arguments)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("arguments"), arguments);

    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), id);
    request.insert(QStringLiteral("method"), QStringLiteral("tools/call"));
    request.insert(QStringLiteral("params"), params);
    return request;
}

QList<SunoMcpClient::Tool> SunoMcpClient::parseTools(const QJsonObject& rpcResponse)
{
    QList<Tool> out;
    const QJsonArray entries = rpcResponse.value(QStringLiteral("result"))
                                       .toObject()
                                       .value(QStringLiteral("tools"))
                                       .toArray();
    out.reserve(entries.size());
    for (const QJsonValue& entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject object = entry.toObject();
        Tool tool;
        tool.name = object.value(QStringLiteral("name")).toString();
        tool.description = object.value(QStringLiteral("description")).toString();
        tool.inputSchema = object.value(QStringLiteral("inputSchema")).toObject();
        if (!tool.name.isEmpty()) {
            out.append(std::move(tool));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void SunoMcpClient::connectAndListTools()
{
    if (!nam_) {
        emit failed(QStringLiteral("no network manager available"));
        return;
    }
    if (handshakeInFlight_) {
        return;
    }
    if (bearer_.isEmpty()) {
        emit failed(QStringLiteral("sign in to Suno first — no MCP token is stored"));
        return;
    }
    handshakeInFlight_ = true;
    connected_ = false;
    sessionId_.clear();
    tools_.clear();

    postRpc(makeInitialize(nextId_++, QStringLiteral("ChadVis"), QStringLiteral("1.2.0")), true);
}

void SunoMcpClient::callTool(const QString& name, const QJsonObject& arguments)
{
    if (!connected_) {
        emit failed(QStringLiteral("the MCP connection is not initialized"));
        return;
    }
    if (name.isEmpty()) {
        emit failed(QStringLiteral("no tool name given"));
        return;
    }
    lastToolName_ = name;
    postRpc(makeToolCall(nextId_++, name, arguments));
}

void SunoMcpClient::postRpc(const QJsonObject& payload, bool initializeRequest)
{
    if (!nam_) {
        return;
    }

    QNetworkRequest request(endpoint_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    http::applyRequestPolicy(request, http::RequestClass::Auth);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setRawHeader("MCP-Protocol-Version", kProtocolVersion);
    if (!bearer_.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + bearer_.toUtf8());
    }
    if (!initializeRequest && !sessionId_.isEmpty()) {
        request.setRawHeader("Mcp-Session-Id", sessionId_.toUtf8());
    }

    const QString phase = payload.value(QStringLiteral("method")).toString();
    QNetworkReply* reply = nam_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // Same bounded policy as the rest of the tree: pump on readyRead and abort
    // one byte past the cap. A post-hoc readALl is exactly what
    // TestBoundedBody::noCallSiteReadsABodyDirectlyOffAPumpedReply guards.
    auto reader = http::makeBodyReader(http::RequestClass::Auth,
                                       http::maxResponseBytes(http::RequestClass::Auth));
    if (!reader) {
        emit failed(http::describePolicyFailureText(reader.error()));
        reply->deleteLater();
        return;
    }
    QSharedPointer<http::BodyReader> bounded = QSharedPointer<http::BodyReader>::create(*reader);
    connect(reply, &QNetworkReply::readyRead, this, [bounded, reply]() {
        if (auto pumped = bounded->pump(*reply); !pumped) {
            LOG_ERROR("SunoMcpClient: {}", http::describePolicyFailure(pumped.error()));
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bounded, phase]() {
        (void)bounded->pump(*reply); // final drain
        onReply(reply, phase, *bounded);
        reply->deleteLater();
    });
}

void SunoMcpClient::onReply(QNetworkReply* reply, const QString& phase,
                            const http::BodyReader& reader)
{
    if (reader.failure()) {
        if (phase == QStringLiteral("initialize")) {
            handshakeInFlight_ = false;
        }
        emit failed(http::describePolicyFailureText(*reader.failure()));
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray contentType =
            reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

    const std::optional<QJsonObject> rpc = parseRpcBody(reader.body(), contentType);
    if (!rpc) {
        if (phase == QStringLiteral("initialize")) {
            handshakeInFlight_ = false;
        }
        emit failed(status == 401 || status == 403
                            ? QStringLiteral("Suno refused the MCP token (HTTP %1)").arg(status)
                            : QStringLiteral("the MCP server answered an unreadable body (HTTP %1)")
                                      .arg(status));
        return;
    }

    if (const QJsonObject error = rpc->value(QStringLiteral("error")).toObject();
        !error.isEmpty()) {
        if (phase == QStringLiteral("initialize")) {
            handshakeInFlight_ = false;
        }
        // JSON-RPC error `message` is server prose and may be long; report the
        // numeric code, which is the part we can branch on.
        const int code = error.value(QStringLiteral("code")).toInt(kJsonRpcError);
        emit failed(QStringLiteral("MCP error %1 during %2").arg(code).arg(phase));
        return;
    }

    if (phase == QStringLiteral("initialize")) {
        handshakeInFlight_ = false;
        const QJsonObject result = rpc->value(QStringLiteral("result")).toObject();
        if (result.isEmpty()) {
            emit failed(QStringLiteral("MCP initialize answered no result"));
            return;
        }
        // Protocol revision mismatch is reported but not fatal: the server picks
        // the revision it will speak, and a differing string is the signal that
        // this client is older than the surface.
        const QString serverVersion = result.value(QStringLiteral("protocolVersion")).toString();
        if (!serverVersion.isEmpty() && serverVersion != QString::fromLatin1(kProtocolVersion)) {
            LOG_INFO("SunoMcpClient: server protocol {} (client {})",
                     serverVersion.toStdString(), kProtocolVersion);
        }
        sessionId_ = QString::fromUtf8(
                reply->rawHeader("Mcp-Session-Id"));
        connected_ = true;
        emit connected();

        // notifications/initialized has no id and no result; fire and then ask
        // for the tool list.
        QJsonObject initialized;
        initialized.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        initialized.insert(QStringLiteral("method"), QStringLiteral("notifications/initialized"));
        postRpc(initialized);

        QJsonObject listRequest;
        listRequest.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        listRequest.insert(QStringLiteral("id"), nextId_++);
        listRequest.insert(QStringLiteral("method"), QStringLiteral("tools/list"));
        postRpc(listRequest);
        return;
    }

    if (phase == QStringLiteral("notifications/initialized")) {
        return; // no result is expected
    }

    if (phase == QStringLiteral("tools/list")) {
        tools_ = parseTools(*rpc);
        emit toolsListed(tools_);
        return;
    }

    if (phase == QStringLiteral("tools/call")) {
        const QJsonObject result = rpc->value(QStringLiteral("result")).toObject();
        if (result.isEmpty()) {
            emit failed(QStringLiteral("the tool call answered no result"));
            return;
        }
        // MCP signals tool-level failure inside a successful transport reply.
        if (result.value(QStringLiteral("isError")).toBool()) {
            QString message = QStringLiteral("the tool reported an error");
            const QJsonArray content = result.value(QStringLiteral("content")).toArray();
            if (!content.isEmpty() && content.first().isObject()) {
                const QString text =
                        content.first().toObject().value(QStringLiteral("text")).toString();
                if (!text.isEmpty()) {
                    message = text;
                }
            }
            emit failed(message);
            return;
        }
        emit toolResult(lastToolName_, result);
    }
}

} // namespace vc::suno
