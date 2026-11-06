#include <QtTest>

#include "suno/SunoMcpClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace vc::suno;

/// The MCP client's protocol surface is text in and text out, so the parsing
/// and envelope shapes are pinned here without a socket: the exact JSON-RPC
/// bodies, both response framings (plain JSON and SSE), and the tool-list
/// tolerance that keeps a missing inputSchema from dropping a tool.
class TestSunoMcpClient : public QObject
{
    Q_OBJECT

private slots:
    void parsesPlainJsonBody()
    {
        const QByteArray body = R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18"}})";
        const auto parsed = SunoMcpClient::parseRpcBody(body, "application/json");
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->value(QStringLiteral("id")).toInt(), 1);
        QCOMPARE(parsed->value(QStringLiteral("result"))
                         .toObject()
                         .value(QStringLiteral("protocolVersion"))
                         .toString(),
                 QStringLiteral("2025-06-18"));
    }

    void parsesSseBodyAndTakesTheLastEvent()
    {
        const QByteArray body =
                "event: message\n"
                "data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\n"
                "\n"
                "event: message\n"
                "data: {\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"tools\":[]}}\n"
                "\n";
        const auto parsed = SunoMcpClient::parseRpcBody(body, "text/event-stream");
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->value(QStringLiteral("id")).toInt(), 7);
    }

    void refusesBodiesThatAreNotJsonObjects()
    {
        QVERIFY(!SunoMcpClient::parseRpcBody("not json", "application/json"));
        QVERIFY(!SunoMcpClient::parseRpcBody("[1,2,3]", "application/json"));
        QVERIFY(!SunoMcpClient::parseRpcBody("", "text/event-stream"));
        QVERIFY(!SunoMcpClient::parseRpcBody("data: nonsense\n\n", "text/event-stream"));
    }

    void initializeCarriesProtocolVersionAndClientInfo()
    {
        const QJsonObject request =
                SunoMcpClient::makeInitialize(1, QStringLiteral("ChadVis"), QStringLiteral("1.2.0"));
        QCOMPARE(request.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
        QCOMPARE(request.value(QStringLiteral("method")).toString(), QStringLiteral("initialize"));
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("protocolVersion")).toString(),
                 QString::fromLatin1(SunoMcpClient::kProtocolVersion));
        QVERIFY(params.value(QStringLiteral("capabilities")).isObject());
        QCOMPARE(params.value(QStringLiteral("clientInfo"))
                         .toObject()
                         .value(QStringLiteral("name"))
                         .toString(),
                 QStringLiteral("ChadVis"));
    }

    void toolCallCarriesNameAndArguments()
    {
        QJsonObject args;
        args.insert(QStringLiteral("prompt"), QStringLiteral("lo-fi"));
        const QJsonObject request = SunoMcpClient::makeToolCall(9, QStringLiteral("generate"), args);
        QCOMPARE(request.value(QStringLiteral("method")).toString(), QStringLiteral("tools/call"));
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("name")).toString(), QStringLiteral("generate"));
        QCOMPARE(params.value(QStringLiteral("arguments"))
                         .toObject()
                         .value(QStringLiteral("prompt"))
                         .toString(),
                 QStringLiteral("lo-fi"));
    }

    void parsesToolsAndToleratesMissingSchema()
    {
        const QJsonObject response = QJsonDocument::fromJson(R"({
            "jsonrpc":"2.0","id":2,
            "result":{"tools":[
                {"name":"generate","description":"make a song","inputSchema":{"type":"object"}},
                {"name":"credits","description":"how many credits"},
                {"description":"no name — dropped"}
            ]}
        })").object();
        const auto tools = SunoMcpClient::parseTools(response);
        QCOMPARE(tools.size(), 2);
        QCOMPARE(tools.at(0).name, QStringLiteral("generate"));
        QCOMPARE(tools.at(0).inputSchema.value(QStringLiteral("type")).toString(),
                 QStringLiteral("object"));
        // A tool without a schema is still a tool.
        QVERIFY(tools.at(1).inputSchema.isEmpty());
    }

    void callBeforeHandshakeFailsClosed()
    {
        SunoMcpClient client(nullptr);
        QSignalSpy failures(&client, &SunoMcpClient::failed);
        client.callTool(QStringLiteral("generate"), QJsonObject{});
        QCOMPARE(failures.count(), 1);
        QVERIFY(failures.first().first().toString().contains(QStringLiteral("not initialized")));
    }
};

QTEST_MAIN(TestSunoMcpClient)
#include "test_SunoMcpClient.moc"
