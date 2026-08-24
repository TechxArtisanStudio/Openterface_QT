/*
 * ========================================================================== *
 *    TestMcpProtocol — QtTest suite for McpProtocol                          *
 *                                                                            *
 *    Tests JSON-RPC 2.0 parsing, response building, and content helpers      *
 *    defined in server/mcp/mcpProtocol.{h,cpp}.                              *
 *                                                                            *
 *    Run:  ./test_mcp_protocol                                               *
 *    Or:   ctest -R McpProtocol --output-on-failure                          *
 * ========================================================================== *
 */

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>

#include "server/mcp/mcpProtocol.h"
#include "server/mcp/mcpConstants.h"

class TestMcpProtocol : public QObject
{
    Q_OBJECT

private slots:
    // --- parseRequest ---
    void parseRequest_validInitialize();
    void parseRequest_validToolsList();
    void parseRequest_validToolsCallWithParams();
    void parseRequest_notificationNoId();
    void parseRequest_integerId();
    void parseRequest_stringId();
    void parseRequest_invalidJson();
    void parseRequest_emptyInput();
    void parseRequest_notAnObject();
    void parseRequest_missingJsonrpc();
    void parseRequest_wrongJsonrpcVersion();
    void parseRequest_missingMethod();
    void parseRequest_emptyMethod();
    void parseRequest_extraFieldsIgnored();

    // --- buildResult ---
    void buildResult_integerId();
    void buildResult_stringId();
    void buildResult_nullId();
    void buildResult_structureCheck();

    // --- buildError ---
    void buildError_withoutData();
    void buildError_withData();
    void buildError_codePropagation();
    void buildError_allStandardCodes();

    // --- buildInitializeResult ---
    void buildInitializeResult_serverInfo();
    void buildInitializeResult_protocolVersion();
    void buildInitializeResult_capabilities();

    // --- serialize ---
    void serialize_endWithNewline();
    void serialize_validJson();
    void serialize_roundTrip();

    // --- content helpers ---
    void textContent_structure();
    void imageContent_structure();
    void imageContent_defaultMimeType();
    void imageContent_customMimeType();
    void toolResult_structure();
    void toolError_structure();
    void toolError_isErrorFlag();
};

// ============================================================================
// parseRequest tests
// ============================================================================

void TestMcpProtocol::parseRequest_validInitialize()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(ok, "parseRequest should succeed for a valid initialize request");
    QCOMPARE(req.jsonrpc, QStringLiteral("2.0"));
    QCOMPARE(req.method, QStringLiteral("initialize"));
    QVERIFY(!req.isNotification);
    QCOMPARE(req.id.toInt(), 1);
    QVERIFY(req.params.contains("protocolVersion"));
    QCOMPARE(req.params["protocolVersion"].toString(), QStringLiteral("2024-11-05"));
    QVERIFY(req.params.contains("clientInfo"));
}

void TestMcpProtocol::parseRequest_validToolsList()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY(ok);
    QCOMPARE(req.method, QStringLiteral("tools/list"));
    QCOMPARE(req.id.toInt(), 2);
    QVERIFY(!req.isNotification);
}

void TestMcpProtocol::parseRequest_validToolsCallWithParams()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"mouse_move_absolute","arguments":{"x":2048,"y":2048}}})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY(ok);
    QCOMPARE(req.method, QStringLiteral("tools/call"));
    QVERIFY(req.params.contains("name"));
    QCOMPARE(req.params["name"].toString(), QStringLiteral("mouse_move_absolute"));

    QJsonObject args = req.params["arguments"].toObject();
    QCOMPARE(args["x"].toInt(), 2048);
    QCOMPARE(args["y"].toInt(), 2048);
}

void TestMcpProtocol::parseRequest_notificationNoId()
{
    const QByteArray line = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY(ok);
    QVERIFY(req.isNotification);
    QVERIFY(!req.id.isValid());
    QCOMPARE(req.method, QStringLiteral("notifications/initialized"));
}

void TestMcpProtocol::parseRequest_integerId()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":42,"method":"ping"})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY(ok);
    QVERIFY(!req.isNotification);
    QCOMPARE(req.id.toInt(), 42);
}

void TestMcpProtocol::parseRequest_stringId()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":"abc-def","method":"ping"})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY(ok);
    QVERIFY(!req.isNotification);
    QCOMPARE(req.id.toString(), QStringLiteral("abc-def"));
}

void TestMcpProtocol::parseRequest_invalidJson()
{
    const QByteArray line = R"({not valid json)";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should fail on invalid JSON");
}

void TestMcpProtocol::parseRequest_emptyInput()
{
    const QByteArray line = "";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should fail on empty input");
}

void TestMcpProtocol::parseRequest_notAnObject()
{
    const QByteArray line = R"([1, 2, 3])";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should fail when JSON is not an object");
}

void TestMcpProtocol::parseRequest_missingJsonrpc()
{
    const QByteArray line = R"({"id":1,"method":"ping"})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should fail when jsonrpc field is missing");
}

void TestMcpProtocol::parseRequest_wrongJsonrpcVersion()
{
    const QByteArray line = R"({"jsonrpc":"1.0","id":1,"method":"ping"})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should reject non-2.0 jsonrpc version");
}

void TestMcpProtocol::parseRequest_missingMethod()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":1})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should fail when method is missing");
}

void TestMcpProtocol::parseRequest_emptyMethod()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":1,"method":""})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(!ok, "parseRequest should fail when method is empty string");
}

void TestMcpProtocol::parseRequest_extraFieldsIgnored()
{
    const QByteArray line = R"({"jsonrpc":"2.0","id":1,"method":"ping","extra":"data","foo":123})";

    McpProtocol::Request req;
    bool ok = McpProtocol::parseRequest(line, req);

    QVERIFY2(ok, "parseRequest should ignore extra fields");
    QCOMPARE(req.method, QStringLiteral("ping"));
}

// ============================================================================
// buildResult tests
// ============================================================================

void TestMcpProtocol::buildResult_integerId()
{
    QJsonObject result;
    result["message"] = "ok";

    QJsonObject resp = McpProtocol::buildResult(42, result);

    QCOMPARE(resp["jsonrpc"].toString(), QStringLiteral("2.0"));
    QCOMPARE(resp["id"].toInt(), 42);
    QVERIFY(resp.contains("result"));
    QVERIFY(!resp.contains("error"));
    QCOMPARE(resp["result"].toObject()["message"].toString(), QStringLiteral("ok"));
}

void TestMcpProtocol::buildResult_stringId()
{
    QJsonObject result;
    result["value"] = 100;

    QJsonObject resp = McpProtocol::buildResult(QStringLiteral("req-7"), result);

    QCOMPARE(resp["id"].toString(), QStringLiteral("req-7"));
    QVERIFY(resp.contains("result"));
}

void TestMcpProtocol::buildResult_nullId()
{
    QJsonObject resp = McpProtocol::buildResult(QVariant(), QJsonValue());

    QVERIFY(resp.contains("id"));
    QVERIFY(resp["id"].isNull());
}

void TestMcpProtocol::buildResult_structureCheck()
{
    QJsonObject result{{"key", "value"}};
    QJsonObject resp = McpProtocol::buildResult(1, result);

    // Must have exactly these three top-level keys per JSON-RPC 2.0
    QStringList keys = resp.keys();
    keys.sort();
    QStringList expected = {"id", "jsonrpc", "result"};
    QCOMPARE(keys, expected);
}

// ============================================================================
// buildError tests
// ============================================================================

void TestMcpProtocol::buildError_withoutData()
{
    QJsonObject resp = McpProtocol::buildError(1, -32600, "Invalid request");

    QCOMPARE(resp["jsonrpc"].toString(), QStringLiteral("2.0"));
    QCOMPARE(resp["id"].toInt(), 1);
    QVERIFY(resp.contains("error"));
    QVERIFY(!resp.contains("result"));

    QJsonObject err = resp["error"].toObject();
    QCOMPARE(err["code"].toInt(), -32600);
    QCOMPARE(err["message"].toString(), QStringLiteral("Invalid request"));
    QVERIFY(!err.contains("data"));
}

void TestMcpProtocol::buildError_withData()
{
    QJsonObject data;
    data["detail"] = "missing field";

    QJsonObject resp = McpProtocol::buildError(5, -32602, "Invalid params", data);

    QJsonObject err = resp["error"].toObject();
    QCOMPARE(err["code"].toInt(), -32602);
    QVERIFY(err.contains("data"));
    QCOMPARE(err["data"].toObject()["detail"].toString(), QStringLiteral("missing field"));
}

void TestMcpProtocol::buildError_codePropagation()
{
    // Each standard JSON-RPC error code should be preserved verbatim
    const QList<int> codes = {-32700, -32600, -32601, -32602, -32603};
    for (int code : codes) {
        QJsonObject resp = McpProtocol::buildError(1, code, "msg");
        QCOMPARE(resp["error"].toObject()["code"].toInt(), code);
    }
}

void TestMcpProtocol::buildError_allStandardCodes()
{
    // Verify the constants in mcpConstants.h match the standard JSON-RPC codes
    QCOMPARE(JSONRPC_ERROR_PARSE_ERROR, -32700);
    QCOMPARE(JSONRPC_ERROR_INVALID_REQUEST, -32600);
    QCOMPARE(JSONRPC_ERROR_METHOD_NOT_FOUND, -32601);
    QCOMPARE(JSONRPC_ERROR_INVALID_PARAMS, -32602);
    QCOMPARE(JSONRPC_ERROR_INTERNAL_ERROR, -32603);
}

// ============================================================================
// buildInitializeResult tests
// ============================================================================

void TestMcpProtocol::buildInitializeResult_serverInfo()
{
    QJsonObject result = McpProtocol::buildInitializeResult();

    QVERIFY(result.contains("serverInfo"));
    QJsonObject info = result["serverInfo"].toObject();
    QCOMPARE(info["name"].toString(), QStringLiteral(MCP_SERVER_NAME));
    QCOMPARE(info["version"].toString(), QStringLiteral(MCP_SERVER_VERSION));
}

void TestMcpProtocol::buildInitializeResult_protocolVersion()
{
    QJsonObject result = McpProtocol::buildInitializeResult();

    QVERIFY(result.contains("protocolVersion"));
    QCOMPARE(result["protocolVersion"].toString(),
             QStringLiteral(MCP_PROTOCOL_VERSION));
}

void TestMcpProtocol::buildInitializeResult_capabilities()
{
    QJsonObject result = McpProtocol::buildInitializeResult();

    QVERIFY(result.contains("capabilities"));
    QJsonObject caps = result["capabilities"].toObject();
    QVERIFY(caps.contains("tools"));
    // tools capability is an empty object for basic tool support
    QVERIFY(caps["tools"].isObject());
}

// ============================================================================
// serialize tests
// ============================================================================

void TestMcpProtocol::serialize_endWithNewline()
{
    QJsonObject obj{{"key", "value"}};
    QByteArray data = McpProtocol::serialize(obj);

    QVERIFY2(data.endsWith('\n'), "Serialized output must end with newline");
}

void TestMcpProtocol::serialize_validJson()
{
    QJsonObject obj{{"hello", "world"}, {"num", 42}};
    QByteArray data = McpProtocol::serialize(obj);

    // Strip the trailing newline before parsing
    QByteArray jsonOnly = data.trimmed();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonOnly, &err);

    QCOMPARE(err.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object()["hello"].toString(), QStringLiteral("world"));
    QCOMPARE(doc.object()["num"].toInt(), 42);
}

void TestMcpProtocol::serialize_roundTrip()
{
    // Build a result, serialize it, parse it back, verify structure
    QJsonObject inner{{"status", "ok"}};
    QJsonObject resp = McpProtocol::buildResult(99, inner);
    QByteArray wire = McpProtocol::serialize(resp);

    // Parse as a Request (it has jsonrpc+id, but method will be empty — ok for this test)
    // Instead, parse manually to verify round-trip fidelity
    QJsonDocument doc = QJsonDocument::fromJson(wire.trimmed());
    QVERIFY(doc.isObject());

    QJsonObject parsed = doc.object();
    QCOMPARE(parsed["jsonrpc"].toString(), QStringLiteral("2.0"));
    QCOMPARE(parsed["id"].toInt(), 99);
    QCOMPARE(parsed["result"].toObject()["status"].toString(), QStringLiteral("ok"));
}

// ============================================================================
// Content helper tests
// ============================================================================

void TestMcpProtocol::textContent_structure()
{
    QJsonObject content = McpProtocol::textContent("Hello");

    QCOMPARE(content["type"].toString(), QStringLiteral("text"));
    QCOMPARE(content["text"].toString(), QStringLiteral("Hello"));
}

void TestMcpProtocol::imageContent_structure()
{
    QByteArray b64 = QByteArray("AQIDBA=="); // base64 of \x01\x02\x03\x04
    QJsonObject content = McpProtocol::imageContent(b64);

    QCOMPARE(content["type"].toString(), QStringLiteral("image"));
    QCOMPARE(content["data"].toString(), QString::fromLatin1(b64));
    QCOMPARE(content["mimeType"].toString(), QStringLiteral("image/jpeg"));
}

void TestMcpProtocol::imageContent_defaultMimeType()
{
    QJsonObject content = McpProtocol::imageContent(QByteArray("dGVzdA=="));

    QCOMPARE(content["mimeType"].toString(), QStringLiteral("image/jpeg"));
}

void TestMcpProtocol::imageContent_customMimeType()
{
    QJsonObject content = McpProtocol::imageContent(
        QByteArray("dGVzdA=="), QStringLiteral("image/png"));

    QCOMPARE(content["mimeType"].toString(), QStringLiteral("image/png"));
}

void TestMcpProtocol::toolResult_structure()
{
    QJsonArray content;
    content.append(McpProtocol::textContent("done"));

    QJsonObject result = McpProtocol::toolResult(content);

    QVERIFY(result.contains("content"));
    QJsonArray arr = result["content"].toArray();
    QCOMPARE(arr.size(), 1);
    QCOMPARE(arr[0].toObject()["type"].toString(), QStringLiteral("text"));
    QVERIFY(!result.contains("isError"));
}

void TestMcpProtocol::toolError_structure()
{
    QJsonObject result = McpProtocol::toolError("something broke");

    QVERIFY(result.contains("content"));
    QVERIFY(result.contains("isError"));
    QCOMPARE(result["isError"].toBool(), true);

    QJsonArray arr = result["content"].toArray();
    QCOMPARE(arr.size(), 1);
    QCOMPARE(arr[0].toObject()["text"].toString(), QStringLiteral("something broke"));
}

void TestMcpProtocol::toolError_isErrorFlag()
{
    QJsonObject result = McpProtocol::toolError("err");
    QVERIFY2(result["isError"].toBool(), "toolError must set isError=true");
}

QTEST_MAIN(TestMcpProtocol)
#include "test_mcp_protocol.moc"
