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

#include "HostManager.h"
#include "../serial/SerialPortManager.h"
#include <QProcess>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>

Q_LOGGING_CATEGORY(log_core_host, "opf.core.host")

HostManager::HostManager(QObject *parent) : QObject(parent),
                                            mouseManager(),
                                            keyboardManager()
{
}

void HostManager::setEventCallback(StatusEventCallback* callback)
{
    qCDebug(log_core_host) << "HostManager.setEventCallback";
    SerialPortManager::getInstance().setEventCallback(callback);
    mouseManager.setEventCallback(callback);
}

void HostManager::handleKeyPress(QKeyEvent *event)
{
    QString hexKeyCode = QString::number(event->key(), 16);
    // In order to distingush the left or right modifiers
    int modifiers = keyboardManager.isModiferKeys(event->key()) ? event->nativeModifiers() : event->modifiers();
    qCDebug(log_core_host) << "Key press event for qt key code:" << event->key() << "(" << hexKeyCode << "), modifers:" << "0x" + QString::number(modifiers, 16);
    keyboardManager.handleKeyboardAction(event->key(), modifiers, true);
}

void HostManager::handleKeyRelease(QKeyEvent *event)
{
    QString hexKeyCode = QString::number(event->key(), 16);
    int modifiers = keyboardManager.isModiferKeys(event->key()) ? event->nativeModifiers() : event->modifiers();
    qCDebug(log_core_host) << "Key release event for qt key code:" << event->key() << "(" << hexKeyCode << "), modifer:" << "0x" + QString::number(modifiers, 16);
    keyboardManager.handleKeyboardAction(event->key(), modifiers, false);
}

void HostManager::handleMousePress(MouseEventDTO *event)
{
    if(event->isAbsoluteMode()) {
        mouseManager.handleAbsoluteMouseAction(event->getX(), event->getY(), event->getMouseButton(), 0);
    } else {
        mouseManager.handleRelativeMouseAction(event->getX(), event->getY(), event->getMouseButton(), 0);
    }
}

void HostManager::handleMouseRelease(MouseEventDTO *event)
{
    if(event->isAbsoluteMode()) {
        mouseManager.handleAbsoluteMouseAction(event->getX(), event->getY(), 0, 0);
    } else {
        mouseManager.handleRelativeMouseAction(event->getX(), event->getY(), 0, 0);
    }
}

void HostManager::handleMouseScroll(MouseEventDTO *event)
{
    if(event->isAbsoluteMode()) {
        mouseManager.handleAbsoluteMouseAction(event->getX(), event->getY(), 0, event->getWheelDelta());
    } else {
        mouseManager.handleRelativeMouseAction(event->getX(), event->getY(), 0, event->getWheelDelta());
    }
}

void HostManager::handleMouseMove(MouseEventDTO *event)
{
    if(event->isAbsoluteMode()) {
        mouseManager.handleAbsoluteMouseAction(event->getX(), event->getY(), event->getMouseButton(), 0);
    } else {
        mouseManager.handleRelativeMouseAction(event->getX(), event->getY(), event->getMouseButton(), 0);
    }
}

void HostManager::resetHid()
{
    SerialPortManager::getInstance().resetHipChip();
}

void HostManager::resetSerialPort()
{
    if(SerialPortManager::getInstance().restartPort()) {
        qCDebug(log_core_host) << "Serial port restarted successfully";
    } else {
        qCDebug(log_core_host) << "Serial port restart failed";
    }
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

void HostManager::pasteTextToTarget(QString text){
    qCDebug(log_core_host) << "Paste text to target: " << text;
    keyboardManager.pasteTextToTarget(text);
}

void HostManager::autoMoveMouse()
{
    mouseManager.startAutoMoveMouse();
}
