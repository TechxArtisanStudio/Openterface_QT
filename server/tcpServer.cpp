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

    // Process the data (this is where you handle the incoming data)
    QByteArray responseData = "Processed: " + data; // Example processing

    // Send response back to the client
    currentClient->write(responseData);
    currentClient->flush();
}


void TcpServer::captureFullScreen(){

}
