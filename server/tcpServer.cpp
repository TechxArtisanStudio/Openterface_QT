#include "tcpServer.h"
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_server_tcp, "opf.server.tcp")

TcpServer::TcpServer(QObject *parent) : QTcpServer(parent), currentClient(nullptr) {}

void TcpServer::startServer(quint16 port) {
    if (this->listen(QHostAddress::Any, port)) {
        qDebug() << "Server started on port:" << port;
        connect(this, &QTcpServer::newConnection, this, &TcpServer::onNewConnection);
    } else {
        qDebug() << "Server could not start!";
    }
}

void TcpServer::onNewConnection() {
    currentClient = this->nextPendingConnection();
    connect(currentClient, &QTcpSocket::readyRead, this, &TcpServer::onReadyRead);
    connect(currentClient, &QTcpSocket::disconnected, currentClient, &QTcpSocket::deleteLater);
    qCDebug(log_server_tcp) << "New client connected!";
}

void TcpServer::onReadyRead() {
    QByteArray data = currentClient->readAll();
    
    qCDebug(log_server_tcp) << "Received data:" << data;
    ActionCommand cmd = parseCommand(data);
    processCommand(cmd);
    // Process the data (this is where you handle the incoming data)
    
}

void TcpServer::handleImgPath(const QString& imagePath){
    lastImgPath = imagePath;
    qCDebug(log_server_tcp) << "img path updated: " << lastImgPath;
}

ActionCommand TcpServer::parseCommand(const QByteArray& data){
    QString command = QString(data).trimmed().toLower();

    if (command == "lastimage"){
        return CmdGetLastImage;
    }else if(command == "checkstatus") {
        return CheckStatus;
    }else{
        scriptStatement = QString::fromUtf8(data);
        return ScriptCommand;
    }
}

void TcpServer::sendImageToClient(){
    QByteArray responseData;
    
    if (!lastImgPath.isEmpty()) {
        QFile imageFile(lastImgPath);
        if (imageFile.open(QIODevice::ReadOnly)) {
            QByteArray imageData = imageFile.readAll();
            imageFile.close();
            
            responseData = "IMAGE:" + QByteArray::number(imageData.size()) + "\n";
            responseData.append(imageData);
        } else {
            responseData = "ERROR: Could not open image file";
        }
    } else {
        responseData = "ERROR: No image available";
    }
    
    if (currentClient->state() == QAbstractSocket::ConnectedState) {
        currentClient->write(responseData);
        qCDebug(log_server_tcp) << "Sending image to client";
        currentClient->flush();
    }
}

void TcpServer::processCommand(ActionCommand cmd){
    QByteArray responseData;
    switch (cmd)
    {
    case CmdGetLastImage:
        sendImageToClient();
        break;
    case CheckStatus:
        correponseClientStauts();
        break;
    default:
        compileScript();
        break;
    }
}

void TcpServer::compileScript(){
    if (scriptStatement.isEmpty()){
        qCDebug(log_server_tcp) << "The statement is empty";
        return;
    }
    if(actionStatus == Running){
        qCDebug(log_server_tcp) << "The statement is empty";
        return;
    }
    lexer.setSource(scriptStatement.toStdString());
    tokens = lexer.tokenize();

    Parser parser(tokens);
    std::shared_ptr<ASTNode> syntaxTree = parser.parse();
    emit syntaxTreeReady(syntaxTree);
}

void TcpServer::recvTCPCommandStatus(bool status){
    qCDebug(log_server_tcp) << "The command status: " << status;
    if (status) {
        actionStatus = Finish;
    }else{
        actionStatus = Fail;
    }
}

void TcpServer::correponseClientStauts(){
    QByteArray responseData;
    switch(actionStatus){
        case Finish:
            responseData = "STATUS:FINISH";
            break;
        case Running:
            responseData = "STATUS:RUNNING";
            break;
        case Fail:
            responseData = "STATUS:FAIL";
            break;
        default:
            responseData = "STATUS:UNKNOWN";
            break;
    }

    if(currentClient && currentClient->state() == QAbstractSocket::ConnectedState) {
        currentClient->write(responseData);
        qCDebug(log_server_tcp) << "Sending status to client:" << responseData;
        currentClient->flush();
    }
}
