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

#include "mainwindow.h"
#include "global.h"
#include "settingdialog.h"
#include "ui_mainwindow.h"
#include "globalsetting.h"

#include "host/HostManager.h"
#include "serial/SerialPortManager.h"
#include "loghandler.h"
#include "ui/imagesettings.h"
#include "ui/settingdialog.h"
#include "ui/helppane.h"
#include "ui/serialportdebugdialog.h"

#include "ui/videopane.h"
#include "video/videohid.h"

#include <QCameraDevice>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QMediaRecorder>
#include <QVideoWidget>
#include <QStackedLayout>
#include <QMessageBox>
#include <QImageCapture>
#include <QToolBar>
#include <QClipboard>
#include <QInputMethod>
#include <QAction>
#include <QActionGroup>
#include <QImage>
#include <QKeyEvent>
#include <QPalette>
#include <QSystemTrayIcon>
#include <QDir>
#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPainter>
#include <QMessageBox>
#include <QDesktopServices>
#include <QSysInfo>
#include <QMenuBar>
#include <QPushButton>
#include <QComboBox>
#include <QScrollBar>
#include <QGuiApplication>

Q_LOGGING_CATEGORY(log_ui_mainwindow, "opf.ui.mainwindow")

/*
  * QT Permissions API is not compatible with Qt < 6.5 and will cause compilation failure on
  * expanding the QT_CONFIG macro if it isn't set as a feature in qtcore-config.h. QT < 6.5
  * is still true for a large number of linux distros in 2024. This ifdef or another
  * workaround needs to be used anywhere the QPermissions class is called, for distros to
  * be able to use their package manager's native Qt libs, if they are < 6.5.
  *
  * See qtconfigmacros.h, qtcore-config.h, etc. in the relevant Qt includes directory, and:
  * https://doc-snapshots.qt.io/qt6-6.5/whatsnew65.html
  * https://doc-snapshots.qt.io/qt6-6.5/permissions.html
*/

#ifdef QT_FEATURE_permissions
#if QT_CONFIG(permissions)
#include <QPermission>
#endif
#endif

QPixmap recolorSvg(const QString &svgPath, const QColor &color, const QSize &size) {
    QSvgRenderer svgRenderer(svgPath);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    svgRenderer.render(&painter);

    // Create a color overlay
    QPixmap colorOverlay(size);
    colorOverlay.fill(color);

    // Set the composition mode to SourceIn to apply the color overlay
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.drawPixmap(0, 0, colorOverlay);

    return pixmap;
}

QColor getContrastingColor(const QColor &color) {
    int d = 0;
    // Calculate the perceptive luminance (Y) - human eye favors green color
    double luminance = (0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue()) / 255;
    if (luminance > 0.5) {
        d = 0; // bright colors - black font
    } else {
        d = 255; // dark colors - white font
    }
    return QColor(d, d, d);
}

MainWindow::MainWindow() : ui(new Ui::MainWindow), m_audioManager(new AudioManager(this)),
                                        videoPane(new VideoPane(this)),
                                        scrollArea(new QScrollArea(this)),
                                        stackedLayout(new QStackedLayout(this)),
                                        toolbarManager(new ToolbarManager(this)),
                                        statusWidget(new StatusWidget(this)),
                                        toggleSwitch(new ToggleSwitch(this)),
                                        m_cameraManager(new CameraManager(this)),
                                        m_inputHandler(new InputHandler(this))
{
    qCDebug(log_ui_mainwindow) << "Init camera...";
    ui->setupUi(this);
    ui->statusbar->addPermanentWidget(statusWidget);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setLayout(stackedLayout);

    HelpPane *helpPane = new HelpPane;
    stackedLayout->addWidget(helpPane);

    // Set size policy and minimum size for videoPane
    // videoPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoPane->setMinimumSize(this->width(),
    this->height() - ui->statusbar->height() - ui->menubar->height()); // must minus the statusbar and menubar height

    scrollArea->setWidget(videoPane);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setBackgroundRole(QPalette::Dark);
    stackedLayout->addWidget(scrollArea);

    stackedLayout->setCurrentIndex(0);

    centralWidget->setMouseTracking(true);

    ui->menubar->setCornerWidget(ui->cornerWidget, Qt::TopRightCorner);

    setCentralWidget(centralWidget);
    qCDebug(log_ui_mainwindow) << "Set host manager event callback...";
    HostManager::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe Video HID connected...";
    VideoHid::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe video input changed...";
    connect(&m_source, &QMediaDevices::videoInputsChanged, this, &MainWindow::updateCameras);

    //connect(videoDevicesGroup, &QActionGroup::triggered, this, &MainWindow::updateCameraDevice);

    qCDebug(log_ui_mainwindow) << "Observe Relative/Absolute toggle...";
    connect(ui->actionRelative, &QAction::triggered, this, &MainWindow::onActionRelativeTriggered);
    connect(ui->actionAbsolute, &QAction::triggered, this, &MainWindow::onActionAbsoluteTriggered);

    qCDebug(log_ui_mainwindow) << "Observe reset HID triggerd...";
    connect(ui->actionResetHID, &QAction::triggered, this, &MainWindow::onActionResetHIDTriggered);

    qCDebug(log_ui_mainwindow) << "Observe factory reset HID triggerd...";
    connect(ui->actionFactory_reset_HID, &QAction::triggered, this, &MainWindow::onActionFactoryResetHIDTriggered);

    qCDebug(log_ui_mainwindow) << "Observe reset Serial Port triggerd...";
    connect(ui->actionResetSerialPort, &QAction::triggered, this, &MainWindow::onActionResetSerialPortTriggered);

    qDebug() << "Observe Hardware change MainWindow triggerd...";

    qCDebug(log_ui_mainwindow) << "Creating and setting up ToggleSwitch...";
    toggleSwitch->setFixedSize(78, 28);  // Adjust size as needed
    connect(toggleSwitch, &ToggleSwitch::stateChanged, this, &MainWindow::onToggleSwitchStateChanged);

    // Add the ToggleSwitch as the last button in the cornerWidget's layout
    QHBoxLayout *cornerLayout = qobject_cast<QHBoxLayout*>(ui->cornerWidget->layout());
    if (cornerLayout) {
        cornerLayout->addWidget(toggleSwitch);
    } else {
        qCWarning(log_ui_mainwindow) << "Corner widget layout is not a QHBoxLayout. Unable to add ToggleSwitch.";
    }

    // load the settings
    qDebug() << "Loading settings";
    GlobalSetting::instance().loadLogSettings();
    GlobalSetting::instance().loadVideoSettings();
    LogHandler::instance().enableLogStore();

    qCDebug(log_ui_mainwindow) << "Observe switch usb connection trigger...";
    connect(ui->actionTo_Host, &QAction::triggered, this, &MainWindow::onActionSwitchToHostTriggered);
    connect(ui->actionTo_Target, &QAction::triggered, this, &MainWindow::onActionSwitchToTargetTriggered);

    qCDebug(log_ui_mainwindow) << "Observe action paste from host...";
    connect(ui->actionPaste, &QAction::triggered, this, &MainWindow::onActionPasteToTarget);
    connect(ui->pasteButton, &QPushButton::released, this, &MainWindow::onActionPasteToTarget);

    connect(ui->screensaverButton, &QPushButton::released, this, &MainWindow::onActionScreensaver);

    connect(ui->virtualKeyboardButton, &QPushButton::released, this, &MainWindow::onToggleVirtualKeyboard);

    qDebug() << "Init...";
    init();

    qDebug() << "Init status bar...";
    initStatusBar();

    addToolBar(Qt::TopToolBarArea, toolbarManager->getToolbar());
    toolbarManager->getToolbar()->setVisible(false);

    // In the MainWindow constructor or initialization method
    connect(qApp, &QGuiApplication::paletteChanged, toolbarManager, &ToolbarManager::updateStyles);

    connect(m_cameraManager, &CameraManager::cameraActiveChanged, this, &MainWindow::updateCameraActive);
    connect(m_cameraManager, &CameraManager::cameraError, this, &MainWindow::displayCameraError);
    connect(m_cameraManager, &CameraManager::imageCaptured, this, &MainWindow::processCapturedImage);                                         

    // Connect palette change signal to the slot
    iconColor = palette().color(QPalette::WindowText);
    onLastKeyPressed("");
    onLastMouseLocation(QPoint(0, 0), "");

    // Connect zoom buttons
    connect(ui->ZoomInButton, &QPushButton::clicked, this, &MainWindow::onZoomIn);
    connect(ui->ZoomOutButton, &QPushButton::clicked, this, &MainWindow::onZoomOut);
    connect(ui->ZoomReductionButton, &QPushButton::clicked, this, &MainWindow::onZoomReduction);
    scrollArea->ensureWidgetVisible(videoPane);

    // Initialize the timer
    scrollTimer = new QTimer(this);
    connect(scrollTimer, &QTimer::timeout, this, &MainWindow::updateScrollbars);
    scrollTimer->start(100); // Check every 50 ms
}

void MainWindow::onZoomIn()
{
    factorScale = 1.1 * factorScale;
    QSize currentSize = videoPane->size() * 1.1;
    videoPane->resize(currentSize.width(), currentSize.height());
    qDebug() << "video pane size:" << videoPane->geometry();
    if (videoPane->width() > scrollArea->width() || videoPane->height() > scrollArea->height()) {
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void MainWindow::onZoomOut()
{
    if (videoPane->width() != this->width()){
        factorScale = 0.9 * factorScale;
        QSize currentSize = videoPane->size() * 0.9;
        videoPane->resize(currentSize.width(), currentSize.height());
        if (videoPane->width() <= scrollArea->width() && videoPane->height() <= scrollArea->height()) {
            scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
    }

}

void MainWindow::onZoomReduction()
{
    videoPane->resize(this->width() * 0.9, (this->height() - ui->statusbar->height() - ui->menubar->height()) * 0.9);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void MainWindow::init()
{
    qCDebug(log_ui_mainwindow) << "MainWindow init...";
#ifdef QT_FEATURE_permissions //Permissions API not compatible with Qt < 6.5 and will cause compilation failure on expanding macro in qtconfigmacros.h
#if QT_CONFIG(permissions)
    // camera 
    QCameraPermission cameraPermission;
    switch (qApp->checkPermission(cameraPermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(cameraPermission, this, &MainWindow::init);
        return;
    case Qt::PermissionStatus::Denied:
        qWarning("MainWindow permission is not granted!");
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
    // microphone
    QMicrophonePermission microphonePermission;
    switch (qApp->checkPermission(microphonePermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(microphonePermission, this, &MainWindow::init);
        return;
    case Qt::PermissionStatus::Denied:
        qWarning("Microphone permission is not granted!");
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
#endif
    // Camera devices:
    updateCameras();

    loadCameraSettingAndSetCamera();

    GlobalVar::instance().setWinWidth(this->width());
    GlobalVar::instance().setWinHeight(this->height());

    // Initialize the virtual keyboard button icon
    QIcon icon(":/images/keyboard-down.svg");
    ui->virtualKeyboardButton->setIcon(icon);

    // Add this after other menu connections
    connect(ui->menuBaudrate, &QMenu::triggered, this, &MainWindow::onBaudrateMenuTriggered);
    connect(&SerialPortManager::getInstance(), &SerialPortManager::connectedPortChanged, this, &MainWindow::onPortConnected);
}

void MainWindow::initStatusBar()
{
    qCDebug(log_ui_mainwindow) << "Init status bar...";

    // Create a QLabel to hold the SVG icon
    mouseLabel = new QLabel(this);
    mouseLocationLabel = new QLabel(QString("(0,0)"), this);
    mouseLocationLabel->setFixedWidth(80);

    // Mouse container widget
    QWidget *mouseContainer = new QWidget(this);
    QHBoxLayout *mouseLayout = new QHBoxLayout(mouseContainer);

    mouseLayout->setContentsMargins(0, 0, 0, 0); // Remove margins
    mouseLayout->addWidget(mouseLabel);
    mouseLayout->addWidget(mouseLocationLabel);
    ui->statusbar->addWidget(mouseContainer);

    onLastMouseLocation(QPoint(0, 0), nullptr);
    keyPressedLabel = new QLabel(this);
    keyLabel = new QLabel(this);
    keyLabel->setFixedWidth(25);
    // Key container widget
    QWidget *keyContainer = new QWidget(this);
    QHBoxLayout *keyLayout = new QHBoxLayout(keyContainer);
    keyLayout->setContentsMargins(0, 0, 0, 0); // Remove margins
    keyLayout->addWidget(keyPressedLabel);
    keyLayout->addWidget(keyLabel);
    ui->statusbar->addWidget(keyContainer);

    onLastKeyPressed("");
}

void MainWindow::loadCameraSettingAndSetCamera(){
    QSettings settings("Techxartisan", "Openterface");
    QString deviceDescription = settings.value("camera/device", "Openterface").toString();
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    if (devices.isEmpty()) {
        qDebug() << "No video input devices found.";
    } else {
        for (const QCameraDevice &cameraDevice : devices) {
            if (cameraDevice.description() == deviceDescription) {
                setCamera(cameraDevice);
                break;
            }
        }
    }
}

void MainWindow::setCamera(const QCameraDevice &cameraDevice)
{
    m_cameraManager->setCamera(cameraDevice);
    m_cameraManager->setVideoOutput(videoPane);  // Add this line

    queryResolutions();

    m_cameraManager->startCamera();

    VideoHid::getInstance().start();
}

void MainWindow::queryResolutions()
{
    QPair<int, int> resolution = VideoHid::getInstance().getResolution();
    qCDebug(log_ui_mainwindow) << "Input resolution: " << resolution;
    GlobalVar::instance().setInputWidth(resolution.first);
    GlobalVar::instance().setInputHeight(resolution.second);
    video_width = GlobalVar::instance().getCaptureWidth();
    video_height = GlobalVar::instance().getCaptureHeight();

    float input_fps = VideoHid::getInstance().getFps();
    updateResolutions(resolution.first, resolution.second, input_fps, video_width, video_height, GlobalVar::instance().getCaptureFps());
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    qCDebug(log_ui_mainwindow) << "Handle window resize event.";
    QMainWindow::resizeEvent(event);  // Call base class implementation

    // Define the desired aspect ratio
    qreal aspect_ratio = static_cast<qreal>(video_width) / video_height;

    int titleBarHeight = this->frameGeometry().height() - this->geometry().height();
    qCDebug(log_ui_mainwindow) << "Aspect ratio:" << aspect_ratio << ", Width:" << video_width << "Height:" << video_height;
    qCDebug(log_ui_mainwindow) << "menuBar height:" << this->menuBar()->height() << ", statusbar height:" << ui->statusbar->height() << ", titleBarHeight" << titleBarHeight;

    // Calculate the new height based on the width and the aspect ratio
    // int new_width = static_cast<int>((height() -  this->menuBar()->height() - ui->statusbar->height()) * aspect_ratio);
    int new_height = static_cast<int>(width() / aspect_ratio) + this->menuBar()->height() + ui->statusbar->height();

    // Set the new size of the window
    qCDebug(log_ui_mainwindow) << "Resize to " << width() << "x" << new_height;
    resize(width(), new_height);

    GlobalVar::instance().setWinWidth(this->width());
    GlobalVar::instance().setWinHeight(this->height());
    videoPane->setMinimumSize(this->width(),
        this->height() - ui->statusbar->height() - ui->menubar->height());
    videoPane->resize(this->width(), this->height() - ui->statusbar->height() - ui->menubar->height());
    scrollArea->resize(this->width(), this->height() - ui->statusbar->height() - ui->menubar->height());
    // scrollArea->ensureWidgetVisible(videoPane);
}


void MainWindow::moveEvent(QMoveEvent *event) {
    // Get the old and new positions
    QPoint oldPos = event->oldPos();
    QPoint newPos = event->pos();
    
    scrollTimer->start(100);
    // Calculate the position delta
    QPoint delta = newPos - oldPos;

    qCDebug(log_ui_mainwindow) << "Window move delta: " << delta;

    // Call the base class implementation
    QWidget::moveEvent(event);

    //calculate_video_position();
}

void MainWindow::calculate_video_position(){

    double aspect_ratio = static_cast<double>(video_width) / video_height;

    int scaled_window_width, scaled_window_height;
    int titleBarHeight = this->frameGeometry().height() - this->geometry().height();
    int statusBarHeight = ui->statusbar->height();
    QMenuBar *menuBar = this->menuBar();
    int menuBarHeight = menuBar->height();

    double widget_ratio = static_cast<double>(width()) / (height()-titleBarHeight-statusBarHeight-menuBarHeight);

    qCDebug(log_ui_mainwindow) << "titleBarHeight: " << titleBarHeight;
    qCDebug(log_ui_mainwindow) << "statusBarHeight: " << statusBarHeight;
    qCDebug(log_ui_mainwindow) << "menuBarHeight: " << menuBarHeight;

    if (widget_ratio < aspect_ratio) {
        // Window is relatively shorter, scale the window by video width
        scaled_window_width =  static_cast<int>(ui->centralwidget->height() * aspect_ratio);
        scaled_window_height = ui->centralwidget->height() + titleBarHeight + statusBarHeight+menuBarHeight;
    } else {
        // Window is relatively taller, scale the window by video height
        scaled_window_width = ui->centralwidget->width();
        scaled_window_height =static_cast<int>(ui->centralwidget->width()) / aspect_ratio + titleBarHeight + statusBarHeight+menuBarHeight;
    }
    resize(scaled_window_width, scaled_window_height);

    GlobalVar::instance().setMenuHeight(menuBarHeight);
    GlobalVar::instance().setTitleHeight(titleBarHeight);
    GlobalVar::instance().setStatusbarHeight(statusBarHeight);
    QSize windowSize = this->size();
    GlobalVar::instance().setWinWidth(windowSize.width());
    GlobalVar::instance().setWinHeight(windowSize.height());
}


void MainWindow::keyPressEvent(QKeyEvent *event)
{
    m_inputHandler->handleKeyPress(event);
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    m_inputHandler->handleKeyRelease(event);
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    m_inputHandler->handleMousePress(event);
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_inputHandler->handleMouseRelease(event);
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    
    lastMousePos = event->pos();
    qDebug() << "lastMousePos: " ;
    m_inputHandler->handleMouseMove(event);
    QMainWindow::mouseMoveEvent(event);
    
}

void MainWindow::updateScrollbars() {
    // Get the screen geometry using QScreen

    // Check if the mouse is near the edges of the screen
    const int edgeThreshold = 300; // Adjust this value as needed

    int deltaX = 0;
    int deltaY = 0;

    if (lastMousePos.x() < edgeThreshold) {
        // Move scrollbar to the left
        deltaX = -10; // Adjust step size as needed
    } else if (lastMousePos.x() > 4096*factorScale - edgeThreshold) {
        // Move scrollbar to the right
        deltaX = 10; // Adjust step size as needed
    }

    if (lastMousePos.y() < edgeThreshold) {
        // Move scrollbar up
        deltaY = -10; // Adjust step size as needed
    } else if (lastMousePos.y() > 4096*factorScale - edgeThreshold) {
        // Move scrollbar down
        deltaY = 10; // Adjust step size as needed
    }

    // Update scrollbars
    scrollArea->horizontalScrollBar()->setValue(scrollArea->horizontalScrollBar()->value() + deltaX);
    scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->value() + deltaY);
}

void MainWindow::onActionRelativeTriggered()
{
    QPoint globalPosition = videoPane->mapToGlobal(QPoint(0, 0));

    QRect globalGeometry = QRect(globalPosition, videoPane->geometry().size());

    // move the mouse to window center
    QPoint center = globalGeometry.center();
    QCursor::setPos(center);

    GlobalVar::instance().setAbsoluteMouseMode(false);
    videoPane->hideHostMouse();

    this->popupMessage("Long press ESC to exit.");
}

void MainWindow::onActionAbsoluteTriggered()
{
    GlobalVar::instance().setAbsoluteMouseMode(true);
}

void MainWindow::onActionResetHIDTriggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(this, "Confirm Reset Keyboard and Mouse?",
                                        "Resetting the Keyboard & Mouse chip will apply new settings. Do you want to proceed?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        qCDebug(log_ui_mainwindow) << "onActionResetHIDTriggered";
        HostManager::getInstance().resetHid();
    } else {
        qCDebug(log_ui_mainwindow) << "Reset HID canceled by user.";
    }
}

void MainWindow::onActionFactoryResetHIDTriggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(this, "Confirm Factory Reset HID Chip?",
                                        "Factory reset the HID chip. Proceed?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        qCDebug(log_ui_mainwindow) << "onActionFactoryResetHIDTriggered";
        SerialPortManager::getInstance().factoryResetHipChip();
        // HostManager::getInstance().resetHid();
    } else {
        qCDebug(log_ui_mainwindow) << "Factory reset HID chip canceled by user.";
    }
}

void MainWindow::onActionResetSerialPortTriggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirm Reset Serial Port?",
                                        "Resetting the serial port will close and re-open it without changing settings. Proceed?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        qCDebug(log_ui_mainwindow) << "onActionResetSerialPortTriggered";
        HostManager::getInstance().resetSerialPort();
    } else {
        qCDebug(log_ui_mainwindow) << "Serial port reset canceled by user.";
    }
}

void MainWindow::onActionSwitchToHostTriggered()
{
    qCDebug(log_ui_mainwindow) << "Switchable USB to host...";
    VideoHid::getInstance().switchToHost();
    ui->actionTo_Host->setChecked(true);
    ui->actionTo_Target->setChecked(false);
}

void MainWindow::onActionSwitchToTargetTriggered()
{
    qCDebug(log_ui_mainwindow) << "Switchable USB to target...";
    VideoHid::getInstance().switchToTarget();
    ui->actionTo_Host->setChecked(false);
    ui->actionTo_Target->setChecked(true);
}

void MainWindow::onToggleSwitchStateChanged(int state)
{
    qCDebug(log_ui_mainwindow) << "Toggle switch state changed to:" << state;
    if (state == Qt::Checked) {
        onActionSwitchToTargetTriggered();
    } else {
        onActionSwitchToHostTriggered();
    }
}

void MainWindow::onResolutionChange(const int& width, const int& height, const float& fps)
{
    GlobalVar::instance().setInputWidth(width);
    GlobalVar::instance().setInputHeight(height);
    statusWidget->setInputResolution(width, height, fps);
}

void MainWindow::onTargetUsbConnected(const bool isConnected)
{
    statusWidget->setTargetUsbConnected(isConnected);
}

void MainWindow::updateBaudrateMenu(int baudrate){
    // Find the QAction corresponding to the current baudrate and check it
    QList<QAction*> actions = ui->menuBaudrate->actions();
    for (QAction* action : actions) {
        bool ok;
        int actionBaudrate = action->text().toInt(&ok);
        if (ok && actionBaudrate == baudrate) {
            action->setChecked(true);
        } else {
            action->setChecked(false);
        }
    }

    // If the current baudrate is not in the menu, add a new option and check it
    bool baudrateFound = false;
    for (QAction* action : actions) {
        if (action->text().toInt() == baudrate) {
            baudrateFound = true;
            break;
        }
    }
    if (!baudrateFound) {
        QAction* newAction = new QAction(QString::number(baudrate), this);
        newAction->setCheckable(true);
        newAction->setChecked(true);
        ui->menuBaudrate->addAction(newAction);
        connect(newAction, &QAction::triggered, this, [this, newAction]() {
            onBaudrateMenuTriggered(newAction);
        });
    }
}

void MainWindow::onActionPasteToTarget()
{
    HostManager::getInstance().pasteTextToTarget(QGuiApplication::clipboard()->text());
}

void MainWindow::onActionScreensaver()
{
    HostManager::getInstance().autoMoveMouse();
}

void MainWindow::onToggleVirtualKeyboard()
{
    bool isVisible = toolbarManager->getToolbar()->isVisible();
    toolbarManager->getToolbar()->setVisible(!isVisible);

    // Toggle the icon
    QString iconPath = isVisible ? ":/images/keyboard-down.svg" : ":/images/keyboard-up.svg";
    QIcon icon(iconPath);
    ui->virtualKeyboardButton->setIcon(icon);
}

void MainWindow::popupMessage(QString message)
{
    QDialog dialog;
    dialog.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    QVBoxLayout layout;
    dialog.setLayout(&layout);

    // Set the font of the message box
    QFont font;
    font.setPointSize(18); // Set the size of the font
    font.setBold(true); // Make the font bold

    QLabel label(message);
    label.setFont(font); // Use the same font as before
    layout.addWidget(&label);

    dialog.adjustSize(); // Resize the dialog to fit its content

    // Show the dialog off-screen
    dialog.move(-1000, -1000);
    dialog.show();

    // Now that the dialog is shown, we can get its correct dimensions
    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    int x = screenGeometry.width() - dialog.frameGeometry().width();
    int y = 0;
    qCDebug(log_ui_mainwindow) << "x: " << x << "y:" << y;

    // Move the dialog to the desired position
    dialog.move(x, y);

    // Auto hide in 3 seconds
    QTimer::singleShot(3000, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::updateCameraActive(bool active) {
    qCDebug(log_ui_mainwindow) << "Camera active: " << active;
    if(active){
        qCDebug(log_ui_mainwindow) << "Set index to : " << 1;
        stackedLayout->setCurrentIndex(1);
    } else {
        qCDebug(log_ui_mainwindow) << "Set index to : " << 0;
        stackedLayout->setCurrentIndex(0);
    }
    queryResolutions();
}

void MainWindow::updateRecordTime()
{
    QString str = tr("Recorded %1 sec").arg(m_mediaRecorder->duration() / 1000);
    ui->statusbar->showMessage(str);
}

void MainWindow::processCapturedImage(int requestId, const QImage &img)
{
    Q_UNUSED(requestId);
    QImage scaledImage =
            img.scaled(ui->centralwidget->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

   // ui->lastImagePreviewLabel->setPixmap(QPixmap::fromImage(scaledImage));

    // Display captured image for 4 seconds.
    displayCapturedImage();
    QTimer::singleShot(4000, this, &MainWindow::displayViewfinder);
}

// void MainWindow::configureCaptureSettings()
// {
//     // if (m_doImageCapture)
//     //     configureImageSettings();
//     // else
//     configureVideoSettings();

// }

// void MainWindow::configureVideoSettings()
// {
//     VideoSettings settingsDialog(m_camera.data());

//     if (settingsDialog.exec())
//         settingsDialog.applySettings();
// }

// void MainWindow::configureImageSettings()
// {
//     ImageSettings settingsDialog(m_imageCapture.get());

//     if (settingsDialog.exec() == QDialog::Accepted)
//         settingsDialog.applyImageSettings();
// }

void MainWindow::configureSettings() {
    qDebug() << "configureSettings";
    qDebug() << "settingsDialog: " << settingsDialog;
    if (!settingsDialog){
        qDebug() << "Creating settings dialog";
        settingsDialog = new SettingDialog(m_cameraManager->getCamera(), this);
        connect(settingsDialog, &SettingDialog::cameraSettingsApplied, this, &MainWindow::loadCameraSettingAndSetCamera);
        // connect the finished signal to the set the dialog pointer to nullptr
        connect(settingsDialog, &QDialog::finished, this, [this](){
            settingsDialog = nullptr;
        });
        settingsDialog->show();
    }else{
        settingsDialog->raise();
        settingsDialog->activateWindow();
    }
}

void MainWindow::debugSerialPort() {
    qDebug() << "debug dialog" ;
    qDebug() << "serialPortDebugDialog: " << serialPortDebugDialog;
    if (!serialPortDebugDialog){
        qDebug() << "Creating serial port debug dialog";
        serialPortDebugDialog = new SerialPortDebugDialog();
        // connect the finished signal to the set the dialog pointer to nullptr
        connect(serialPortDebugDialog, &QDialog::finished, this, [this]() {
            serialPortDebugDialog = nullptr;
        });
        serialPortDebugDialog->show();
    }else{
        serialPortDebugDialog->raise();
        serialPortDebugDialog->activateWindow();
    }
}

void MainWindow::purchaseLink(){
    QDesktopServices::openUrl(QUrl("https://www.crowdsupply.com/techxartisan/openterface-mini-kvm"));
}

void MainWindow::feedbackLink(){
    QDesktopServices::openUrl(QUrl("https://forms.gle/KNQPTNfXCPUPybgG9"));
}

void MainWindow::aboutLink(){
    QDesktopServices::openUrl(QUrl("https://openterface.com/"));
}

void MainWindow::versionInfo() {
    QString applicationName = QApplication::applicationName();
    QString organizationName = QApplication::organizationName();
    QString applicationVersion = QApplication::applicationVersion();
    QString osVersion = QSysInfo::prettyProductName();
    QString title = tr("%1").arg(applicationName);
    QString message = tr("Version:\t %1 \nQT:\t %2\nOS:\t %3")
                          .arg(applicationVersion)
                          .arg(qVersion())
                          .arg(osVersion);

    QMessageBox msgBox;
    msgBox.setWindowTitle(title);
    msgBox.setText(message);

    QPushButton *copyButton = msgBox.addButton(tr("Copy"), QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Close);

    connect(copyButton, &QPushButton::clicked, this, &MainWindow::copyToClipboard);

    msgBox.exec();

    if (msgBox.clickedButton() == copyButton) {
        copyToClipboard();
    }
}

void MainWindow::copyToClipboard(){
    QString applicationName = QApplication::applicationName();
    QString organizationName = QApplication::organizationName();
    QString applicationVersion = QApplication::applicationVersion();

    QString message = tr("Version:\t %1 \nOrganization:\t %2")
                          .arg(applicationVersion)
                          .arg(organizationName);

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(message);
}

void MainWindow::onFunctionKeyPressed(int key)
{
    HostManager::getInstance().handleFunctionKey(key);
}

void MainWindow::onCtrlAltDelPressed()
{
    HostManager::getInstance().sendCtrlAltDel();
}

void MainWindow::onRepeatingKeystrokeChanged(int interval)
{
    HostManager::getInstance().setRepeatingKeystroke(interval);
}

void MainWindow::onPaletteChanged()
{
    iconColor = getContrastingColor(palette().color(QPalette::WindowText));
    onLastKeyPressed("");
    onLastMouseLocation(QPoint(0, 0), "");
}

void MainWindow::record()
{
    m_cameraManager->startRecording();
}

void MainWindow::pause()
{
    m_cameraManager->stopRecording();
}

void MainWindow::setMuted(bool muted)
{
   // m_captureSession.audioInput()->setMuted(muted);
}

void MainWindow::takeImage()
{
    m_cameraManager->takeImage();
}

void MainWindow::displayCaptureError(int id, const QImageCapture::Error error,
                                 const QString &errorString)
{
    Q_UNUSED(id);
    Q_UNUSED(error);
    QMessageBox::warning(this, tr("Image Capture Error"), errorString);
    m_isCapturingImage = false;
}

void MainWindow::setExposureCompensation(int index)
{
    m_camera->setExposureCompensation(index * 0.5);
}


void MainWindow::displayCameraError()
{
    qWarning() << "Camera error: " << m_camera->errorString();
    if (m_camera->error() != QCamera::NoError){
        qCDebug(log_ui_mainwindow) << "A camera has been disconnected.";

        stackedLayout->setCurrentIndex(0);

        stop();
    }
}

void MainWindow::stop(){
    qDebug() << "Stop camera data...";
    disconnect(m_camera.data());
    qDebug() << "Camera data stopped.";
    m_audioManager->disconnect();
    qDebug() << "Audio manager stopped.";

    m_captureSession.disconnect();
    qDebug() << "Capture session stopped.";
    VideoHid::getInstance().stop();
    qDebug() << "Video HID stopped.";
    m_camera->stop();
    qDebug() << "Camera stopped.";
}

void MainWindow::updateCameraDevice(QAction *action)
{
    setCamera(qvariant_cast<QCameraDevice>(action->data()));
}

void MainWindow::displayViewfinder()
{
    //ui->stackedWidget->setCurrentIndex(0);
}

void MainWindow::displayCapturedImage()
{
    //ui->stackedWidget->setCurrentIndex(1);
}

void MainWindow::onBaudrateMenuTriggered(QAction* action)
{
    bool ok;
    int baudrate = action->text().toInt(&ok);
    if (ok) {
        SerialPortManager::getInstance().setBaudRate(baudrate);
    }
}

void MainWindow::imageSaved(int id, const QString &fileName)
{
    Q_UNUSED(id);
    ui->statusbar->showMessage(tr("Captured \"%1\"").arg(QDir::toNativeSeparators(fileName)));

    m_isCapturingImage = false;
    if (m_applicationExiting)
        close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_isCapturingImage) {
        setEnabled(false);
        m_applicationExiting = true;
        event->ignore();
    } else {
        event->accept();
    }
}

void MainWindow::updateCameras()
{
    qCDebug(log_ui_mainwindow) << "Update cameras...";
    // ui->menuSource->clear();
    const QList<QCameraDevice> availableCameras = QMediaDevices::videoInputs();

    for (const QCameraDevice &camera : availableCameras) {
        if (!m_lastCameraList.contains(camera)) {
            qCDebug(log_ui_mainwindow) << "A new camera has been connected:" << camera.description();

            if (!camera.description().contains("Openterface"))
                continue;

            qCDebug(log_ui_mainwindow) << "Update openterface layer to top layer.";

            stackedLayout->setCurrentIndex(1);

            //If the default camera is not an Openterface camera, set the camera to the first Openterface camera
            if (!QMediaDevices::defaultVideoInput().description().contains("Openterface")) {
                qCDebug(log_ui_mainwindow) << "Set defualt camera to the Openterface camera...";
            } else {
                qCDebug(log_ui_mainwindow) << "The default camera is" << QMediaDevices::defaultVideoInput().description();
            }
            m_audioManager->initializeAudio();
            setCamera(camera);
            break;
        }
    }
}

void MainWindow::checkCameraConnection()
{
    const QList<QCameraDevice> availableCameras = QMediaDevices::videoInputs();

    if (availableCameras != m_lastCameraList) {
        // The list of available cameras has changed
        if (availableCameras.count() > m_lastCameraList.count()) {
            // A new camera has been connected
            // Find out which camera was connected
        }
        m_lastCameraList = availableCameras;
    }
}


void MainWindow::onPortConnected(const QString& port, const int& baudrate) {
    statusWidget->setConnectedPort(port, baudrate);
    updateBaudrateMenu(baudrate);
}

void MainWindow::onStatusUpdate(const QString& status) {
    statusWidget->setStatusUpdate(status);
}

void MainWindow::onLastKeyPressed(const QString& key) {
    QString svgPath;
    if(key == ""){
        svgPath = QString(":/images/keyboard.svg");
    }else{
        svgPath = QString(":/images/keyboard-pressed.svg");
    }

    // Load the SVG into a QPixmap
    // iconColor = palette().color(QPalette::WindowText);
    // qDebug() << "keyboard iconColor: " << iconColor;
    QPixmap pixmap = recolorSvg(svgPath, iconColor, QSize(18, 18)); // Adjust the size as needed

    // Set the QPixmap to the QLabel
    keyPressedLabel->setPixmap(pixmap);
    keyLabel->setText(QString("%1").arg(key));
}

void MainWindow::onLastMouseLocation(const QPoint& location, const QString& mouseEvent) {
    // Load the SVG into a QPixmap
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

    // Load the SVG into a QPixmap
    // iconColor = palette().color(QPalette::WindowText);
    // qDebug() << "mouse iconColor: " << iconColor;
    QPixmap pixmap = recolorSvg(svgPath, iconColor, QSize(12, 12)); // Adjust the size as needed

    // Set the QPixmap to the QLabel
    mouseLabel->setPixmap(pixmap);
    mouseLocationLabel->setText(QString("(%1,%2)").arg(location.x()).arg(location.y()));
}

void MainWindow::onSwitchableUsbToggle(const bool isToTarget) {
    if (isToTarget) {
        qDebug() << "UI Switchable USB to target...";
        ui->actionTo_Host->setChecked(false);
        ui->actionTo_Target->setChecked(true);
        toggleSwitch->setChecked(true);
    } else {
        qDebug() << "UI Switchable USB to host...";
        ui->actionTo_Host->setChecked(true);
        ui->actionTo_Target->setChecked(false);
        toggleSwitch->setChecked(false);
    }
    SerialPortManager::getInstance().restartSwitchableUSB();
}

void MainWindow::updateResolutions(const int input_width, const int input_height, const float input_fps, const int capture_width, const int capture_height, const int capture_fps)
{
    statusWidget->setInputResolution(input_width, input_height, input_fps);
    statusWidget->setCaptureResolution(capture_width, capture_height, capture_fps);
}
