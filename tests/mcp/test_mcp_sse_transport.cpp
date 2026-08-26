/*
 * ========================================================================== *
 *    TestMcpSseTransport — QtTest suite for McpSseTransport                  *
 *                                                                            *
 *    Tests SSE transport lifecycle, session management, HTTP parsing, and    *
 *    endpoint routing defined in server/mcp/mcpSseTransport.{h,cpp}.         *
 *                                                                            *
 *    Run:  ./test_mcp_sse_transport                                          *
 *    Or:   ctest -R McpSseTransport --output-on-failure                      *
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
#include <QRegularExpression>

#include "server/mcp/mcpSseTransport.h"
#include "server/mcp/mcpToolHandler.h"
#include "server/mcp/mcpConstants.h"

class TestMcpSseTransport : public QObject
{
    Q_OBJECT

private:
    // Helper: find an available port
    static quint16 findFreePort();

    // Helper: send raw HTTP and read response
    static QByteArray sendRawHttp(QTcpSocket* socket,
                                  const QByteArray& request,
                                  int timeoutMs = 3000);

    // Helper: open a fresh SSE session
    QString openSession(QTcpSocket*& socketOut);

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // --- Lifecycle ---
    void initialState_notRunning();
    void start_success();
    void start_returnsFalse_whenPortBusy();
    void stop_success();
    void stop_whenNotRunning_isNoop();
    void port_returnsCorrectPort();
    void activeSessionCount_initiallyZero();

    // --- Session creation (GET /sse) ---
    void getSse_returnsHttp200();
    void getSse_contentTypeIsEventStream();
    void getSse_sendsEndpointEvent();
    void getSse_sessionIdIsUuid();
    void getSse_incrementsSessionCount();

    // --- Message routing (POST /messages) ---
    void postMessages_withValidSession_returnsAccepted();
    void postMessages_withInvalidSession_returns404();
    void postMessages_withoutSessionId_returns400();
    void postMessages_invalidJson_returnsParseErrorOnStream();
    void postMessages_validToolCall_returnsResultOnStream();

    // --- Unknown routes ---
    void getUnknownPath_returns404();
    void postUnknownPath_returns404();
    void putMethod_returns405();

    // --- Session lifecycle ---
    void sessionCreated_signalEmitted();
    void sessionDestroyed_signalEmitted_onClientDisconnect();
    void multipleSessions_uniqueIds();
    void multipleSessions_allCounted();

    // --- Constants ---
    void defaultPort_matchesConstant();
    void defaultPath_matchesConstant();
    void maxSessions_matchesConstant();
    void sessionTimeout_matchesConstant();
    void keepaliveInterval_matchesConstant();

private:
    McpToolHandler* m_toolHandler = nullptr;
    McpSseTransport* m_transport = nullptr;
    quint16 m_testPort = 0;
};

// ============================================================================
// Fixture
// ============================================================================

quint16 TestMcpSseTransport::findFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    quint16 port = probe.serverPort();
    probe.close();
    return port;
}

QByteArray TestMcpSseTransport::sendRawHttp(QTcpSocket* socket,
                                              const QByteArray& request,
                                              int timeoutMs)
{
    // Write request
    socket->write(request);
    socket->flush();

    // Use qWait-based loop so the GLOBAL event loop runs — this processes
    // both the client socket's readyRead AND the server's newConnection signals.
    // (waitForReadyRead only processes events for the specific socket.)
    QByteArray response;
    int elapsed = 0;
    const int step = 25;
    while (elapsed < timeoutMs) {
        QTest::qWait(step);
        elapsed += step;
        response += socket->readAll();
        if (!response.isEmpty()) {
            // Small grace period to collect remaining data
            QTest::qWait(100);
            response += socket->readAll();
            break;
        }
    }
    return response;
}

QString TestMcpSseTransport::openSession(QTcpSocket*& socketOut)
{
    // Heap-allocate: the SSE server takes ownership via deleteLater() on disconnect.
    // The test must NOT delete the socket — the server will.
    socketOut = new QTcpSocket();
    socketOut->connectToHost(QHostAddress::LocalHost, m_testPort);
    if (!socketOut->waitForConnected(3000)) {
        delete socketOut;
        socketOut = nullptr;
        return {};
    }

    // Let server process newConnection
    QTest::qWait(100);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socketOut, req, 3000);
    QString respStr = QString::fromUtf8(resp);

    int sidStart = respStr.indexOf("sessionId=");
    if (sidStart < 0) return {};
    sidStart += strlen("sessionId=");
    int sidEnd = respStr.indexOf("\"", sidStart);
    if (sidEnd < 0) sidEnd = respStr.indexOf("\r", sidStart);
    if (sidEnd < 0) sidEnd = respStr.indexOf("\n", sidStart);
    if (sidEnd < 0) return {};

    return respStr.mid(sidStart, sidEnd - sidStart);
}

void TestMcpSseTransport::initTestCase()
{
    m_testPort = findFreePort();
    QVERIFY2(m_testPort > 0, "Failed to find a free port for testing");
}

void TestMcpSseTransport::cleanupTestCase()
{
}

void TestMcpSseTransport::init()
{
    m_toolHandler = new McpToolHandler(this);
    m_transport = new McpSseTransport(m_toolHandler, this);
}

void TestMcpSseTransport::cleanup()
{
    if (m_transport) {
        m_transport->stop();
    }
    // m_transport and m_toolHandler are parented to `this`, auto-deleted
}

// ============================================================================
// Lifecycle tests
// ============================================================================

void TestMcpSseTransport::initialState_notRunning()
{
    QVERIFY(!m_transport->isRunning());
    QCOMPARE(m_transport->activeSessionCount(), 0);
}

void TestMcpSseTransport::start_success()
{
    bool ok = m_transport->start(m_testPort, QHostAddress::LocalHost);
    QVERIFY2(ok, "start() should succeed on available port");
    QVERIFY(m_transport->isRunning());
    QCOMPARE(m_transport->port(), m_testPort);
}

void TestMcpSseTransport::start_returnsFalse_whenPortBusy()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));

    // Try a second transport on the same port
    McpSseTransport second(m_toolHandler);
    bool ok = second.start(m_testPort, QHostAddress::LocalHost);
    QVERIFY2(!ok, "start() should fail when port is already bound");
    QVERIFY(!second.isRunning());
}

void TestMcpSseTransport::stop_success()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    m_transport->stop();
    QVERIFY(!m_transport->isRunning());
}

void TestMcpSseTransport::stop_whenNotRunning_isNoop()
{
    // Should not crash
    m_transport->stop();
    QVERIFY(!m_transport->isRunning());
}

void TestMcpSseTransport::port_returnsCorrectPort()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QCOMPARE(m_transport->port(), m_testPort);
}

void TestMcpSseTransport::activeSessionCount_initiallyZero()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QCOMPARE(m_transport->activeSessionCount(), 0);
}

// ============================================================================
// Session creation (GET /sse) tests
// ============================================================================

void TestMcpSseTransport::getSse_returnsHttp200()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);

    QVERIFY2(resp.contains("200"), "GET /sse should return HTTP 200");

    // Server will deleteLater() on disconnect
    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::getSse_contentTypeIsEventStream()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);

    QVERIFY(resp.contains("text/event-stream"));

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::getSse_sendsEndpointEvent()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);
    QString respStr = QString::fromUtf8(resp);

    QVERIFY2(respStr.contains("event: endpoint"),
             "Response must contain 'event: endpoint'");
    QVERIFY2(respStr.contains("/messages"),
             "Endpoint event must reference /messages path");

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::getSse_sessionIdIsUuid()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);
    QString respStr = QString::fromUtf8(resp);

    int sidStart = respStr.indexOf("sessionId=");
    QVERIFY(sidStart >= 0);
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

void TestMcpSseTransport::getSse_incrementsSessionCount()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QCOMPARE(m_transport->activeSessionCount(), 0);
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    sendRawHttp(socket, req, 3000);
    QTest::qWait(200);

    QCOMPARE(m_transport->activeSessionCount(), 1);

    socket->disconnectFromHost();
    QTest::qWait(500);
    QCOMPARE(m_transport->activeSessionCount(), 0);
}

// ============================================================================
// Message routing (POST /messages) tests
// ============================================================================

void TestMcpSseTransport::postMessages_withValidSession_returnsAccepted()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSession(sseSock);
    QVERIFY(!sid.isEmpty());

    // POST uses a separate connection (not the SSE stream)
    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QByteArray req = "POST /messages?sessionId=" + sid.toUtf8() + " HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;

    QByteArray resp = sendRawHttp(postSock, req, 3000);
    QVERIFY(resp.contains("202") || resp.contains("200"));

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpSseTransport::postMessages_withInvalidSession_returns404()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* postSock = new QTcpSocket();
    postSock->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(postSock->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QByteArray req = "POST /messages?sessionId=00000000-0000-0000-0000-000000000000 HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;

    QByteArray resp = sendRawHttp(postSock, req, 3000);
    QVERIFY(resp.contains("404"));

    postSock->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::postMessages_withoutSessionId_returns400()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
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
    QVERIFY(resp.contains("400"));

    postSock->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::postMessages_invalidJson_returnsParseErrorOnStream()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSession(sseSock);
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

    // Parse error should be pushed to the SSE stream
    QByteArray sseData = sseSock->readAll();
    QString sseStr = QString::fromUtf8(sseData);
    QVERIFY(sseStr.contains("-32700")); // JSON-RPC parse error code

    postSock->disconnectFromHost();
    sseSock->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpSseTransport::postMessages_validToolCall_returnsResultOnStream()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sseSock = nullptr;
    QString sid = openSession(sseSock);
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
// Unknown routes tests
// ============================================================================

void TestMcpSseTransport::getUnknownPath_returns404()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /unknown HTTP/1.1\r\nHost: localhost\r\n\r\n";
    QByteArray resp = sendRawHttp(socket, req, 3000);

    QVERIFY(resp.contains("404"));

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::postUnknownPath_returns404()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = "test";
    QByteArray req = "POST /unknown HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    QByteArray resp = sendRawHttp(socket, req, 3000);

    QVERIFY(resp.contains("404"));

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::putMethod_returns405()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray body = "test";
    QByteArray req = "PUT /sse HTTP/1.1\r\n"
                     "Host: localhost\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    QByteArray resp = sendRawHttp(socket, req, 3000);

    // PUT is not supported
    QVERIFY(resp.contains("405") || resp.contains("404") || resp.contains("501"));

    socket->disconnectFromHost();
    QTest::qWait(300);
}

// ============================================================================
// Session lifecycle tests
// ============================================================================

void TestMcpSseTransport::sessionCreated_signalEmitted()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QSignalSpy spy(m_transport, &McpSseTransport::sessionCreated);

    QTcpSocket* socket = new QTcpSocket();
    socket->connectToHost(QHostAddress::LocalHost, m_testPort);
    QVERIFY(socket->waitForConnected(3000));
    QTest::qWait(50);

    QByteArray req = "GET /sse HTTP/1.1\r\nHost: localhost\r\n\r\n";
    sendRawHttp(socket, req, 3000);
    QTest::qWait(300);

    QCOMPARE(spy.count(), 1);
    QString sid = spy[0][0].toString();
    QVERIFY(!sid.isEmpty());

    socket->disconnectFromHost();
    QTest::qWait(300);
}

void TestMcpSseTransport::sessionDestroyed_signalEmitted_onClientDisconnect()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sock = nullptr;
    QString sid = openSession(sock);
    QVERIFY(!sid.isEmpty());
    QCOMPARE(m_transport->activeSessionCount(), 1);

    QSignalSpy spy(m_transport, &McpSseTransport::sessionDestroyed);

    sock->disconnectFromHost();
    QTest::qWait(1000);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toString(), sid);
    QCOMPARE(m_transport->activeSessionCount(), 0);
    // sock is deleted by server via deleteLater
}

void TestMcpSseTransport::multipleSessions_uniqueIds()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QSignalSpy spy(m_transport, &McpSseTransport::sessionCreated);

    QTcpSocket* sock1 = nullptr;
    QTcpSocket* sock2 = nullptr;
    QTcpSocket* sock3 = nullptr;

    QString sid1 = openSession(sock1);
    QString sid2 = openSession(sock2);
    QString sid3 = openSession(sock3);

    QVERIFY(!sid1.isEmpty());
    QVERIFY(!sid2.isEmpty());
    QVERIFY(!sid3.isEmpty());

    QVERIFY(sid1 != sid2);
    QVERIFY(sid2 != sid3);
    QVERIFY(sid1 != sid3);

    QCOMPARE(spy.count(), 3);

    sock1->disconnectFromHost();
    sock2->disconnectFromHost();
    sock3->disconnectFromHost();
    QTest::qWait(500);
}

void TestMcpSseTransport::multipleSessions_allCounted()
{
    QVERIFY(m_transport->start(m_testPort, QHostAddress::LocalHost));
    QTest::qWait(50);

    QTcpSocket* sock1 = nullptr;
    QTcpSocket* sock2 = nullptr;

    openSession(sock1);
    QTest::qWait(200);
    QCOMPARE(m_transport->activeSessionCount(), 1);

    openSession(sock2);
    QTest::qWait(200);
    QCOMPARE(m_transport->activeSessionCount(), 2);

    sock1->disconnectFromHost();
    QTest::qWait(500);
    QCOMPARE(m_transport->activeSessionCount(), 1);

    sock2->disconnectFromHost();
    QTest::qWait(500);
    QCOMPARE(m_transport->activeSessionCount(), 0);
}

// ============================================================================
// Constants tests
// ============================================================================

void TestMcpSseTransport::defaultPort_matchesConstant()
{
    QCOMPARE(MCP_SSE_DEFAULT_PORT, quint16(8080));
}

void TestMcpSseTransport::defaultPath_matchesConstant()
{
    QCOMPARE(QByteArray(MCP_SSE_PATH_SSE), QByteArray("/sse"));
    QCOMPARE(QByteArray(MCP_SSE_PATH_MESSAGES), QByteArray("/messages"));
}

void TestMcpSseTransport::maxSessions_matchesConstant()
{
    QCOMPARE(MCP_SSE_MAX_SESSIONS, 16);
}

void TestMcpSseTransport::sessionTimeout_matchesConstant()
{
    // 30 minutes = 1,800,000 ms
    QCOMPARE(MCP_SSE_SESSION_TIMEOUT_MS, 1800000);
}

void TestMcpSseTransport::keepaliveInterval_matchesConstant()
{
    // 15 seconds = 15,000 ms
    QCOMPARE(MCP_SSE_KEEPALIVE_INTERVAL, 15000);
}

QTEST_MAIN(TestMcpSseTransport)
#include "test_mcp_sse_transport.moc"
