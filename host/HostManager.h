#ifndef HOSTMANAGER_H
#define HOSTMANAGER_H

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QLoggingCategory>
#include "../target/MouseManager.h"
#include "../target/KeyboardManager.h"
#include "../serial/SerialPortManager.h"
#include "../serial/serialportevents.h"

Q_DECLARE_LOGGING_CATEGORY(log_core_host)

class HostManager : public QObject
{
    Q_OBJECT

public:
    static HostManager& getInstance()
    {
        static HostManager instance; // Guaranteed to be destroyed.
                                    // Instantiated on first use.
        return instance;
    }

    HostManager(HostManager const&) = delete;             // Copy construct
    void operator=(HostManager const&)  = delete; // Copy assign

    void handleKeyPress(QKeyEvent *event);
    void handleKeyRelease(QKeyEvent *event);
    void handleMousePress(int x, int y, int mouseButton);
    void handleMouseRelease(int x, int y);
    void handleMouseMove(int x, int y, int mouseButton);
    void handleMouseScroll(int x, int y, int delta);
    
    void resetSerialPort();
    void setEventCallback(SerialPortEventCallback* callback);
    void restartApplication();

private:
    explicit HostManager(QObject *parent = nullptr);
    SerialPortManager serialPortManager;
    MouseManager mouseManager;
    KeyboardManager keyboardManager;
};

#endif // HOSTMANAGER_H
