// MouseManager.h
#ifndef MOUSEMANAGER_H
#define MOUSEMANAGER_H


#include "serial/SerialPortManager.h"

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_core_mouse)

class MouseManager : public QObject
{
    Q_OBJECT

public:
    explicit MouseManager(SerialPortManager& spm, QObject *parent = nullptr);

    void handleAbsoluteMouseAction(int x, int y, int mouse_event, int wheelMovement);
    void handleRelativeMouseAction(int dx, int dy, int mouse_event, int wheelMovement);

private:
    SerialPortManager& serialPortManager;
    bool isDragging = false; 
    
    uint8_t mapScrollWheel(int delta);
    // ... rest of your class definition ...
};

#endif // MOUSEMANAGER_H
