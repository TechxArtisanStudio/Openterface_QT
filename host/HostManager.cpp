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
#include <QTimer>
#include <QRandomGenerator>

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
    statusEventCallback = callback;
}

void HostManager::handleKeyPress(QKeyEvent *event)
{
    handleKeyboardAction(event->key(), event->modifiers(), true);
}

void HostManager::handleKeyRelease(QKeyEvent *event)
{
    handleKeyboardAction(event->key(), event->modifiers(), false);
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

void HostManager::startAutoMoveMouse()
{
    mouseManager.startAutoMoveMouse();
}

void HostManager::stopAutoMoveMouse()
{
    mouseManager.stopAutoMoveMouse();
}

void HostManager::sendCtrlAltDel()
{
    qCDebug(log_core_host) << "Sending Ctrl+Alt+Del to target";
    keyboardManager.sendCtrlAltDel();
}

void HostManager::handleFunctionKey(int keyCode, int modifiers)
{
    handleKeyboardAction(keyCode, modifiers, true);
    QTimer::singleShot(50, this, [this, keyCode, modifiers]() {
        handleKeyboardAction(keyCode, modifiers, false);
    });
}

void HostManager::handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown)
{
    QString hexKeyCode = QString::number(keyCode, 16);
    int effectiveModifiers = keyboardManager.isModiferKeys(keyCode) ? modifiers : modifiers;
    qCDebug(log_core_host) << (isKeyDown ? "Key press" : "Key release") << "event for qt key code:" << keyCode << "(" << hexKeyCode << "), modifers:" << "0x" + QString::number(effectiveModifiers, 16);
    
    keyboardManager.handleKeyboardAction(keyCode, effectiveModifiers, isKeyDown);
    
    if (isKeyDown) {
        qCDebug(log_core_host) << "Key press event detected with keyCode:" << keyCode << " and modifiers:" << effectiveModifiers;
        m_lastKeyCode = keyCode;
        m_lastModifiers = effectiveModifiers;

        // Update the status with the last key pressed
        if (statusEventCallback != nullptr) {
            QString keyText;
            // Handle modifier keys specially
            if (keyboardManager.isModiferKeys(keyCode)) {
                // For modifier keys, just use the key name without combining with modifiers
                keyText = QKeySequence(effectiveModifiers).toString();
            } else {
                keyText = QKeySequence(keyCode | effectiveModifiers).toString();
            }
            qCDebug(log_core_host) << "onLastKeyPressed:" << keyText;
            statusEventCallback->onLastKeyPressed(keyText);
        }
    } else {
        // When key is released, clear the status
        if (statusEventCallback != nullptr) {
            statusEventCallback->onLastKeyPressed("");
        }
    }
}

void HostManager::setRepeatingKeystroke(int interval) {
    m_repeatingInterval = interval;
    if (interval > 0) {
        if (!m_repeatingTimer) {
            m_repeatingTimer = new QTimer(this);
            connect(m_repeatingTimer, &QTimer::timeout, this, &HostManager::repeatLastKeystroke);
        }
        qCDebug(log_core_host) << "Repeating keystroke start with interval:" << interval << "ms";
        m_repeatingTimer->start(interval);
    } else {
        if (m_repeatingTimer) {
            qCDebug(log_core_host) << "Repeating keystroke stopped";
            m_repeatingTimer->stop();
        }
        // Send a key release event for the last pressed key
        if (m_lastKeyCode != 0) {
            qCDebug(log_core_host) << "Sending key release for last pressed key:" << m_lastKeyCode;
            handleKeyboardAction(m_lastKeyCode, m_lastModifiers, false);
        }
        // Clear the last key code and modifier when stopping repetition
        m_lastKeyCode = 0;
        m_lastModifiers = 0;
        qCDebug(log_core_host) << "Last key code and modifier cleared";
    }
}

void HostManager::repeatLastKeystroke() {
    if (m_repeatingInterval > 0 && m_lastKeyCode != 0) {
        qCDebug(log_core_host) << "Repeating keystroke, keyCode:" << m_lastKeyCode;
        handleKeyboardAction(m_lastKeyCode, m_lastModifiers, true);
        QTimer::singleShot(50, this, [this]() {
            handleKeyboardAction(m_lastKeyCode, m_lastModifiers, false);
        });
    }
}

void HostManager::setKeyboardLayout(const QString& layoutName) {
    qCDebug(log_core_host) << "Keyboard layout changed to" << layoutName;
    keyboardManager.setKeyboardLayout(layoutName);
}