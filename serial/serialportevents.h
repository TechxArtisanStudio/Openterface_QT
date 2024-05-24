#ifndef SERIALPORTEVENTS_H
#define SERIALPORTEVENTS_H

#include <QObject>
#include <QString>
#include <QPoint>

class SerialPortEventCallback
{
public:
    virtual ~SerialPortEventCallback() = default;

    virtual void onPortConnected(const QString& port) = 0;
    virtual void onLastKeyPressed(const QString& key) = 0;
    virtual void onLastMouseLocation(const QPoint& location) = 0;
};

#endif
