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
#include <QLoggingCategory>
#include <QDebug>
#include <QHBoxLayout>
#include <QProcess>
#include <QEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
#include <unistd.h>
#include <sys/times.h>
#endif

Q_LOGGING_CATEGORY(log_ui_statuswidget, "opf.ui.statuswidget")

StatusWidget::StatusWidget(QWidget *parent) : QWidget(parent), m_captureWidth(0), m_captureHeight(0), m_captureFramerate(0.0) {
    keyboardIndicatorsLabel = new QLabel("", this);
    keyStatesLabel = new QLabel("", this);
    statusLabel = new QLabel("", this);
    
    // Create labels with SVG icons
    cpuUsageLabel = new QLabel(this);
    cpuUsageLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", "0%"));
    
    fpsLabel = new QLabel(this);
    fpsLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", "0fps"));
    
    resolutionLabel = new QLabel(this);
    resolutionLabel->setPixmap(createIconTextLabel(":/images/laptop.svg", ""));
    inputResolutionLabel = new QLabel("INPUT(NA)", this);
    
    connectedPortLabel = new QLabel(this);
    connectedPortLabel->setPixmap(createIconTextLabel(":/images/usbplug.svg", "N/A"));
    
    // Store icons for reuse
    keyboardIcon = QPixmap(16, 16);
    keyboardIcon.fill(Qt::transparent);
    QPainter keyboardPainter(&keyboardIcon);
    QSvgRenderer keyboardRenderer(QString(":/images/keyboard.svg"));
    keyboardRenderer.render(&keyboardPainter, QRectF(0, 0, 16, 16));
    
    monitorIcon = QPixmap(16, 16);
    monitorIcon.fill(Qt::transparent);
    QPainter monitorPainter(&monitorIcon);
    QSvgRenderer monitorRenderer(QString(":/images/monitor.svg"));
    monitorRenderer.render(&monitorPainter, QRectF(0, 0, 16, 16));
    
    plugIcon = QPixmap(16, 16);
    plugIcon.fill(Qt::transparent);
    QPainter plugPainter(&plugIcon);
    QSvgRenderer plugRenderer(QString(":/images/usbplug.svg"));
    plugRenderer.render(&plugPainter, QRectF(0, 0, 16, 16));

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
    layout->addWidget(fpsLabel);
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

void StatusWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    
    // Detect palette/theme changes and refresh all icons
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshAllIcons();
    }
}

void StatusWidget::refreshAllIcons()
{
    // Refresh CPU usage label
    updateCpuUsage();
    
    // Refresh connected port label (preserve current state)
    QString currentTooltip = connectedPortLabel->toolTip();
    if (!currentTooltip.isEmpty() && currentTooltip != "N/A") {
        // Extract port and baudrate from tooltip or maintain current display
        connectedPortLabel->setPixmap(connectedPortLabel->pixmap(Qt::ReturnByValue));
    }
    
    // Refresh key states label (preserve current state)
    // This will be automatically updated on next setKeyStates call
    
    update();
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
        connectedPortLabel->setPixmap(createIconTextLabel(":/images/usbplug.svg", QString("%1@%2").arg(port).arg(baudrate)));
    }else{
        connectedPortLabel->setPixmap(createIconTextLabel(":/images/usbplug.svg", "N/A"));
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
    // Extract port name from current text (after the colon and space)
    QString currentText = connectedPortLabel->toolTip();
    QString portName = currentText.split('@').first();
    if (portName.isEmpty()) {
        portName = "Unknown";
    }
    connectedPortLabel->setPixmap(createIconTextLabel(":/images/usbplug.svg", QString("%1@%2").arg(portName).arg(baudrate)));
    update();
}

void StatusWidget::setKeyStates(bool numLock, bool capsLock, bool scrollLock)
{
    if (!keyStatesLabel) {
        qCCritical(log_ui_statuswidget) << "CRITICAL: StatusWidget::setKeyStates - keyStatesLabel is null!";
        return;
    }
    
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
        keyStatesText = "---";
    } else {
        keyStatesText = activeKeys.join("|");
    }
    
    QPixmap pixmap = createIconTextLabel(":/images/keyboard.svg", keyStatesText);
    
    if (pixmap.isNull()) {
        qCCritical(log_ui_statuswidget) << "CRITICAL: StatusWidget::setKeyStates - createIconTextLabel returned null pixmap!";
        return;
    }
    
    keyStatesLabel->setPixmap(pixmap);
    
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
        QString text = QString("%1%").arg(QString::number(cpuUsage, 'f', 1));
        QColor color;
        
        // Choose color based on usage
        if (cpuUsage > 80) {
            color = QColor("red");
        } else if (cpuUsage > 60) {
            color = QColor("orange");
        } else {
            color = QColor("green");
        }
        
        cpuUsageLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", text, color));
        cpuUsageLabel->setToolTip(QString("App CPU Usage: %1%").arg(QString::number(cpuUsage, 'f', 1)));
    } else {
        cpuUsageLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", "N/A"));
        cpuUsageLabel->setToolTip("App CPU usage unavailable");
    }
    update();
}

void StatusWidget::setFps(const double &fps, const QString &backend) {
    QString backendPrefix = backend.toUpper();
    if (fps >= 0) {
        QString text = QString("%1: %2fps").arg(backendPrefix).arg(QString::number(fps, 'f', 1));
        QColor color;
        
        // Choose color based on fps (green for good, yellow for ok, red for poor)
        if (fps >= 25) {
            color = QColor("green");
        } else if (fps >= 15) {
            color = QColor("orange");
        } else {
            color = QColor("red");
        }
        
        fpsLabel->setPixmap(createIconTextLabel("", text, color));
        fpsLabel->setToolTip(QString("Video FPS (%1): %2").arg(backend).arg(QString::number(fps, 'f', 1)));
    } else {
        fpsLabel->setPixmap(createIconTextLabel("", QString("%1: N/A").arg(backendPrefix)));
        fpsLabel->setToolTip(QString("Video FPS (%1) unavailable").arg(backend));
    }
    update();
}

QPixmap StatusWidget::createIconTextLabel(const QString &svgPath, const QString &text, const QColor &textColor, const QColor &iconColor)
{
    // Determine colors to use
    QColor finalIconColor = iconColor.isValid() ? iconColor : getIconColorForCurrentTheme();
    QColor finalTextColor = textColor.isValid() ? textColor : palette().color(QPalette::WindowText);
    
    // Load SVG icon if path is provided
    QPixmap iconPixmap(16, 16);
    bool hasIcon = !svgPath.isEmpty();
    if (hasIcon) {
        iconPixmap.fill(Qt::transparent);
        QPainter iconPainter(&iconPixmap);
        QSvgRenderer renderer(svgPath);
        renderer.render(&iconPainter, QRectF(0, 0, 16, 16));
        
        // Apply color overlay to make icon adapt to theme
        iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        iconPainter.fillRect(iconPixmap.rect(), finalIconColor);
        iconPainter.end();
    }
    
    // Calculate text width
    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(text);
    int totalWidth = hasIcon ? (16 + 4 + textWidth) : textWidth; // icon + spacing + text, or just text
    int height = 16;
    
    // Create combined pixmap
    QPixmap combinedPixmap(totalWidth, height);
    combinedPixmap.fill(Qt::transparent);
    QPainter painter(&combinedPixmap);
    
    // Draw icon if present
    if (hasIcon) {
        painter.drawPixmap(0, 0, iconPixmap);
        
        // Draw text
        painter.setPen(finalTextColor);
        painter.drawText(20, fm.ascent(), text);
    } else {
        // Draw text only (centered vertically)
        painter.setPen(finalTextColor);
        painter.drawText(0, fm.ascent(), text);
    }
    
    painter.end();
    
    return combinedPixmap;
}

QColor StatusWidget::getIconColorForCurrentTheme() const
{
    // Determine if we're in a dark or light theme by checking the window background
    QPalette pal = palette();
    QColor backgroundColor = pal.color(QPalette::Window);
    
    // Calculate luminance to determine if background is dark or light
    // Using the relative luminance formula: Y = 0.299*R + 0.587*G + 0.114*B
    int luminance = (299 * backgroundColor.red() + 587 * backgroundColor.green() + 114 * backgroundColor.blue()) / 1000;
    
    // If background is dark (luminance < 128), use light icons; otherwise use dark icons
    if (luminance < 128) {
        return QColor(220, 220, 220); // Light gray for dark theme
    } else {
        return QColor(50, 50, 50); // Dark gray for light theme
    }
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
        qCWarning(log_ui_statuswidget) << "Failed to get process times on Windows";
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
        qWarning(log_ui_statuswidget) << "Failed to get process times on Linux";
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
    qCWarning(log_ui_statuswidget) << "CPU usage monitoring not fully supported on this platform";
    return -1;
#endif
}
