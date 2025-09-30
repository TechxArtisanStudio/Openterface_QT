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

#include "statuswidget.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QHBoxLayout>
#include <QProcess>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
#include <unistd.h>
#include <sys/times.h>
#endif

StatusWidget::StatusWidget(QWidget *parent) : QWidget(parent), m_captureWidth(0), m_captureHeight(0), m_captureFramerate(0.0) {
    keyboardIndicatorsLabel = new QLabel("", this);
    keyStatesLabel = new QLabel("", this);
    statusLabel = new QLabel("", this);
    cpuUsageLabel = new QLabel("🖥️: 0%", this);
    resolutionLabel = new QLabel("💻:", this);
    inputResolutionLabel = new QLabel("INPUT(NA)", this);
    connectedPortLabel = new QLabel("🔌: N/A", this);

    // Setup CPU monitoring timer
    cpuTimer = new QTimer(this);
    connect(cpuTimer, &QTimer::timeout, this, &StatusWidget::updateCpuUsage);
    cpuTimer->start(2000); // Update every 2 seconds

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    layout->addWidget(statusLabel);
    layout->addWidget(new QLabel("| ", this));
    layout->addWidget(cpuUsageLabel);
    layout->addWidget(new QLabel("| ", this));
    layout->addWidget(keyboardIndicatorsLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(keyStatesLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(connectedPortLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(resolutionLabel);
    layout->addWidget(inputResolutionLabel);

    setLayout(layout);
    setMinimumHeight(30);
    
    // Initialize key states display
    setKeyStates(false, false, false);
    update();
}

StatusWidget::~StatusWidget() {
#ifdef Q_OS_WIN
    // Clean up Windows PDH resources if they were initialized
    // Note: This cleanup is handled automatically when the process ends,
    // but it's good practice to clean up explicitly
#endif
}

void StatusWidget::setInputResolution(const int &width, const int &height, const float &fps, const float &pixelClk) {
    if(width == 0 || height == 0 || fps == 0) {
        inputResolutionLabel->setText("INPUT(NA)");
        inputResolutionLabel->setToolTip("Input video is not available");
        update();
        return;
    }
    inputResolutionLabel->setText(QString("INPUT(%1X%2@%3)").arg(width).arg(height).arg(fps));
    
    // Create tooltip with both input and capture resolution info
    QString tooltip = QString("Input Resolution: %1 x %2@%3\nPixel Clock: %4Mhz").arg(width).arg(height).arg(fps).arg(pixelClk);
    
    // Add capture resolution info if available
    if (m_captureWidth > 0 && m_captureHeight > 0 && m_captureFramerate > 0) {
        tooltip += QString("\nCapture Resolution: %1 x %2@%3").arg(m_captureWidth).arg(m_captureHeight).arg(m_captureFramerate);
    }

    inputResolutionLabel->setToolTip(tooltip);
    update(); 
}

void StatusWidget::setCaptureResolution(const int &width, const int &height, const float &fps) {
    m_captureWidth = width;
    m_captureHeight = height;
    m_captureFramerate = fps;

    
    // Update the input resolution tooltip to include capture information
    QString currentTooltip = inputResolutionLabel->toolTip();
    
    // Remove any existing capture info from tooltip
    QStringList lines = currentTooltip.split('\n');
    QStringList filteredLines;
    for (const QString &line : lines) {
        if (!line.contains("Capture Resolution:")) {
            filteredLines.append(line);
        }
    }
    
    // Add the new capture resolution info
    if (width > 0 && height > 0 && fps > 0) {
        filteredLines.append(QString("Capture Resolution: %1 x %2@%3").arg(width).arg(height).arg(fps));
    }
    
    inputResolutionLabel->setToolTip(filteredLines.join('\n'));
    update(); 
}

void StatusWidget::setConnectedPort(const QString &port, const int &baudrate) {
    if(baudrate > 0){
        connectedPortLabel->setText(QString("🔌: %1@%2").arg(port).arg(baudrate));
    }else{
        connectedPortLabel->setText(QString("🔌: N/A"));
    }
    update(); 
}

void StatusWidget::setStatusUpdate(const QString &status){
    statusLabel->setText(status);
    update();
}

void StatusWidget::setTargetUsbConnected(const bool isConnected){
    QString keyboardSvgPath = ":/images/keyboard.svg";
    QString mouseSvgPath = ":/images/mouse-default.svg";
    QColor fillColor = isConnected ? QColor(0, 255, 0, 128) : QColor(255, 0, 0, 200);
    
    QPixmap combinedPixmap(36, 18);
    combinedPixmap.fill(Qt::transparent);
    QPainter painter(&combinedPixmap);
    
    // Render keyboard
    QSvgRenderer keyboardRenderer(keyboardSvgPath);
    keyboardRenderer.render(&painter, QRectF(0, 0, 18, 18));
    
    // Render mouse
    QSvgRenderer mouseRenderer(mouseSvgPath);
    QRectF mouseRect(18, 1.8, 14.4, 14.4);  // 20% smaller, centered vertically
    mouseRenderer.render(&painter, mouseRect);
    
    // Apply color overlay
    painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    painter.fillRect(combinedPixmap.rect(), fillColor);
    painter.end();
    
    keyboardIndicatorsLabel->setPixmap(combinedPixmap);
    update();
}

void StatusWidget::setBaudrate(int baudrate)
{
    // Update the UI element that displays the baudrate
    connectedPortLabel->setText(QString("🔌: %1@%2").arg(connectedPortLabel->text().split('@').first()).arg(baudrate));
    update();
}

void StatusWidget::setKeyStates(bool numLock, bool capsLock, bool scrollLock)
{
    QString keyStatesText;
    QStringList activeKeys;
    
    if (numLock) {
        activeKeys << "NUM";
    }
    if (capsLock) {
        activeKeys << "CAPS";
    }
    if (scrollLock) {
        activeKeys << "SCROLL";
    }
    
    if (activeKeys.isEmpty()) {
        keyStatesText = "⌨️: ---";
    } else {
        keyStatesText = QString("⌨️: %1").arg(activeKeys.join("|"));
    }
    
    keyStatesLabel->setText(keyStatesText);
    
    // Set tooltip with detailed information
    QString tooltip = QString("Keyboard Lock States:\nNum Lock: %1\nCaps Lock: %2\nScroll Lock: %3")
                     .arg(numLock ? "ON" : "OFF")
                     .arg(capsLock ? "ON" : "OFF")
                     .arg(scrollLock ? "ON" : "OFF");
    keyStatesLabel->setToolTip(tooltip);
    
    update();
}

// Implement the new methods:
int StatusWidget::getCaptureWidth() const
{
    return m_captureWidth;
}

int StatusWidget::getCaptureHeight() const
{
    return m_captureHeight;
}

void StatusWidget::updateCpuUsage()
{
    double cpuUsage = getCpuUsage();
    if (cpuUsage >= 0) {
        cpuUsageLabel->setText(QString("🖥️: %1%").arg(QString::number(cpuUsage, 'f', 1)));
        
        // Set tooltip with more detailed info
        cpuUsageLabel->setToolTip(QString("App CPU Usage: %1%").arg(QString::number(cpuUsage, 'f', 1)));
        
        // Optional: Change color based on usage
        if (cpuUsage > 80) {
            cpuUsageLabel->setStyleSheet("color: red;");
        } else if (cpuUsage > 60) {
            cpuUsageLabel->setStyleSheet("color: orange;");
        } else {
            cpuUsageLabel->setStyleSheet("color: green;");
        }
    } else {
        cpuUsageLabel->setText("🖥️: N/A");
        cpuUsageLabel->setToolTip("App CPU usage unavailable");
        cpuUsageLabel->setStyleSheet("");
    }
    update();
}

double StatusWidget::getCpuUsage()
{
#ifdef Q_OS_WIN
    // Windows implementation using process-specific CPU usage
    static FILETIME lastKernelTime = {0}, lastUserTime = {0};
    static ULARGE_INTEGER lastSystemTime = {0};
    static bool initialized = false;
    
    HANDLE process = GetCurrentProcess();
    FILETIME creationTime, exitTime, kernelTime, userTime;
    FILETIME systemTime;
    GetSystemTimeAsFileTime(&systemTime);
    
    if (!GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime)) {
        qWarning() << "Failed to get process times on Windows";
        return -1;
    }
    
    if (!initialized) {
        lastKernelTime = kernelTime;
        lastUserTime = userTime;
        lastSystemTime.LowPart = systemTime.dwLowDateTime;
        lastSystemTime.HighPart = systemTime.dwHighDateTime;
        initialized = true;
        return 0.0; // Return 0 for first measurement
    }
    
    // Convert FILETIME to ULARGE_INTEGER for easier calculation
    ULARGE_INTEGER currentKernel, currentUser, currentSystem;
    currentKernel.LowPart = kernelTime.dwLowDateTime;
    currentKernel.HighPart = kernelTime.dwHighDateTime;
    currentUser.LowPart = userTime.dwLowDateTime;
    currentUser.HighPart = userTime.dwHighDateTime;
    currentSystem.LowPart = systemTime.dwLowDateTime;
    currentSystem.HighPart = systemTime.dwHighDateTime;
    
    ULARGE_INTEGER prevKernel, prevUser;
    prevKernel.LowPart = lastKernelTime.dwLowDateTime;
    prevKernel.HighPart = lastKernelTime.dwHighDateTime;
    prevUser.LowPart = lastUserTime.dwLowDateTime;
    prevUser.HighPart = lastUserTime.dwHighDateTime;
    
    // Calculate deltas
    ULONGLONG kernelDelta = currentKernel.QuadPart - prevKernel.QuadPart;
    ULONGLONG userDelta = currentUser.QuadPart - prevUser.QuadPart;
    ULONGLONG systemDelta = currentSystem.QuadPart - lastSystemTime.QuadPart;
    
    // Update last values
    lastKernelTime = kernelTime;
    lastUserTime = userTime;
    lastSystemTime = currentSystem;
    
    if (systemDelta > 0) {
        double cpuUsage = (double)(kernelDelta + userDelta) / systemDelta * 100.0;
        return qMin(cpuUsage, 100.0); // Cap at 100%
    }
    
    return 0.0;
    
#elif defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    // Linux/Unix implementation using process-specific CPU usage
    static clock_t lastProcessTime = 0;
    static clock_t lastSystemTime = 0;
    static bool initialized = false;
    
    // Get current process CPU time
    struct tms processTime;
    clock_t currentSystemTime = times(&processTime);
    
    if (currentSystemTime == (clock_t)-1) {
        qWarning() << "Failed to get process times on Linux";
        return -1;
    }
    
    clock_t currentProcessTime = processTime.tms_utime + processTime.tms_stime;
    
    if (!initialized) {
        lastProcessTime = currentProcessTime;
        lastSystemTime = currentSystemTime;
        initialized = true;
        return 0.0; // Return 0 for first measurement
    }
    
    // Calculate deltas
    clock_t processDelta = currentProcessTime - lastProcessTime;
    clock_t systemDelta = currentSystemTime - lastSystemTime;
    
    // Update last values
    lastProcessTime = currentProcessTime;
    lastSystemTime = currentSystemTime;
    
    if (systemDelta > 0) {
        // Get number of CPU cores for proper scaling
        long numCpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (numCpus <= 0) numCpus = 1;
        
        double cpuUsage = (double)processDelta / systemDelta * 100.0;
        return qMin(cpuUsage, 100.0); // Cap at 100%
    }
    
    return 0.0;
    
#else
    // Unsupported platform - fallback using QProcess to get CPU usage
    static QProcess *process = nullptr;
    static bool initialized = false;
    
    if (!initialized) {
        process = new QProcess();
        initialized = true;
        return 0.0;
    }
    
    // This is a basic fallback that may not work on all platforms
    qWarning() << "CPU usage monitoring not fully supported on this platform";
    return -1;
#endif
}
