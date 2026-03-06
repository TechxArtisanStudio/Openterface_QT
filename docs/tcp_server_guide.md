# TCP Server Guide — Architecture & Integration

This document provides an in-depth guide for setting up, configuring, and integrating the TCP Server in Openterface QT. For protocol details and client examples, see [tcp_protocol.md](tcp_protocol.md).

---

## Overview

The Openterface QT TCP Server enables remote control and monitoring via a lightweight, JSON-based protocol. It supports:
- **Remote screen capture** (FFmpeg/GStreamer backends)
- **Script execution** (AutoHotkey-compatible syntax)
- **Status monitoring** (operation state queries)
- **Multi-client connections** (sequential, single-client per connection)

**Default Configuration:**
- **Port**: 5900 (configurable)
- **Protocol**: TCP with JSON responses (UTF-8)
- **Platform Support**: Windows, Linux, macOS

---

## Architecture

### Core Components

#### TcpServer (server/tcpServer.h/cpp)
Main server class inheriting from `QTcpServer`.

**Key Responsibilities:**
- Listen for incoming connections on specified port
- Parse incoming commands (lastimage, gettargetscreen, checkstatus, or script)
- Route commands to appropriate handlers
- Manage frame buffer and client communication
- Coordinate with CameraManager and script execution

**Key Methods:**
```cpp
void startServer(quint16 port);              // Start listening
void setCameraManager(CameraManager* mgr);   // Initialize camera backend
QImage getCurrentFrameFromCamera();           // Thread-safe frame access
void processCommand(ActionCommand cmd);      // Command dispatcher
```

#### TcpResponse (server/tcpResponse.h/cpp)
Factory class for building standardized JSON responses.

**Response Types:**
- `TypeImage` - Image file data response
- `TypeScreen` - Screen capture response (base64-encoded JPEG)
- `TypeStatus` - Operation status (RUNNING, FINISH, FAIL)
- `TypeError` - Error message response
- `TypeUnknown` - Unrecognized command response

**Response Status Codes:**
- `Success` - Operation completed successfully
- `Error` - Fatal error (operation failed)
- `Warning` - Non-fatal issue (operation may continue)
- `Pending` - Operation in progress

**Factory Methods:**
```cpp
QByteArray createScreenResponse(const QByteArray& base64Data, int w, int h);
QByteArray createErrorResponse(const QString& errorMessage);
QByteArray createStatusResponse(const QString& status, const QString& msg = "");
QByteArray createImageResponse(const QByteArray& imageData, const QString& fmt, ...);
```

---

## Integration Guide

### Step 1: Initialize the Server

In your application initialization (e.g., `MainWindow::MainWindow()`):

```cpp
#include "server/tcpServer.h"
#include "host/cameramanager.h"

// Create TCP server instance
TcpServer* tcpServer = new TcpServer(this);

// Create or get camera manager
CameraManager* cameraManager = new CameraManager(this);

// CRITICAL: Set the camera manager before starting
tcpServer->setCameraManager(cameraManager);

// Start listening
tcpServer->startServer(5900);  // Default port

qDebug() << "TCP Server started on port 5900";
```

### Step 2: Connect Signals (Optional)

Connect signals for monitoring and advanced use cases:

```cpp
// Monitor script execution completion
connect(tcpServer, &TcpServer::syntaxTreeReady, 
        this, &MainWindow::onScriptReceived);

// Monitor key events from script execution
connect(tcpServer, &TcpServer::tcpServerKeyHandled, 
        this, &MainWindow::onScriptKeyEvent);
```

### Step 3: Configure Camera Backend

Ensure the camera/video stream is active before clients request screen capture:

```cpp
// Start video capture (platform-specific)
cameraManager->startVideo();  // or equivalent for your backend
```

---

## Frame Capture Mechanisms

The TCP server supports two frame capture backends:

### FFmpeg Backend (Windows, Linux, macOS)

**Flow:**
1. `CameraManager` continuously decodes video frames via FFmpeg
2. On each decoded frame, `imageCaptured(int id, QImage img)` signal is emitted
3. Signal is connected to `TcpServer::onImageCaptured()` with **direct connection**
4. Latest frame is stored atomically in `m_currentFrame` (mutex-protected)
5. On `gettargetscreen` request, frame is immediately available

**Advantages:**
- Frame always in memory and ready
- Minimal response latency
- No temp file I/O

**Code Reference:**
```cpp
// In tcpServer.cpp
void TcpServer::onImageCaptured(int id, const QImage& img) {
    QMutexLocker locker(&m_frameMutex);
    m_currentFrame = img;  // Atomic store
}

QImage TcpServer::getCurrentFrameFromCamera() {
    QMutexLocker locker(&m_frameMutex);
    return m_currentFrame;  // Thread-safe read
}
```

### GStreamer Backend (Linux Only)

**Flow:**
1. `gettargetscreen` request triggers `captureFrameFromGStreamer()`
2. Calls `GStreamerBackendHandler::takeImage(tempPath)` on-demand
3. Image saved to temp file (e.g., `/tmp/openterface_gst_frame.jpg`)
4. Temp file loaded into `QImage`
5. Temp file deleted immediately

**Advantages:**
- Lower memory footprint (no continuous frame buffering)
- On-demand capture (CPU efficient)

**Disadvantages:**
- Temp file I/O overhead (~50-100ms)
- Slightly higher latency

**Code Reference:**
```cpp
#ifndef Q_OS_WIN
QImage TcpServer::captureFrameFromGStreamer() {
    GStreamerBackendHandler* gstBackend = m_cameraManager->getGStreamerBackend();
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) 
                       + "/openterface_gst_frame.jpg";
    gstBackend->takeImage(tempPath);
    
    QImage image(tempPath);
    QFile::remove(tempPath);
    return image;
}
#endif
```

### Threading Model

- **Server thread**: Handles TCP connections, command parsing, response sending
- **Camera thread**: Captures frames (FFmpeg backend) or temp I/O (GStreamer)
- **Frame buffer**: Mutex (`m_frameMutex`) ensures safe multi-threaded access
- **Signal connection**: `Qt::DirectConnection` used for FFmpeg to bypass event loop

---

## Command Processing

### Command Parser

The server parses incoming commands (case-insensitive, trimmed):

```cpp
enum ActionCommand {
    CmdGetLastImage,    // "lastimage"
    CmdGetTargetScreen, // "gettargetscreen"
    CheckStatus,        // "checkstatus"
    ScriptCommand,      // Anything else
    CmdUnknow = -1
};

ActionCommand TcpServer::parseCommand(const QByteArray& data) {
    QString command = QString(data).trimmed().toLower();
    if (command == "lastimage")       return CmdGetLastImage;
    if (command == "gettargetscreen") return CmdGetTargetScreen;
    if (command == "checkstatus")     return CheckStatus;
    return ScriptCommand;
}
```

### Script Execution Pipeline

For unknown commands (treated as script statements):

1. **Lexer**: Tokenizes the input string (`scripts/Lexer.h`)
2. **Parser**: Builds Abstract Syntax Tree (AST) (`scripts/Parser.h`)
3. **Signal**: `syntaxTreeReady()` emitted with AST
4. **Execution**: AST interpreted and commands sent to device
5. **Response**: Status response sent back to client

**Example:**
```
Client sends: "Send ^c"
  ↓
parseCommand() → ScriptCommand
  ↓
Lexer tokenizes → [Token(Send), Token(^c)]
  ↓
Parser builds AST → ASTNode(Send command)
  ↓
syntaxTreeReady(ast) signal emitted
  ↓
Script executor interprets AST
  ↓
Send keyboard packet via SerialPortManager
  ↓
Response: { "status": "success", "data": { "state": "FINISH" } }
```

For supported script commands, see [script_tool.md](script_tool.md).

---

## Configuration & Customization

### Change Default Port

```cpp
// Instead of hardcoded 5900
int serverPort = settings.value("server/port", 5900).toInt();
tcpServer->startServer(serverPort);
```

### Persistent Configuration (QSettings)

```cpp
QSettings settings("Openterface", "Openterface_QT");

// Save port preference
settings.setValue("server/enabled", true);
settings.setValue("server/port", 5900);

// Load on startup
if (settings.value("server/enabled", false).toBool()) {
    int port = settings.value("server/port", 5900).toInt();
    tcpServer->startServer(port);
}
```

### Adjust JPEG Quality (Current)

Currently hardcoded to 90% quality in `sendScreenToClient()`. To make configurable:

```cpp
// In TcpServer
private:
    int m_jpegQuality = 90;  // Configurable

public:
    void setJpegQuality(int quality) { m_jpegQuality = qBound(10, quality, 100); }
```

---

## Error Handling & Recovery

### Common Failure Scenarios

#### Scenario 1: CameraManager Not Set
**Symptom**: `gettargetscreen` returns error
```json
{
  "type": "error",
  "status": "error",
  "message": "CameraManager not set. Call setCameraManager() first."
}
```
**Fix**: Ensure `setCameraManager()` is called during initialization

#### Scenario 2: No Frame Available
**Symptom**: FFmpeg backend, camera not running
```json
{
  "timestamp": "...",
  "message": "No frame available from FFmpeg backend - camera may not be running"
}
```
**Fix**: Start video capture before requesting screen

#### Scenario 3: GStreamer on Windows
**Symptom**: `captureFrameFromGStreamer()` returns null image
**Cause**: GStreamer backend only compiled on Linux (`#ifndef Q_OS_WIN`)
**Fix**: Use FFmpeg backend on Windows

---

## Debugging & Logging

### Enable TCP Server Logging

```cpp
// In main.cpp or initialization code
QLoggingCategory::setFilterRules("opf.server.tcp.debug=true");
```

### Log Output Examples

**Connection:**
```
opf.server.tcp: New client connected!
opf.server.tcp: Received data: "gettargetscreen"
```

**Frame Capture:**
```
opf.server.tcp: Frame captured and stored, size: QSize(1920, 1080)
opf.server.tcp: Screen data captured, JPEG size: 125432 bytes
```

**Error:**
```
opf.server.tcp: CameraManager not set. Call setCameraManager() first.
opf.server.tcp: Exception while capturing from GStreamer: ...
```

### Inspect Network Traffic

```bash
# Linux/macOS: Monitor TCP port 5900
netstat -an | grep 5900

# Capture traffic (requires tcpdump)
sudo tcpdump -i lo -A 'tcp port 5900'

# Test connection (netcat)
echo "gettargetscreen" | nc localhost 5900
```

---

## Performance Tuning

### Frame Size Impact

| Resolution | JPEG 90% | JPEG 75% | Base64 Size | Network (30 FPS) |
|------------|----------|----------|-------------|------------------|
| 640x480    | 25 KB    | 18 KB    | 33 KB      | 7.9 Mbps         |
| 1280x720   | 60 KB    | 45 KB    | 80 KB      | 19.2 Mbps        |
| 1920x1080  | 125 KB   | 90 KB    | 167 KB     | 40 Mbps          |

**Recommendation**: For remote over slow networks, reduce resolution or lower JPEG quality.

### Latency Optimization

**FFmpeg Backend:**
- Direct connection + frame already in memory = **~5-10ms** server-side latency
- Network round-trip dominates total latency

**GStreamer Backend:**
- Temp file I/O overhead = **~50-100ms** additional latency

**Optimization**: For low-latency scenarios, use FFmpeg on all platforms.

---

## Testing

### Unit Test Example (C++)

```cpp
#include <QtTest>
#include "server/tcpServer.h"

class TcpServerTest : public QObject {
    Q_OBJECT

private slots:
    void testServerStart() {
        TcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 5900));
        server.close();
    }

    void testFrameCapture() {
        // Setup: camera running, server initialized
        TcpServer server;
        CameraManager camMgr;
        
        server.setCameraManager(&camMgr);
        QImage frame = server.getCurrentFrameFromCamera();
        QVERIFY(!frame.isNull());
    }
};
```

### Integration Test Example (Python)

```python
import socket
import json
import time

def test_server_connectivity():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect(('localhost', 5900))
        sock.sendall(b'checkstatus\n')
        response = sock.recv(4096)
        data = json.loads(response)
        assert data['status'] in ['success', 'error']
    finally:
        sock.close()

def test_screen_capture():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('localhost', 5900))
    sock.sendall(b'gettargetscreen\n')
    
    response = sock.recv(1024 * 1024)  # 1MB buffer
    data = json.loads(response)
    
    assert data['type'] == 'screen'
    assert 'content' in data['data']
    sock.close()
```

---

## Future Enhancements

- [ ] **Streaming mode**: Continuous frame delivery (Motion JPEG over HTTP?)
- [ ] **Image format selection**: PNG, WebP, or raw YUV
- [ ] **Quality control**: Client-requested JPEG quality
- [ ] **Batch commands**: Multiple scripts in single request
- [ ] **Compression**: gzip for responses
- [ ] **Authentication**: Token/API key support
- [ ] **Rate limiting**: Prevent DoS attacks
- [ ] **Connection pooling**: Multiple simultaneous clients
- [ ] **Metrics**: Frame capture rate, response times, error rates

---

## Related Documentation

- [tcp_protocol.md](tcp_protocol.md) — Protocol reference and client examples
- [script_tool.md](script_tool.md) — Script commands and syntax
- [scripts/KeyboardMouse.h](../scripts/KeyboardMouse.h) — Key/mouse mappings
- [host/cameramanager.h](../host/cameramanager.h) — Camera backend interface
