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

#include "ChatInputRouter.h"
#include "../host/HostManager.h"
#include "../ui/customkey/customkeymanager.h"
#include <QThread>
#include <QLoggingCategory>
#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

ChatInputRouter::ChatInputRouter(QObject *parent)
    : QObject(parent)
{
}

ChatInputRouter &ChatInputRouter::instance()
{
    static ChatInputRouter inst;
    return inst;
}

int ChatInputRouter::normalizedToAbsolute(double normalized)
{
    return std::clamp(static_cast<int>(std::round(
        std::clamp(normalized, 0.0, 1.0) * 4096.0)), 0, 4096);
}

double ChatInputRouter::absoluteToNormalized(int absolute)
{
    return std::clamp(static_cast<double>(absolute) / 4096.0, 0.0, 1.0);
}

void ChatInputRouter::setTrackedMousePos(int x, int y)
{
    m_trackedMouseX = std::clamp(x, 0, 4096);
    m_trackedMouseY = std::clamp(y, 0, 4096);
}

void ChatInputRouter::sendMouseMove(int absX, int absY)
{
    int clampedX = std::clamp(absX, 0, 4096);
    int clampedY = std::clamp(absY, 0, 4096);
    m_trackedMouseX = clampedX;
    m_trackedMouseY = clampedY;

    HostManager::getInstance().getMouseManager().handleAbsoluteMouseAction(
        clampedX, clampedY, 0, 0);
}

void ChatInputRouter::animatedClick(int button, int absX, int absY, bool isDoubleClick)
{
    int targetX = std::clamp(absX, 0, 4096);
    int targetY = std::clamp(absY, 0, 4096);
    int startX = m_trackedMouseX;
    int startY = m_trackedMouseY;

    // Animate mouse movement to target
    if (startX != targetX || startY != targetY) {
        double stepDelay = ANIMATED_CLICK_DURATION_SECONDS / std::max(ANIMATED_CLICK_STEPS, 1);
        for (int step = 1; step <= ANIMATED_CLICK_STEPS; ++step) {
            double progress = static_cast<double>(step) / ANIMATED_CLICK_STEPS;
            int interpX = static_cast<int>(std::round(startX + (targetX - startX) * progress));
            int interpY = static_cast<int>(std::round(startY + (targetY - startY) * progress));
            sendMouseMove(interpX, interpY);
            QThread::msleep(static_cast<int>(stepDelay * 1000));
        }
    } else {
        sendMouseMove(targetX, targetY);
    }

    showClickOverlay(targetX, targetY);
    doClick(button, targetX, targetY, isDoubleClick);
}

void ChatInputRouter::animatedDrag(int absStartX, int absStartY, int absEndX, int absEndY)
{
    int startX = std::clamp(absStartX, 0, 4096);
    int startY = std::clamp(absStartY, 0, 4096);
    int endX = std::clamp(absEndX, 0, 4096);
    int endY = std::clamp(absEndY, 0, 4096);

    auto &mouse = HostManager::getInstance().getMouseManager();

    // Move to start
    sendMouseMove(startX, startY);
    QThread::msleep(50);

    // Press mouse button
    mouse.handleAbsoluteMouseAction(startX, startY, 1, 0); // left button press

    // Drag to end
    double stepDelay = ANIMATED_CLICK_DURATION_SECONDS / std::max(ANIMATED_CLICK_STEPS, 1);
    for (int step = 1; step <= ANIMATED_CLICK_STEPS; ++step) {
        double progress = static_cast<double>(step) / ANIMATED_CLICK_STEPS;
        int interpX = static_cast<int>(std::round(startX + (endX - startX) * progress));
        int interpY = static_cast<int>(std::round(startY + (endY - startY) * progress));
        mouse.handleAbsoluteMouseAction(interpX, interpY, 1, 0); // drag
        QThread::msleep(static_cast<int>(stepDelay * 1000));
    }

    // Release at end
    mouse.handleAbsoluteMouseAction(endX, endY, 0, 0);

    m_trackedMouseX = endX;
    m_trackedMouseY = endY;
}

void ChatInputRouter::doClick(int button, int absX, int absY, bool isDoubleClick)
{
    auto &mouse = HostManager::getInstance().getMouseManager();

    // Release (ensure clean state)
    mouse.handleAbsoluteMouseAction(absX, absY, 0, 0);
    QThread::msleep(40);
    // Press
    mouse.handleAbsoluteMouseAction(absX, absY, button, 0);
    QThread::msleep(40);
    // Release
    mouse.handleAbsoluteMouseAction(absX, absY, 0, 0);

    if (isDoubleClick) {
        QThread::msleep(120);
        mouse.handleAbsoluteMouseAction(absX, absY, button, 0);
        QThread::msleep(40);
        mouse.handleAbsoluteMouseAction(absX, absY, 0, 0);
    }
}

void ChatInputRouter::sendText(const QString &text)
{
    if (text.isEmpty()) return;

    // Check if text contains angle-bracket shortcuts that need conversion
    if (text.contains('<') && text.contains('>')) {
        // Parse text and convert angle-bracket shortcuts to key presses
        QString remaining = text;
        QString textBuffer;

        while (!remaining.isEmpty()) {
            int startIdx = remaining.indexOf('<');

            if (startIdx < 0) {
                // No more shortcuts, type the rest
                textBuffer += remaining;
                break;
            }

            // Add text before the shortcut
            if (startIdx > 0) {
                textBuffer += remaining.left(startIdx);
            }

            // Find closing bracket
            int endIdx = remaining.indexOf('>', startIdx);
            if (endIdx < 0) {
                // No closing bracket, treat rest as text
                textBuffer += remaining.mid(startIdx);
                break;
            }

            // Flush text buffer before sending shortcut
            if (!textBuffer.isEmpty()) {
                HostManager::getInstance().getKeyboardManager().pasteTextToTarget(textBuffer);
                textBuffer.clear();
                QThread::msleep(50);
            }

            // Extract and send the shortcut
            QString shortcut = remaining.mid(startIdx + 1, endIdx - startIdx - 1);
            sendShortcut(shortcut);

            // Move past this shortcut
            remaining = remaining.mid(endIdx + 1);
        }

        // Flush any remaining text
        if (!textBuffer.isEmpty()) {
            HostManager::getInstance().getKeyboardManager().pasteTextToTarget(textBuffer);
        }
    } else {
        // No shortcuts, just type the text
        HostManager::getInstance().getKeyboardManager().pasteTextToTarget(text);
    }
}

void ChatInputRouter::sendShortcut(const QString &shortcut)
{
    if (shortcut.isEmpty()) return;

    // Normalize the shortcut format - handle both "ctrl+alt+t" and "<ctrl><alt>t"
    QString normalized = shortcut.trimmed();

    // Convert angle-bracket format to plus-separated format
    // "<ctrl><alt>t" -> "ctrl+alt+t"
    // "<ctrl>l" -> "ctrl+l"
    // "<enter>" -> "enter"
    if (normalized.contains('<') || normalized.contains('>')) {
        QString converted;
        int i = 0;
        while (i < normalized.length()) {
            if (normalized[i] == '<') {
                // Find closing >
                int end = normalized.indexOf('>', i);
                if (end > i) {
                    if (!converted.isEmpty()) converted += '+';
                    converted += normalized.mid(i + 1, end - i - 1);
                    i = end + 1;
                } else {
                    i++;
                }
            } else if (normalized[i].isLetterOrNumber()) {
                // Regular key character(s)
                if (!converted.isEmpty() && !converted.endsWith('+')) {
                    converted += '+';
                }
                // Collect consecutive alphanumeric chars
                int start = i;
                while (i < normalized.length() && normalized[i].isLetterOrNumber()) {
                    i++;
                }
                converted += normalized.mid(start, i - start);
            } else {
                i++;
            }
        }
        normalized = converted;
    }

    qCDebug(log_ai_chat) << "AI shortcut: original=" << shortcut << "normalized=" << normalized;

    // Parse shortcut like "ctrl+l", "alt+f4", "ctrl+shift+t"
    QStringList parts = normalized.toLower().split('+');
    for (auto &p : parts) p = p.trimmed();
    if (parts.isEmpty()) return;

    // Last part is the key, rest are modifiers
    QString keyToken = parts.last();
    QStringList modifiers = parts.mid(0, parts.size() - 1);

    // Build Qt modifier flags
    int qtModifiers = 0;
    for (const auto &mod : modifiers) {
        if (mod == "ctrl" || mod == "control") qtModifiers |= Qt::ControlModifier;
        else if (mod == "alt" || mod == "option") qtModifiers |= Qt::AltModifier;
        else if (mod == "shift") qtModifiers |= Qt::ShiftModifier;
        else if (mod == "meta" || mod == "super" || mod == "win" || mod == "cmd") qtModifiers |= Qt::MetaModifier;
    }

    // Map the main key
    static const QMap<QString, int> namedKeys = {
        {"esc", Qt::Key_Escape}, {"escape", Qt::Key_Escape},
        {"enter", Qt::Key_Return}, {"return", Qt::Key_Return},
        {"tab", Qt::Key_Tab}, {"space", Qt::Key_Space},
        {"backspace", Qt::Key_Backspace}, {"delete", Qt::Key_Delete},
        {"home", Qt::Key_Home}, {"end", Qt::Key_End},
        {"pageup", Qt::Key_PageUp}, {"pagedown", Qt::Key_PageDown},
        {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
        {"left", Qt::Key_Left}, {"right", Qt::Key_Right},
        {"f1", Qt::Key_F1}, {"f2", Qt::Key_F2}, {"f3", Qt::Key_F3},
        {"f4", Qt::Key_F4}, {"f5", Qt::Key_F5}, {"f6", Qt::Key_F6},
        {"f7", Qt::Key_F7}, {"f8", Qt::Key_F8}, {"f9", Qt::Key_F9},
        {"f10", Qt::Key_F10}, {"f11", Qt::Key_F11}, {"f12", Qt::Key_F12}
    };

    int keyCode = 0;
    if (namedKeys.contains(keyToken)) {
        keyCode = namedKeys[keyToken];
    } else if (keyToken.length() == 1) {
        keyCode = keyToken.at(0).toUpper().unicode();
    }

    if (keyCode) {
        QList<KeyStep> steps;
        steps.append({qtModifiers, keyCode});
        HostManager::getInstance().handleKeySequence(steps);
        qCDebug(log_ai_chat) << "AI shortcut executed:" << shortcut
                             << "-> keyCode=" << keyCode << "modifiers=" << qtModifiers;
    } else {
        qCWarning(log_ai_chat) << "AI shortcut: unknown key token:" << keyToken;
    }
}

void ChatInputRouter::showClickOverlay(int absX, int absY)
{
    emit clickOverlayRequested(absX, absY);
}
