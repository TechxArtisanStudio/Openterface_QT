#include "statusbarmanager.h"
#include <QPainter>
#include <QSvgRenderer>
#include <QTimer>

StatusBarManager::StatusBarManager(QStatusBar *statusBar, QObject *parent)
    : QObject(parent), m_statusBar(statusBar), m_messageTimer(new QTimer(this)), m_messageThrottleActive(false)
{
    iconColor = QPalette().color(QPalette::WindowText);
    m_statusWidget = new StatusWidget(m_statusBar);
    m_statusBar->addPermanentWidget(m_statusWidget);
    
    // Setup message timer for throttling
    m_messageTimer->setSingleShot(true);
    connect(m_messageTimer, &QTimer::timeout, this, [this]() {
        m_messageThrottleActive = false;
        if (!m_pendingMessage.isEmpty()) {
            // Show the pending message if there's one waiting
            resetLabel->setText(m_pendingMessage);
            m_lastMessage = m_pendingMessage;
            m_pendingMessage.clear();
        }
    });
    
    initStatusBar();
}

void StatusBarManager::initStatusBar()
{
    mouseLabel = new QLabel(m_statusBar);
    mouseLocationLabel = new QLabel(QString("(0,0)"), m_statusBar);
    mouseLocationLabel->setFixedWidth(80);

    QWidget *mouseContainer = new QWidget(m_statusBar);
    QHBoxLayout *mouseLayout = new QHBoxLayout(mouseContainer);

    mouseLayout->setContentsMargins(0, 0, 0, 0);
    mouseLayout->addWidget(mouseLabel);
    mouseLayout->addWidget(mouseLocationLabel);
    m_statusBar->addWidget(mouseContainer);

    keyPressedLabel = new QLabel(m_statusBar);
    keyLabel = new QLabel(m_statusBar);
    keyLabel->setFixedWidth(120);

    QWidget *keyContainer = new QWidget(m_statusBar);
    QHBoxLayout *keyLayout = new QHBoxLayout(keyContainer);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->addWidget(keyPressedLabel);
    keyLayout->addWidget(keyLabel);
    m_statusBar->addWidget(keyContainer);

    QWidget *resetContainer = new QWidget(m_statusBar);
    QHBoxLayout *resetLayout = new QHBoxLayout(resetContainer);
    resetLabel = new QLabel(m_statusBar);

    resetLayout->addWidget(resetLabel);
    m_statusBar->addWidget(resetContainer);

    onLastKeyPressed("");
    updateIconColor();
}

void StatusBarManager::factoryReset(bool isStarted)
{
    if (isStarted) {
        resetLabel->setText("Factory Reset Started, it may take a few seconds...");
        resetLabel->setStyleSheet("color: red;");
    }else {
        resetLabel->clear();
        resetLabel->setText("Factory Reset Ended");
        resetLabel->setStyleSheet("color: green;");
    }
}

void StatusBarManager::serialPortReset(bool isStarted)
{
    if (isStarted) {
        resetLabel->clear();
        resetLabel->setText("Serial Port Reset Started");
        resetLabel->setStyleSheet("color: red;");
    }else {
        resetLabel->clear();
        resetLabel->setText("Serial Port Reset Ended");
        resetLabel->setStyleSheet("color: green;");
        QTimer::singleShot(1000, resetLabel, &QLabel::clear);
    }
}

void StatusBarManager::showThrottledMessage(const QString& message, const QString& style, int duration)
{
    if (m_messageThrottleActive && m_lastMessage == message) {
        // Same message is already being displayed, ignore
        return;
    }
    
    if (m_messageThrottleActive) {
        // Store as pending message to show later
        m_pendingMessage = message;
        return;
    }
    
    // Show the message immediately
    resetLabel->clear();
    resetLabel->setText(message);
    resetLabel->setStyleSheet(style);
    m_lastMessage = message;
    m_messageThrottleActive = true;
    
    // Clear after duration and allow new messages
    QTimer::singleShot(duration, this, [this]() {
        resetLabel->clear();
        m_messageThrottleActive = false;
        m_lastMessage.clear();
    });
}

void StatusBarManager::showNewDevicePluggedIn(const QString& portChain)
{
    qDebug() << "StatusBarManager::showNewDevicePluggedIn called with portChain:" << portChain;
    if (!portChain.isEmpty()) {
        QString message = QString("🔌 New device detected: Port %1").arg(portChain);
        showThrottledMessage(message, "color: blue;", 3000);
    }
}

void StatusBarManager::showDeviceUnplugged(const QString& portChain)
{
    qDebug() << "StatusBarManager::showDeviceUnplugged called with portChain:" << portChain;
    if (!portChain.isEmpty()) {
        QString message = QString("🔌 Device unplugged: Port %1").arg(portChain);
        showThrottledMessage(message, "color: orange;", 3000);
    }
}

void StatusBarManager::onLastKeyPressed(const QString& key)
{
    updateKeyboardIcon(key);
    keyLabel->setText(key);
}

void StatusBarManager::onLastMouseLocation(const QPoint& location, const QString& mouseEvent)
{
    QString svgPath;
    if (mouseEvent == "L") {
        svgPath = ":/images/mouse-left-button.svg";
    } else if (mouseEvent == "R") {
        svgPath = ":/images/mouse-right-button.svg";
    } else if (mouseEvent == "M") {
        svgPath = ":/images/mouse-middle-button.svg";
    } else {
        svgPath = ":/images/mouse-default.svg";
    }

    QPixmap pixmap = recolorSvg(svgPath, iconColor, QSize(12, 12));
    mouseLabel->setPixmap(pixmap);

    int capture_width = m_statusWidget->getCaptureWidth() > 5000 ? 0 : m_statusWidget->getCaptureWidth();
    int capture_height = m_statusWidget->getCaptureHeight() > 5000 ? 0 : m_statusWidget->getCaptureHeight();
    
    int mouse_x = static_cast<int>(location.x() / 4096.0 * capture_width);
    int mouse_y = static_cast<int>(location.y() / 4096.0 * capture_height);

    mouseLocationLabel->setText(QString("(%1,%2)").arg(mouse_x).arg(mouse_y));
}

void StatusBarManager::setConnectedPort(const QString& port, const int& baudrate)
{
    m_currentPort = port;
    m_statusWidget->setConnectedPort(port, baudrate);
}

void StatusBarManager::setStatusUpdate(const QString& status)
{
    m_statusWidget->setStatusUpdate(status);
}

void StatusBarManager::setInputResolution(int width, int height, float fps, float pixelClk)
{
    m_statusWidget->setInputResolution(width, height, fps, pixelClk);
}

void StatusBarManager::setCaptureResolution(int width, int height, int fps)
{
    m_statusWidget->setCaptureResolution(width, height, fps);
}

QPixmap StatusBarManager::recolorSvg(const QString &svgPath, const QColor &color, const QSize &size)
{
    QSvgRenderer svgRenderer(svgPath);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    svgRenderer.render(&painter);

    QPixmap colorOverlay(size);
    colorOverlay.fill(color);

    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.drawPixmap(0, 0, colorOverlay);

    return pixmap;
}

QColor StatusBarManager::getContrastingColor(const QColor &color)
{
    double luminance = (0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue()) / 255;
    return luminance > 0.5 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

void StatusBarManager::setTargetUsbConnected(bool isConnected)
{
    m_statusWidget->setTargetUsbConnected(isConnected);
}

void StatusBarManager::updateIconColor()
{
    iconColor = getContrastingColor(m_statusBar->palette().color(QPalette::Window));
    updateKeyboardIcon(keyLabel->text());
    onLastMouseLocation(QPoint(0, 0), "");
}

void StatusBarManager::showCameraSwitching(const QString& fromDevice, const QString& toDevice)
{
    QString message = QString("📹 Switching camera: %1 → %2").arg(fromDevice.isEmpty() ? "None" : fromDevice, toDevice);
    showThrottledMessage(message, "color: purple;", 2000);
}

void StatusBarManager::showCameraSwitchComplete(const QString& device)
{
    QString message = QString("✅ Camera ready: %1").arg(device);
    showThrottledMessage(message, "color: green;", 2000);
}

void StatusBarManager::updateKeyboardIcon(const QString& key)
{
    QString svgPath = key.isEmpty() ? ":/images/keyboard.svg" : ":/images/keyboard-pressed.svg";
    QPixmap pixmap = recolorSvg(svgPath, iconColor, QSize(18, 18));
    keyPressedLabel->setPixmap(pixmap);
}
