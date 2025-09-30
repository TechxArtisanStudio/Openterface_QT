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

#ifndef STATUSWIDGET_H
#define STATUSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>

class StatusWidget : public QWidget {
    Q_OBJECT

public:
    explicit StatusWidget(QWidget *parent = nullptr);
    ~StatusWidget();

    void setInputResolution(const int &width, const int &height, const float &fps, const float &pixelClk);
    void setCaptureResolution(const int &width, const int &height, const float &fps);
    void setKeyboardIndicators(const QString &indicators);
    void setConnectedPort(const QString &port, const int &baudrate);
    void setStatusUpdate(const QString &status);
    void setTargetUsbConnected(const bool isConnected);
    void setKeyStates(bool numLock, bool capsLock, bool scrollLock);
    int getCaptureWidth() const;
    int getCaptureHeight() const;

private slots:
    void updateCpuUsage();

public slots:
    void setBaudrate(int baudrate);

private:
    QLabel *statusLabel;
    QLabel *cpuUsageLabel;
    QLabel *keyboardIndicatorsLabel;
    QLabel *keyStatesLabel;
    QLabel *resolutionLabel;
    QLabel *inputResolutionLabel;
    QLabel *connectedPortLabel;
    QTimer *cpuTimer;
    int m_captureWidth;
    int m_captureHeight;
    float m_captureFramerate;
    
    double getCpuUsage();
};

#endif // STATUSWIDGET_H
