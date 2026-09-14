#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QPointer>
#include <QString>
#include <QFile>
#include <QImage>
#include <QMutex>
#include "../scripts/Lexer.h"
#include "../scripts/Parser.h"
#include "tcpResponse.h"

class CameraManager;
#ifndef Q_OS_WIN
class GStreamerBackendHandler;
#endif

enum ActionCommand {
    CmdUnknow = -1,
    CmdGetLastImage,
    CmdGetTargetScreen,
    CheckStatus,
    ScriptCommand
};

enum ActionStatus{
    Finish,
    Running,
    Fail
};

class TcpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit TcpServer(QObject *parent = nullptr);

    // Start listening. Returns false if the socket could not be bound.
    // The server has no authentication, so it binds to loopback by default;
    // set OPENTERFACE_TCP_BIND_ADDRESS (an IP address or "any") to expose it.
    bool startServer(quint16 port, const QHostAddress& address = defaultBindAddress());
    static QHostAddress defaultBindAddress();
    void setCameraManager(CameraManager* cameraManager);

signals:
    void syntaxTreeReady(std::shared_ptr<ASTNode> syntaxTree);
    void tcpServerKeyHandled(const QString& key);

public slots:
    void handleImgPath(const QString& imagePath);
    void recvTCPCommandStatus(bool status);
    void onImageCaptured(int id, const QImage& img);
    QImage getCurrentFrameFromCamera();

private slots:
    void onNewConnection();
    void onReadyRead();
    
private:
    // QPointer: nulls itself when the socket is deleted, so a client that
    // disconnects mid-command can never be dereferenced after deletion.
    QPointer<QTcpSocket> currentClient;
    QString lastImgPath;
    CameraManager* m_cameraManager;
    QImage m_currentFrame;
    QMutex m_frameMutex;
    ActionCommand parseCommand(const QByteArray& data);
    void sendImageToClient();
    void sendScreenToClient();
#ifndef Q_OS_WIN
    QImage captureFrameFromGStreamer();
#endif
    void processCommand(ActionCommand cmd);
    Lexer lexer;
    std::vector<Token> tokens;
    QString scriptStatement;
    void compileScript();
    ActionStatus actionStatus;
    void correponseClientStauts();
};


#endif // TCPSERVER_H
