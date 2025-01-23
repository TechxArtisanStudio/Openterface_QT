#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>

class TcpServer : public QTcpServer {
    Q_OBJECT

public:
    explicit TcpServer(QObject *parent = nullptr);
    void startServer(quint16 port);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpSocket *currentClient;
    void captureFullScreen();
};


#endif // TCPSERVER_H
