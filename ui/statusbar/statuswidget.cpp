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
#include <QMessageBox>
#include <QApplication>
#include <QRegularExpression>
#include "serial/SerialPortManager.h"

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
    statusLabel = new QLabel("", this);
    
    // Create Lock state buttons instead of label
    numLockBtn = new QPushButton("NUM", this);
    capsLockBtn = new QPushButton("CAPS", this);
    scrollLockBtn = new QPushButton("SCROLL", this);
    
    // Configure button properties and styles
    QString buttonStyleSheet = R"(
        QPushButton {
            background-color: #f0f0f0;
            border: 1px solid #cccccc;
            border-radius: 3px;
            padding: 2px 4px;
            color: #333333;
            font-weight: bold;
            font-size: 10px;ound-color: #e0e0e0;
            border: 1px solid #999999;
        }
        QPushButton:pressed {
            background-color: #d0d0d0;
        }
        QPushButton:checked {
            background-color: #4CAF50;
            color: white;
            border: 1px solid #45a049;
        }
        QPushButton:checked:hover {
            background-color: #45a049;
        }
    )";
    
    for (auto btn : {numLockBtn, capsLockBtn, scrollLockBtn}) {
        btn->setCheckable(true);
        btn->setChecked(false);
        btn->setMaximumWidth(65);
        btn->setMaximumHeight(24);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFlat(false);
        btn->setStyleSheet(buttonStyleSheet);
    }
    
    // Create container and layout for Lock buttons
    keyStatesContainer = new QWidget(this);
    QHBoxLayout *keyStatesLayout = new QHBoxLayout(keyStatesContainer);
    keyStatesLayout->setContentsMargins(0, 0, 0, 0);
    keyStatesLayout->setSpacing(2);
    keyStatesLayout->addWidget(numLockBtn);
    keyStatesLayout->addWidget(capsLockBtn);
    keyStatesLayout->addWidget(scrollLockBtn);
    keyStatesContainer->setLayout(keyStatesLayout);
    
    // Connect button click signals
    connect(numLockBtn, &QPushButton::clicked, this, &StatusWidget::onNumLockClicked);
    connect(capsLockBtn, &QPushButton::clicked, this, &StatusWidget::onCapsLockClicked);
    connect(scrollLockBtn, &QPushButton::clicked, this, &StatusWidget::onScrollLockClicked);
    
    // Create labels with SVG icons
    cpuUsageLabel = new QLabel(this);
    cpuUsageLabel->setPixmap(createIconTextLabel(":/images/cpu.svg", "0%"));
    
    fpsLabel = new QLabel(this);
    fpsLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", "0fps"));
    
    resolutionLabel = new QLabel(this);
    resolutionLabel->setPixmap(createIconTextLabel(":/images/laptop.svg", ""));
    inputResolutionLabel = new QLabel("INPUT(NA)", this);
    
    connectedPortLabel = new QLabel(this);
    connectedPortLabel->setPixmap(createIconTextLabel(":/images/usbplug.svg", "N/A"));
    
    recordingTimeLabel = new QLabel(this);
    recordingTimeLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", "00:00:00", QColor("red")));
    recordingTimeLabel->setVisible(false);
    
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

    #ifdef Q_OS_WIN
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        m_cpuCoreCount = static_cast<int>(sysInfo.dwNumberOfProcessors);
    #elif defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
        long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
        m_cpuCoreCount = (nprocs > 0) ? static_cast<int>(nprocs) : 1;
    #else
        int ideal = QThread::idealThreadCount();
        m_cpuCoreCount = (ideal > 0) ? ideal : 1;
    #endif

    qCDebug(log_ui_statuswidget) << "Detected CPU core count:" << m_cpuCoreCount;


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
    layout->addWidget(keyStatesContainer);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(connectedPortLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(recordingTimeLabel);
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
    // Check if the new state is the same as the current state to avoid unnecessary updates
    if (m_lastPort == port && m_lastBaudrate == baudrate) {
        // States are the same, skip the update
        qDebug() << "[StatusWidget] Skipping duplicate state update - port:" << port << "baudrate:" << baudrate;
        return;
    }

    // Update the stored state
    m_lastPort = port;
    m_lastBaudrate = baudrate;
    
    QString displayText;
    
    // Debug logging to understand port and baudrate values
    qDebug() << "[StatusWidget] setConnectedPort called with port:" << port << "baudrate:" << baudrate;
    
    // Handle different cases for port and baudrate
    if (port == "NA" || port.isEmpty()) {
        // No valid port or specifically marked as "NA" - show Not Connected
        displayText = "Not Connected";
        qDebug() << "[StatusWidget] Showing 'Not Connected' status";
    } else if (baudrate > 0) {
        // Valid baudrate - show port and baudrate
        displayText = QString("%1@%2").arg(port).arg(baudrate);
        qDebug() << "[StatusWidget] Showing port and baudrate:" << displayText;
    } else if (baudrate == 0 && !port.isEmpty()) {
        // Baudrate is 0 but port is valid - show just the port name
        displayText = port;
        qDebug() << "[StatusWidget] Showing port name only:" << displayText;
    } else {
        // Other cases - show the port name
        displayText = port;
        qDebug() << "[StatusWidget] Showing port name (fallback):" << displayText;
    }
    
    connectedPortLabel->setPixmap(createIconTextLabel(":/images/usbplug.svg", displayText));
    update(); 
}

void StatusWidget::setStatusUpdate(const QString &status){
    statusLabel->setText(status);
    update();
}

// Set keybord and mouse USB connection status
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
    if (!numLockBtn || !capsLockBtn || !scrollLockBtn) {
        qCCritical(log_ui_statuswidget) << "CRITICAL: StatusWidget::setKeyStates - Lock buttons are null!";
        return;
    }
    
    // Update button states
    numLockBtn->setChecked(numLock);
    capsLockBtn->setChecked(capsLock);
    scrollLockBtn->setChecked(scrollLock);
    
    // Set tooltip with detailed information
    QString tooltip = QString("Keyboard Lock States:\nNum Lock: %1\nCaps Lock: %2\nScroll Lock: %3")
                     .arg(numLock ? "ON" : "OFF")
                     .arg(capsLock ? "ON" : "OFF")
                     .arg(scrollLock ? "ON" : "OFF");
    
    keyStatesContainer->setToolTip(tooltip);
    numLockBtn->setToolTip(QString("NumLock: %1").arg(numLock ? "ON" : "OFF"));
    capsLockBtn->setToolTip(QString("CapsLock: %1").arg(capsLock ? "ON" : "OFF"));
    scrollLockBtn->setToolTip(QString("ScrollLock: %1").arg(scrollLock ? "ON" : "OFF"));
    
    update();
}

void StatusWidget::setRecordingTime(const QString &time)
{
    if (!recordingTimeLabel) {
        return;
    }
    recordingTimeLabel->setPixmap(createIconTextLabel(":/images/monitor.svg", "REC " + time, QColor("red")));
    recordingTimeLabel->setToolTip("Recording: " + time);
    update();
}

void StatusWidget::showRecordingTime(bool show)
{
    if (!recordingTimeLabel) {
        return;
    }
    recordingTimeLabel->setVisible(show);
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
        
        cpuUsageLabel->setPixmap(createIconTextLabel(":/images/cpu.svg", text, color));
        cpuUsageLabel->setToolTip(QString("App CPU Usage: %1%").arg(QString::number(cpuUsage, 'f', 1)));
    } else {
        cpuUsageLabel->setPixmap(createIconTextLabel(":/images/cpu.svg", "N/A"));
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
    static FILETIME s_lastKernelTime = {0}, s_lastUserTime = {0};
    static qint64 s_lastWallTimeNs = 0;
    static bool s_initialized = false;

    HANDLE hProcess = GetCurrentProcess();
    FILETIME creationTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime, &userTime)) {
        qCWarning(log_ui_statuswidget) << "Failed to get process times on Windows";
        return -1;
    }

    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
        qCWarning(log_ui_statuswidget) << "Failed to get high-resolution timer on Windows";
        return -1;
    }
    qint64 currentWallTimeNs = (counter.QuadPart * 1000000000LL) / freq.QuadPart;

    if (!s_initialized) {
        s_lastKernelTime = kernelTime;
        s_lastUserTime = userTime;
        s_lastWallTimeNs = currentWallTimeNs;
        s_initialized = true;
        return 0.0;
    }

    auto to100ns = [](const FILETIME& ft) -> quint64 {
        return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };

    quint64 currentKernel100ns = to100ns(kernelTime);
    quint64 currentUser100ns   = to100ns(userTime);
    quint64 lastKernel100ns    = to100ns(s_lastKernelTime);
    quint64 lastUser100ns      = to100ns(s_lastUserTime);

    quint64 processDelta100ns = (currentKernel100ns - lastKernel100ns) + (currentUser100ns - lastUser100ns);
    qint64 wallDeltaNs = currentWallTimeNs - s_lastWallTimeNs;

    s_lastKernelTime = kernelTime;
    s_lastUserTime = userTime;
    s_lastWallTimeNs = currentWallTimeNs;

    if (wallDeltaNs <= 0) {
        return 0.0;
    }

    quint64 processDeltaNs = processDelta100ns * 100ULL; // convert 100-ns to ns
    double cpuUsagePercent = (static_cast<double>(processDeltaNs) / static_cast<double>(wallDeltaNs)) * 100.0;

    // Normalize to 0~100% of total system CPU capacity
    double normalizedCpuUsage = cpuUsagePercent / m_cpuCoreCount;
    return qMax(0.0, qMin(normalizedCpuUsage, 100.0));

#elif defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    static struct timespec s_lastProcessTime = {0, 0};
    static struct timespec s_lastWallTime = {0, 0};
    static bool s_initialized = false;

    struct timespec currentProcessTime, currentWallTime;

    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &currentProcessTime) != 0) {
        qCWarning(log_ui_statuswidget) << "Failed to get process CPU time on Linux/Unix";
        return -1;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &currentWallTime) != 0) {
        qCWarning(log_ui_statuswidget) << "Failed to get monotonic time on Linux/Unix";
        return -1;
    }

    if (!s_initialized) {
        s_lastProcessTime = currentProcessTime;
        s_lastWallTime = currentWallTime;
        s_initialized = true;
        return 0.0;
    }

    auto toNanoseconds = [](const struct timespec& ts) -> quint64 {
        return static_cast<quint64>(ts.tv_sec) * 1000000000ULL + static_cast<quint64>(ts.tv_nsec);
    };

    quint64 currentProcessNs = toNanoseconds(currentProcessTime);
    quint64 lastProcessNs = toNanoseconds(s_lastProcessTime);
    quint64 currentWallNs = toNanoseconds(currentWallTime);
    quint64 lastWallNs = toNanoseconds(s_lastWallTime);

    quint64 processDeltaNs = currentProcessNs - lastProcessNs;
    quint64 wallDeltaNs = currentWallNs - lastWallNs;

    s_lastProcessTime = currentProcessTime;
    s_lastWallTime = currentWallTime;

    if (wallDeltaNs == 0) {
        return 0.0;
    }

    double cpuUsagePercent = (static_cast<double>(processDeltaNs) / static_cast<double>(wallDeltaNs)) * 100.0;
    double normalizedCpuUsage = cpuUsagePercent / m_cpuCoreCount;
    return qMax(0.0, qMin(normalizedCpuUsage, 100.0));

#else
    qCWarning(log_ui_statuswidget) << "CPU usage monitoring not supported on this platform";
    return -1;
#endif
}

/**
 * @brief Slot called when NumLock button is clicked
 * Sends NumLock toggle command to device
 */
void StatusWidget::onNumLockClicked()
{
    bool isChecked = numLockBtn->isChecked();
    
    // Send toggle command to device via SerialPortManager
    bool success = SerialPortManager::getInstance().toggleNumLock();
    
    if (success) {
        qCDebug(log_ui_statuswidget) 
            << "✓ NumLock button toggled. Expected state:" 
            << (isChecked ? "ON" : "OFF");
        numLockBtn->setToolTip(QString("NumLock: %1\n(Click to toggle)")
                                .arg(isChecked ? "ON" : "OFF"));
    } else {
        qCWarning(log_ui_statuswidget) << "✗ Failed to send NumLock command";
        // Revert button state if command failed
        numLockBtn->setChecked(!isChecked);
    }
}

/**
 * @brief Slot called when CapsLock button is clicked
 * Sends CapsLock toggle command to device
 */
void StatusWidget::onCapsLockClicked()
{
    bool isChecked = capsLockBtn->isChecked();
    
    // Send toggle command to device via SerialPortManager
    bool success = SerialPortManager::getInstance().toggleCapsLock();
    
    if (success) {
        qCDebug(log_ui_statuswidget) 
            << "✓ CapsLock button toggled. Expected state:" 
            << (isChecked ? "ON" : "OFF");
        capsLockBtn->setToolTip(QString("CapsLock: %1\n(Click to toggle)")
                                 .arg(isChecked ? "ON" : "OFF"));
    } else {
        qCWarning(log_ui_statuswidget) << "✗ Failed to send CapsLock command";
        // Revert button state if command failed
        capsLockBtn->setChecked(!isChecked);
    }
}

/**
 * @brief Slot called when ScrollLock button is clicked
 * Sends ScrollLock toggle command to device
 */
void StatusWidget::onScrollLockClicked()
{
    bool isChecked = scrollLockBtn->isChecked();
    
    // Send toggle command to device via SerialPortManager
    bool success = SerialPortManager::getInstance().toggleScrollLock();
    
    if (success) {
        qCDebug(log_ui_statuswidget) 
            << "✓ ScrollLock button toggled. Expected state:" 
            << (isChecked ? "ON" : "OFF");
        scrollLockBtn->setToolTip(QString("ScrollLock: %1\n(Click to toggle)")
                                   .arg(isChecked ? "ON" : "OFF"));
    } else {
        qCWarning(log_ui_statuswidget) << "✗ Failed to send ScrollLock command";
        // Revert button state if command failed
        scrollLockBtn->setChecked(!isChecked);
    }
}

void StatusWidget::checkAndWarnResolutionMismatch(const int &preferredWidth, const int &preferredHeight, const float &preferredFps)
{
    // Get the currently displayed input resolution information
    QString inputResText = inputResolutionLabel->text();
    if (inputResText == "INPUT(NA)" || inputResText.isEmpty()) {
        // If there's no input resolution information, return directly
        return;
    }

    // Parse the input resolution information
    // Format: INPUT(1920X1080@60.0)
    QRegularExpression rx("INPUT\\((\\d+)X(\\d+)@(\\d+\\.?\\d*)\\)");
    QRegularExpressionMatch match = rx.match(inputResText);
    if (match.hasMatch()) {
        int inputWidth = match.captured(1).toInt();
        int inputHeight = match.captured(2).toInt();
        float inputFps = match.captured(3).toFloat();

        // Compare if resolution and frame rate match
        bool resolutionMatch = (inputWidth == preferredWidth && inputHeight == preferredHeight);
        bool fpsMatch = (qAbs(inputFps - preferredFps) < 0.5f); // Allow 0.5 margin of error

        if (!resolutionMatch || !fpsMatch) {
            QString warningMessage = QString(tr("Resolution/FPS mismatch detected!\n\n")) +
                                    QString(tr("Preferred setting: %1x%2@%3fps\n")).arg(preferredWidth).arg(preferredHeight).arg(preferredFps) +
                                    QString(tr("Actual input: %1x%2@%3fps\n\n")).arg(inputWidth).arg(inputHeight).arg(inputFps) +
                                    tr("Where to adjust:\n") +
                                    tr("1. Go to Preferences -> Video Settings to change capture resolution/framerate\n") +
                                    tr("2. Adjust your source device's output resolution/framerate\n\n") +
                                    tr("Potential consequences of mismatch:\n") +
                                    tr("- Reduced performance and frame drops\n") +
                                    tr("- Display scaling artifacts\n") +
                                    tr("- Possible audio/video sync issues\n") +
                                    tr("- Suboptimal user experience");

            QMessageBox::warning(this, tr("Resolution/FPS Mismatch Warning"), warningMessage);
        }
    }
}
