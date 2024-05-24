#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include "../serial/SerialPortManager.h"

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_host_keyboard)

class KeyboardManager: public QObject
{
    Q_OBJECT

public:
    explicit KeyboardManager(SerialPortManager& spm, QObject *parent = nullptr);

    void handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown);

    bool isModiferKeys(int keycode);

private:
    SerialPortManager& serialPortManager;
    static const QMap<int, uint8_t> keyMap;
    static const QList<int> SHIFT_KEYS;
    static const QList<int> CTRL_KEYS;
    static const QList<int> ALT_KEYS;
    
    int handleKeyModifiers(int modifierKeyCode, bool isKeyDown);
    int currentModifiers = 0;
};

#endif // KEYBOARDMANAGER_H
