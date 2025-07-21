/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#ifndef HOSTMANAGER_H
#define HOSTMANAGER_H

#include <QObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QLoggingCategory>
#include <QKeySequence>
#include "../target/MouseManager.h"
#include "../target/KeyboardManager.h"
#include "../target/mouseeventdto.h"
#include "../ui/statusevents.h"
#include "../target/KeyboardLayouts.h"


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
    void handleMousePress(MouseEventDTO *event);
    void handleMouseRelease(MouseEventDTO *event);
    void handleMouseMove(MouseEventDTO *event);
    void handleMouseScroll(MouseEventDTO *event);
    
    void resetHid();
    void resetSerialPort();
    void setEventCallback(StatusEventCallback* callback);
    void restartApplication();
    void startAutoMoveMouse();
    void stopAutoMoveMouse();
    void pasteTextToTarget(QString text);

    void sendCtrlAltDel();

    void handleFunctionKey(int keyCode, int modifiers);

    void setRepeatingKeystroke(int interval);

    void handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown);

    void setKeyboardLayout(const QString& layoutName);
    
private:
    explicit HostManager(QObject *parent = nullptr);
    MouseManager mouseManager;
    KeyboardManager keyboardManager;
    StatusEventCallback* statusEventCallback = nullptr;
    bool m_repeatingKeystroke = false;
    int m_lastKeyCode = 0;
    int m_lastModifiers = 0;
    QTimer *m_repeatingTimer = nullptr;
    int m_repeatingInterval = 0;

private slots:
    void repeatLastKeystroke();

};

#endif // HOSTMANAGER_H
