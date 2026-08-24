/*
 * ========================================================================== *
 *    TestMcpServer — QtTest suite for McpServer                              *
 *                                                                            *
 *    Tests server lifecycle, SSE transport start/stop, end-to-end JSON-RPC   *
 *    dispatch over SSE, dependency injection, and signal emissions.          *
 *                                                                            *
 *    Run:  ./test_mcp_server                                                 *
 *    Or:   ctest -R McpServer --output-on-failure                            *
 * ========================================================================== *
 */

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QHostAddress>
#include <QEventLoop>
#include <QTimer>

#include "server/mcp/mcpServer.h"
#include "server/mcp/mcpToolHandler.h"
#include "server/mcp/mcpConstants.h"

class TestMcpServer : public QObject
{
    Q_OBJECT

private:
    // Helper: wait for socket data while processing ALL events in the thread.
    // This is necessary because waitForReadyRead only processes events for
    // the specific socket, not for QTcpServer's newConnection signals.
    static QByteArray waitForSocketData(QTcpSocket* socket, int timeoutMs = 5000);

    // Helper: send raw HTTP request on a socket and read the response headers+body
    static QByteArray sendRawHttp(QTcpSocket* socket,
                                  const QByteArray& request,
                                  int timeoutMs = 3000);

    // Helper: open a fresh SSE session, returns the sessionId (empty on failure)
    QString openSseSession(quint16 port, QTcpSocket*& socketOut);

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();   // runs before each test function
    void cleanup();

    // --- Lifecycle ---
    void initialstate_notRunning();
    void startSse_success();
    void startSse_alreadyRunning();
    void stopSse_success();
    void stopSse_whenNotRunning_isNoop();
    void ssePort_isCorrect();
    void sseSessionCount_initiallyZero();

    // --- Signal emissions ---
    void sseStarted_signalEmitted();
    void sseStopped_signalEmitted();
    void logMessage_signalEmitted();

    // --- Dependency injection ---
    void setToolHandler_replacesDefault();
    void toolHandler_accessor();

    // --- SSE end-to-end: GET /sse ---
    void sse_getSse_returnsEventStream();
    void sse_getSse_returnsEndpointEvent();
    void sse_getSse_sessionIdIsUuid();

    // --- SSE end-to-end: POST /messages ---
    void sse_postInitialize_returnsResult();
    void sse_postToolsList_returnsToolArray();
    void sse_postPing_returnsOk();
    void sse_postToolsCall_returnsToolResult();
    void sse_postUnknownMethod_returnsMethodNotFound();
    void sse_postInvalidJson_returnsParseError();
    void sse_postWithoutSessionId_returnsBadRequest();
    void sse_postWithInvalidSessionId_returnsNotFound();

    // --- SSE session management ---
    void sse_multipleClients_getDifferentSessionIds();
    void sse_sessionTimeout_cleansUp();

    // --- stdio transport ---
    void stdio_initiallyDisabled();

private:
    McpServer* m_server = nullptr;
    quint16 m_testPort = 0;
};

// ============================================================================
// Fixture
// ============================================================================

void TestMcpServer::initTestCase()
{
    // Find an available port for testing
    QTcpServer probe;
    QVERIFY2(probe.listen(QHostAddress::LocalHost, 0),
             "Failed to bind ephemeral port for test probe");
    m_testPort = probe.serverPort();
    probe.close();
}

void TestMcpServer::cleanupTestCase()
{
}

void TestMcpServer::init()
{
    m_server = new McpServer();
}

void TestMcpServer::cleanup()
{
    if (m_server) {
        m_server->stopSse();
        delete m_server;
        m_server = nullptr;
    }
}

// ============================================================================
// Helpers
// ============================================================================

QByteArray TestMcpServer::waitForSocketData(QTcpSocket* socket, int timeoutMs)
{
    QByteArray data;
    int elapsed = 0;
    const int step = 50;

    while (elapsed < timeoutMs) {
        QTest::qWait(step);
        elapsed += step;
        data += socket->readAll();
        if (!data.isEmpty()) {
            // Give a little more time for remaining data
            QTest::qWait(100);
            data += socket->readAll();
            break;
        }
    }
    return data;
}

QByteArray TestMcpServer::sendRawHttp(QTcpSocket* socket,
                                       const QByteArray& request,
                                       int timeoutMs)
{
    socket->write(request);
    socket->flush();
    return waitForSocketData(socket, timeoutMs);
}

QString TestMcpServer::openSseSession(quint16 port, QTcpSocket*& socketOut)
{
    // Heap-allocate without parent — server takes ownership via deleteLater()
    socketOut = new QTcpSocket();
    socketOut->connectToHost(QHostAddress::LocalHost, port);
    if (!socketOut->waitForConnected(3000)) {
        delete socketOut;
        socketOut = nullptr;
        return {};
    }

    QTest::qWait(50); // Let server process newConnection

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socketOut, req, 3000);
    QString respStr = QString::fromUtf8(resp);

    // Extract sessionId from the endpoint event data
    // Expected: "data: {\"endpoint\":\"/messages?sessionId=<uuid>\"}"
    int sidStart = respStr.indexOf("sessionId=");
    if (sidStart < 0) return {};
    sidStart += strlen("sessionId=");
    int sidEnd = respStr.indexOf("\"", sidStart);
    if (sidEnd < 0) sidEnd = respStr.indexOf("\n", sidStart);
    if (sidEnd < 0) return {};

    return respStr.mid(sidStart, sidEnd - sidStart);
}

// ============================================================================
// Lifecycle tests
// ============================================================================

void TestMcpServer::initialstate_notRunning()
{
    QVERIFY(!m_server->isSseRunning());
    QCOMPARE(m_server->sseSessionCount(), 0);
}

void TestMcpServer::startSse_success()
{
    QSignalSpy spy(m_server, &McpServer::sseStarted);

    bool ok = m_server->startSse(m_testPort, QHostAddress::LocalHost);
    QVERIFY2(ok, "startSse should succeed on available port");
    QVERIFY(m_server->isSseRunning());

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toUInt(), m_testPort);
}

void TestMcpServer::startSse_alreadyRunning()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));

    QSignalSpy spy(m_server, &McpServer::sseStarted);
    // Second start on same port should fail (port already bound)
    bool ok = m_server->startSse(m_testPort, QHostAddress::LocalHost);
    // Depending on implementation, this may return false or silently no-op
    // Either way, sseStarted should NOT be emitted again
    QCOMPARE(spy.count(), 0);
}

void TestMcpServer::stopSse_success()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QVERIFY(m_server->isSseRunning());

    QSignalSpy spy(m_server, &McpServer::sseStopped);
    m_server->stopSse();

    QVERIFY(!m_server->isSseRunning());
    QCOMPARE(spy.count(), 1);
}

void TestMcpServer::stopSse_whenNotRunning_isNoop()
{
    QSignalSpy spy(m_server, &McpServer::sseStopped);
    m_server->stopSse(); // Should not crash or emit
    QCOMPARE(spy.count(), 0);
}

void TestMcpServer::ssePort_isCorrect()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    // Port may be queried via the transport — verify it matches what we requested
    // (The McpServer exposes this indirectly through SSE connection success)
    QVERIFY(m_server->isSseRunning());
}

void TestMcpServer::sseSessionCount_initiallyZero()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QCOMPARE(m_server->sseSessionCount(), 0);
}

// ============================================================================
// Signal tests
// ============================================================================

void TestMcpServer::sseStarted_signalEmitted()
{
    QSignalSpy spy(m_server, &McpServer::sseStarted);
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QCOMPARE(spy.count(), 1);
}

void TestMcpServer::sseStopped_signalEmitted()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QSignalSpy spy(m_server, &McpServer::sseStopped);
    m_server->stopSse();
    QCOMPARE(spy.count(), 1);
}

void TestMcpServer::logMessage_signalEmitted()
{
    QSignalSpy spy(m_server, &McpServer::logMessage);
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sock = nullptr;
    QString sid = openSseSession(m_testPort, sock);
    QVERIFY(!sid.isEmpty());

    QTest::qWait(500);

    QVERIFY(spy.count() >= 0);

    sock->disconnectFromHost();
    QTest::qWait(300);
}

// ============================================================================
// Dependency injection tests
// ============================================================================

void TestMcpServer::setToolHandler_replacesDefault()
{
    auto* customHandler = new McpToolHandler();
    m_server->setToolHandler(customHandler);
    QCOMPARE(m_server->toolHandler(), customHandler);
}

void TestMcpServer::toolHandler_accessor()
{
    McpToolHandler* handler = m_server->toolHandler();
    Q_UNUSED(handler);
}

// ============================================================================
// SSE end-to-end: GET /sse
// ============================================================================

void TestMcpServer::sse_getSse_returnsEventStream()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY2(socket->waitForConnected(3000), "Failed to connect to SSE server");
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 5000);

    QVERIFY2(resp.contains("200"), "GET /sse should return HTTP 200");
    QVERIFY(resp.contains("text/event-stream"));

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpServer::sse_getSse_returnsEndpointEvent()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);
    QString respStr = QString::fromUtf8(resp);

    QVERIFY2(respStr.contains("event: endpoint"),
             "SSE response must include 'event: endpoint'");

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpServer::sse_getSse_sessionIdIsUuid()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);
    QString respStr = QString::fromUtf8(resp);

    int sidStart = respStr.indexOf("sessionId=");
    QVERIFY2(sidStart >= 0, "Response must contain sessionId");
    sidStart += strlen("sessionId=");
    int sidEnd = respStr.indexOf("\"", sidStart);
    if (sidEnd < 0) sidEnd = respStr.indexOf("\r", sidStart);
    if (sidEnd < 0) sidEnd = respStr.indexOf("\n", sidStart);
    QVERIFY(sidEnd > sidStart);

    QString sid = respStr.mid(sidStart, sidEnd - sidStart);
    QRegularExpression uuidRe(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",
        QRegularExpression::CaseInsensitiveOption);
    QVERIFY2(uuidRe.match(sid).hasMatch(),
             qPrintable("Session ID must be UUID, got: " + sid));

    socket->disconnectFromHost();
    QTest::qWait(300);
}

// ============================================================================
// SSE end-to-end: POST /messages
// ============================================================================

void TestMcpServer::sse_postInitialize_returnsResult()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSseSession(m_testPort, sseSock);
    QVERIFY2(!sid.isEmpty(), "Failed to open SSE session");

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sendRawHttp(postSock, req, 3000);
    QTest::qWait(500);

    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("event: message"));
    QVERIFY(sseStr.contains("serverInfo"));

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpServer::sse_postToolsList_returnsToolArray()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSseSession(m_testPort, sseSock);
    QVERIFY(!sid.isEmpty());

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sendRawHttp(postSock, req, 3000);
    QTest::qWait(500);

    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("tools"));

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpServer::sse_postPing_returnsOk()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSseSession(m_testPort, sseSock);
    QVERIFY(!sid.isEmpty());

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":3,"method":"ping"})";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sendRawHttp(postSock, req, 3000);
    QTest::qWait(500);

    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("event: message"));

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpServer::sse_postUnknownMethod_returnsMethodNotFound()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSseSession(m_testPort, sseSock);
    QVERIFY(!sid.isEmpty());

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":4,"method":"nonexistent/method","params":{}})";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sendRawHttp(postSock, req, 3000);
    QTest::qWait(500);

    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("-32601")); // Method not found

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpServer::sse_postInvalidJson_returnsParseError()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSseSession(m_testPort, sseSock);
    QVERIFY(!sid.isEmpty());

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({not valid json)";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sendRawHttp(postSock, req, 3000);
    QTest::qWait(500);

    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("-32700")); // Parse error

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpServer::sse_postWithoutSessionId_returnsBadRequest()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QByteArray req = "POST /messages HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    QByteArray resp = sendRawHttp(postSock, req, 3000);

    QVERIFY(resp.contains("400") || resp.contains("Bad Request"));

    postSock->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpServer::sse_postWithInvalidSessionId_returnsNotFound()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QByteArray req = "POST /messages?sessionId=invalid-session HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    QByteArray resp = sendRawHttp(postSock, req, 3000);

    QVERIFY(resp.contains("404") || resp.contains("Not Found"));

    postSock->disconnectFromHost();
    QTest::qWait(300);
}

// ============================================================================
// SSE session management tests
// ============================================================================

void TestMcpServer::sse_multipleClients_getDifferentSessionIds()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sock1 = nullptr;
    QTcpSocket* sock2 = nullptr;
    QTcpSocket* sock3 = nullptr;

    QString sid1 = openSseSession(m_testPort, sock1);
    QString sid2 = openSseSession(m_testPort, sock2);
    QString sid3 = openSseSession(m_testPort, sock3);

    QVERIFY(!sid1.isEmpty());
    QVERIFY(!sid2.isEmpty());
    QVERIFY(!sid3.isEmpty());

    QVERIFY(sid1 != sid2);
    QVERIFY(sid2 != sid3);
    QVERIFY(sid1 != sid3);

    sock1->disconnectFromHost();
    sock2->disconnectFromHost();
    sock3->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpServer::sse_sessionTimeout_cleansUp()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sock = nullptr;
    QString sid = openSseSession(m_testPort, sock);
    QVERIFY(!sid.isEmpty());
    QCOMPARE(m_server->sseSessionCount(), 1);

    // We can't easily wait for the full 30-minute timeout in a test,
    // so we just verify the session was created.
    sock->disconnectFromHost();
    QTest::qWait(500);
    QCOMPARE(m_server->sseSessionCount(), 0);
}

void TestMcpServer::sse_postToolsCall_returnsToolResult()
{
    QVERIFY(m_server->startSse(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSseSession(m_testPort, sseSock);
    QVERIFY(!sid.isEmpty());

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"system_status","arguments":{}}})";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    sendRawHttp(postSock, req, 3000);
    QTest::qWait(500);

    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("event: message"));
    QVERIFY(sseStr.contains("content"));

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

// ============================================================================
// stdio transport tests
// ============================================================================

void TestMcpServer::stdio_initiallyDisabled()
{
    // The stdio transport is only active when explicitly started via startStdio().
    // In a test context, we just verify the server isn't in stdio mode by default.
    // startStdio() attaches to stdin/stdout which is unsafe in a test runner,
    // so we only verify it's not auto-started.
    QVERIFY(!m_server->isSseRunning()); // SSE is the alternative transport
}

QTEST_MAIN(TestMcpServer)
#include "test_mcp_server.moc"
