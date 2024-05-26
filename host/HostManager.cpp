/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
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

#include "HostManager.h"
#include <QProcess>
#include <QCoreApplication>

Q_LOGGING_CATEGORY(log_core_host, "opf.core.host")

HostManager::HostManager(QObject *parent) : QObject(parent),
                                            serialPortManager(),
                                            mouseManager(serialPortManager),
                                            keyboardManager(serialPortManager)
{
}

void HostManager::setEventCallback(SerialPortEventCallback* callback)
{
    this->serialPortManager.setEventCallback(callback);
}

void HostManager::handleKeyPress(QKeyEvent *event)
{
    QString hexKeyCode = QString::number(event->key(), 16);
    // In order to distingush the left or right modifiers
    int modifiers = keyboardManager.isModiferKeys(event->key()) ? event->nativeModifiers() : event->modifiers();
    qCDebug(log_core_host) << "Key press event for qt key code:" << event->key() << "(" << hexKeyCode << "), modifers: " << modifiers;
    keyboardManager.handleKeyboardAction(event->key(), modifiers, true);
}

void HostManager::handleKeyRelease(QKeyEvent *event)
{
    QString hexKeyCode = QString::number(event->key(), 16);
    int modifiers = keyboardManager.isModiferKeys(event->key()) ? event->nativeModifiers() : event->modifiers();
    qCDebug(log_core_host) << "Key release event for qt key code:" << event->key() << "(" << hexKeyCode << "), modifer: " << modifiers;
    keyboardManager.handleKeyboardAction(event->key(), modifiers, false);
}

void HostManager::handleMousePress(int x, int y, int mouseButton)
{
    mouseManager.handleAbsoluteMouseAction(x, y, mouseButton, 0);
}

void HostManager::handleMouseRelease(int x, int y)
{
    qCDebug(log_core_host) << "Mouse released";
    mouseManager.handleAbsoluteMouseAction(x, y, 0, 0);
}

void HostManager::handleMouseScroll(int x, int y, int delta)
{
    mouseManager.handleAbsoluteMouseAction(x, y, 0, delta);
}
void HostManager::handleMouseMove(int x, int y, int mouseButton)
{
    // Add your implementation here
    //qCDebug(log_core_host) << "Mouse moved to (" << x << "," << y << ")";
    mouseManager.handleAbsoluteMouseAction(x, y, mouseButton, 0);
}

void HostManager::resetSerialPort()
{
    serialPortManager.closePort();
}

void HostManager::restartApplication() {
    // Save the application's path
    // QString appPath = QCoreApplication::applicationFilePath();

    // // Start a new instance of the application
    // QStringList arguments = QCoreApplication::arguments();
    // arguments.removeFirst(); // Remove the application's path from the arguments
    // QProcess::startDetached(appPath, arguments);

    // // Quit the current instance
    // QCoreApplication::quit();

}
