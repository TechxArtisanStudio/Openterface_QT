#ifndef SERIALPORTMANAGER_H
#define SERIALPORTMANAGER_H

#include "serialportevents.h"
#include <QObject>
#include <QDebug>
#include <QSerialPort>
#include <QThread>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

class SerialPortManager : public QObject
{
    Q_OBJECT

public:
    static const QByteArray MOUSE_ABS_ACTION_PREFIX;
    static const QByteArray MOUSE_REL_ACTION_PREFIX;
    static const QByteArray CMD_GET_PARA_CFG;
    static const QByteArray CMD_RESET;

    static const int ORIGINAL_BAUDRATE = 9600;
    static const int DEFAULT_BAUDRATE = 115200;
    
    explicit SerialPortManager(QObject *parent = nullptr);
    ~SerialPortManager();

    void setEventCallback(SerialPortEventCallback* callback);
    bool openPort(const QString &portName, int baudRate);
    void closePort();

    bool writeData(const QByteArray &data);
    bool sendCommand(const QByteArray &data, bool force);
    void resetSerialPort();

signals:
    void dataReceived(const QByteArray &data);

private slots:
    void checkSerialPort();
    void initializeSerialPort();
    void observerSerialPortNotification();
    void readData();
    void aboutToClose();
    void bytesWritten(qint64 bytes);

    static quint8 calculateChecksum(const QByteArray &data);
    //void checkSerialPortConnection();
    QString getPortName();

private:
    QSerialPort *serialPort;
    QThread *serialThread;
    QList<QSerialPortInfo> m_lastPortList;
    bool ready = false;
    SerialPortEventCallback* eventCallback = nullptr;
};

#endif // SERIALPORTMANAGER_H
