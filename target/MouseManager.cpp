// MouseManager.cpp
#include "MouseManager.h"

Q_LOGGING_CATEGORY(log_core_mouse, "opf.host.mouse")

MouseManager::MouseManager(SerialPortManager& spm, QObject *parent) : QObject(parent), serialPortManager(spm){
    // Initialization code here...
}

void MouseManager::handleAbsoluteMouseAction(int x, int y, int mouse_event, int wheelMovement) {
    // build a array
    QByteArray data;
    if (mouse_event > 0){
        qCDebug(log_core_mouse) << "mouse_event:" << mouse_event;
    }
    uint8_t mappedWheelMovement = mapScrollWheel(wheelMovement);
    if(mappedWheelMovement>0){    qCDebug(log_core_mouse) << "mappedWheelMovement:" << mappedWheelMovement; }
    data.append(SerialPortManager::MOUSE_ABS_ACTION_PREFIX);
    data.append(static_cast<char>(mouse_event));
    data.append(static_cast<char>(x & 0xFF));
    data.append(static_cast<char>((x >> 8) & 0xFF));
    data.append(static_cast<char>(y & 0xFF));
    data.append(static_cast<char>((y >> 8) & 0xFF));
    data.append(static_cast<char>(mappedWheelMovement & 0xFF));
    // send the data to serial
    serialPortManager.sendCommand(data, false);
}

void MouseManager::handleRelativeMouseAction(int dx, int dy, int mouse_event, int wheelMovement) {
    // Handle relative mouse action here...
}

uint8_t MouseManager::mapScrollWheel(int delta){
    if(delta == 0){
        return 0;
    }else if(delta > 0){
        return uint8_t(delta / 100);
    }else{
        return 0xFF - uint8_t(-1*delta / 100)+1;
    }
}
