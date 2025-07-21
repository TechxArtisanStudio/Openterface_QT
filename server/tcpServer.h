#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QFile>
#include "../scripts/Lexer.h"
#include "../scripts/Parser.h"

enum ActionCommand {
    CmdUnknow = -1,
    CmdGetLastImage,
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
    void startServer(quint16 port);

signals:
    void syntaxTreeReady(std::shared_ptr<ASTNode> syntaxTree);

public slots:
    void handleImgPath(const QString& imagePath);
    void recvTCPCommandStatus(bool status);

private slots:
    void onNewConnection();
    void onReadyRead();
    
private:
    QTcpSocket *currentClient;
    QString lastImgPath;
    ActionCommand parseCommand(const QByteArray& data);
    void sendImageToClient();
    void processCommand(ActionCommand cmd);
    Lexer lexer;
    std::vector<Token> tokens;
    QString scriptStatement;
    void compileScript();
    ActionStatus actionStatus;
    void correponseClientStauts();
};


#endif // TCPSERVER_H
