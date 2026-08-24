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

#ifndef CHAT_INPUT_ROUTER_H
#define CHAT_INPUT_ROUTER_H

#include <QObject>
#include <QString>
#include <QPoint>

/**
 * @brief Routes AI-generated mouse/keyboard input to the target via HostManager.
 *
 * Ported from MacOS AIInputRouter.swift. Coordinates are in the 0-4096 range
 * matching the Openterface absolute mouse coordinate system.
 */
class ChatInputRouter : public QObject
{
    Q_OBJECT

public:
    static ChatInputRouter &instance();

    /// Send absolute mouse move (0-4096 range)
    void sendMouseMove(int absX, int absY);

    /// Animated mouse move + click
    void animatedClick(int button, int absX, int absY, bool isDoubleClick = false);

    /// Animated drag from start to end
    void animatedDrag(int absStartX, int absStartY, int absEndX, int absEndY);

    /// Send text to target keyboard
    void sendText(const QString &text);

    /// Send keyboard shortcut (e.g., "ctrl+l", "alt+f4")
    void sendShortcut(const QString &shortcut);

    /// Get/set tracked mouse position
    int trackedMouseX() const { return m_trackedMouseX; }
    int trackedMouseY() const { return m_trackedMouseY; }
    void setTrackedMousePos(int x, int y);

    /// Convert normalized (0.0-1.0) to absolute (0-4096)
    static int normalizedToAbsolute(double normalized);

    /// Convert absolute (0-4096) to normalized (0.0-1.0)
    static double absoluteToNormalized(int absolute);

signals:
    /// Emitted to show a click overlay on the video pane
    void clickOverlayRequested(int absX, int absY);

private:
    explicit ChatInputRouter(QObject *parent = nullptr);

    int m_trackedMouseX = 2048;
    int m_trackedMouseY = 2048;

    static constexpr double ANIMATED_CLICK_DURATION_SECONDS = 2.0;
    static constexpr int ANIMATED_CLICK_STEPS = 24;

    void showClickOverlay(int absX, int absY);
    void doClick(int button, int absX, int absY, bool isDoubleClick);
};

#endif // CHAT_INPUT_ROUTER_H
