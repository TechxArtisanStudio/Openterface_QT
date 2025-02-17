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

#ifndef MOUSEMANAGER_H
#define MOUSEMANAGER_H


#include "serial/SerialPortManager.h"
#include "ui/statusevents.h"

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_core_mouse)

#include <QThread>
#include <QCursor>
#include <random>

class MouseMoverThread : public QThread {
    Q_OBJECT

public:
    MouseMoverThread() : running(true) {}

    void run() override {
        int x = 4096; // Starting at the top-right corner
        int y = 1;
        int initialYForce = 0; // Initial downward force is 0
        int initialXForce = getRandomForce(); // Random initial horizontal force
        int screenHeight = 4096;
        int screenWidth = 4096;
    
        int yForce = initialYForce;
        int xForce = initialXForce;
        const int yAcceleration = 2; // Constant acceleration to simulate gravity
    
        while (running) {
            // Move the mouse within the screen boundaries
            while (running) {
                y += yForce;
                x += xForce;
    
                // Apply acceleration in the y direction
                yForce += yAcceleration;
    
                // Check for vertical boundaries
                if (y >= screenHeight) {
                    y = y - yAcceleration;
                    yForce = -yForce; // Reverse vertical direction
                } else if (y <= 0) {
                    y = + yAcceleration;
                    yForce = -yForce; // Reverse vertical direction
                }
    
                // Check for horizontal boundaries
                if (x >= screenWidth) {
                    x = screenWidth;
                    xForce = -xForce; // Reverse horizontal direction
                } else if (x <= 0) {
                    x = 0;
                    xForce = -xForce; // Reverse horizontal direction
                }
    
                moveMouse(x, y);
                QThread::msleep(50); // Small delay to simulate movement
            }
        }
    }

    void stop() {
        running = false;
    }

private:
    void moveMouse(int x, int y) {
        QByteArray data;

        data.append(MOUSE_ABS_ACTION_PREFIX);
        data.append(static_cast<char>(0));
        data.append(static_cast<char>(x & 0xFF));
        data.append(static_cast<char>((x >> 8) & 0xFF));
        data.append(static_cast<char>(y & 0xFF));
        data.append(static_cast<char>((y >> 8) & 0xFF));
        data.append(static_cast<char>(0));

        // send the data to serial
        SerialPortManager::getInstance().sendCommandAsync(data, false);
    }

    int getRandomForce() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(5, 20); // Random force between 1 and 20
        return dis(gen);
    }

    std::atomic<bool> running;
};


class MouseManager : public QObject
{
    Q_OBJECT

public:
    explicit MouseManager(QObject *parent = nullptr);
    ~MouseManager();

    void handleAbsoluteMouseAction(int x, int y, int mouse_event, int wheelMovement);
    void handleRelativeMouseAction(int dx, int dy, int mouse_event, int wheelMovement);
    void setEventCallback(StatusEventCallback* callback);
    void startAutoMoveMouse();
    void stopAutoMoveMouse();

    void reset() {
        // Reset any internal state
        // For example, clear any stored coordinates or button states
        qDebug() << "Mouse manager reset";
    }

private:
    bool isDragging = false; 
    StatusEventCallback* statusEventCallback = nullptr;

    uint8_t mapScrollWheel(int delta);
    MouseMoverThread* mouseMoverThread = nullptr;
};

#endif // MOUSEMANAGER_H
