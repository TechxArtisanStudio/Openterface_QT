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
#include "ui_mainwindow.h"
#include "globalsetting.h"
#include "ui/statusbar/statusbarmanager.h"
#include "host/HostManager.h"
#include "host/cameramanager.h"
#include "serial/SerialPortManager.h"
#include "device/DeviceManager.h"
#include "device/HotplugMonitor.h"
#include "ui/preferences/settingdialog.h"
#include "ui/help/helppane.h"
#include "ui/videopane.h"
#include "video/videohid.h"
#include "ui/help/versioninfomanager.h"
#include "ui/TaskManager.h"
#include "ui/advance/serialportdebugdialog.h"
#include "ui/advance/firmwareupdatedialog.h"
#include "ui/advance/envdialog.h"
#include "ui/advance/updatedisplaysettingsdialog.h"

#include <QCameraDevice>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QMediaRecorder>
#include <QStackedLayout>
#include <QMessageBox>
#include <QCheckBox>
#include <QImageCapture>
#include <QToolBar>
#include <QClipboard>
#include <QInputMethod>
#include <QAction>
#include <QActionGroup>
#include <QImage>
#include <QKeyEvent>
#include <QPalette>
#include <QDir>
#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QSvgRenderer>
#include <QPainter>
#include <QDesktopServices>
#include <QSysInfo>
#include <QMenuBar>
#include <QPushButton>
#include <QComboBox>
#include <QScrollBar>
#include <QGuiApplication>
#include <QToolTip>
#include <QScreen>

#ifdef Q_OS_WIN
#include "host/backend/qtbackendhandler.h"
#endif

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

MainWindow::MainWindow(LanguageManager *languageManager, QWidget *parent) :  ui(new Ui::MainWindow),
                            m_audioManager(&AudioManager::getInstance()),
                            videoPane(new VideoPane(this)),
                            stackedLayout(new QStackedLayout(this)),
                            toolbarManager(new ToolbarManager(this)),
                            toggleSwitch(new ToggleSwitch(this)),
                            m_cameraManager(new CameraManager(this)),
                            m_versionInfoManager(new VersionInfoManager(this)),
                            m_languageManager(languageManager),
                            m_screenSaverManager(new ScreenSaverManager(this)),
                            m_cornerWidgetManager(new CornerWidgetManager(this)),
                            m_windowControlManager(nullptr)
{
    Q_UNUSED(parent);

    qCDebug(log_ui_mainwindow) << "Init MainWindow...";
    
    qCDebug(log_ui_mainwindow) << "Setting up UI...";
    ui->setupUi(this);
    qCDebug(log_ui_mainwindow) << "UI setup complete";
    
    // Initialize WindowLayoutCoordinator early - needed before checkInitSize()
    qCDebug(log_ui_mainwindow) << "Initializing WindowLayoutCoordinator...";
    m_windowLayoutCoordinator = new WindowLayoutCoordinator(this, videoPane, menuBar(), statusBar(), this);
    qCDebug(log_ui_mainwindow) << "WindowLayoutCoordinator initialized";
    
    qCDebug(log_ui_mainwindow) << "Setting menubar on corner widget manager...";
    m_cornerWidgetManager->setMenuBar(ui->menubar);
    qCDebug(log_ui_mainwindow) << "Menubar set";
    // ui->menubar->setCornerWidget(m_cornerWidgetManager->getCornerWidget(), Qt::TopRightCorner);

    qCDebug(log_ui_mainwindow) << "Initializing keyboard layouts...";
    initializeKeyboardLayouts();
    qCDebug(log_ui_mainwindow) << "Keyboard layouts initialized";
    
    qCDebug(log_ui_mainwindow) << "Connecting corner widget zoom signals...";
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomInClicked, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->zoomIn();
            mouseEdgeTimer->start(edgeDuration);
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomOutClicked, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->zoomOut();
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomReductionClicked, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->zoomReduction();
            if (mouseEdgeTimer->isActive()) {
                mouseEdgeTimer->stop();
            }
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::screenScaleClicked, this, &MainWindow::configScreenScale);
    connect(m_cornerWidgetManager, &CornerWidgetManager::virtualKeyboardClicked, this, &MainWindow::onToggleVirtualKeyboard);
    connect(m_cornerWidgetManager, &CornerWidgetManager::captureClicked, this, &MainWindow::takeImageDefault);
    connect(m_cornerWidgetManager, &CornerWidgetManager::fullScreenClicked, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->fullScreen();
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::pasteClicked, this, &MainWindow::onActionPasteToTarget);
    connect(m_cornerWidgetManager, &CornerWidgetManager::screensaverClicked, this, &MainWindow::onActionScreensaver);
    connect(m_cornerWidgetManager, &CornerWidgetManager::toggleSwitchChanged, this, &MainWindow::onToggleSwitchStateChanged);
    connect(m_cornerWidgetManager, &CornerWidgetManager::keyboardLayoutChanged, this, &MainWindow::onKeyboardLayoutCombobox_Changed);
    qCDebug(log_ui_mainwindow) << "All corner widget signals connected";

    qCDebug(log_ui_mainwindow) << "Setting mouse auto hide...";
    GlobalVar::instance().setMouseAutoHide(GlobalSetting::instance().getMouseAutoHideEnable());
    qCDebug(log_ui_mainwindow) << "Mouse auto hide set";

    qCDebug(log_ui_mainwindow) << "Creating StatusBarManager...";
    m_statusBarManager = new StatusBarManager(ui->statusbar, this);
    qCDebug(log_ui_mainwindow) << "StatusBarManager created";
    
    qCDebug(log_ui_mainwindow) << "Getting TaskManager instance...";
    taskmanager = TaskManager::instance();
    qCDebug(log_ui_mainwindow) << "TaskManager instance obtained";
    
    // Connect DeviceManager hotplug events to StatusBarManager
    qCDebug(log_ui_mainwindow) << "Getting DeviceManager instance...";
    DeviceManager& deviceManager = DeviceManager::getInstance();
    qCDebug(log_ui_mainwindow) << "Getting HotplugMonitor...";
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    qCDebug(log_ui_mainwindow) << "HotplugMonitor obtained";
    if (hotplugMonitor) {
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn, 
                m_statusBarManager, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindow) << "MainWindow: Received newDevicePluggedIn signal for port:" << device.portChain;
                    m_statusBarManager->showNewDevicePluggedIn(device.portChain);
                });
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged, 
                m_statusBarManager, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindow) << "MainWindow: Received deviceUnplugged signal for port:" << device.portChain;
                    m_statusBarManager->showDeviceUnplugged(device.portChain);
                });
                
        // Connect hotplug monitor to camera manager for device unplugging
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
                this, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindow) << "MainWindow: Attempting camera deactivation for unplugged device port:" << device.portChain;
                    
                    // Only deactivate camera if the device has a camera component
                    if (!device.hasCameraDevice()) {
                        qCDebug(log_ui_mainwindow) << "Device at port" << device.portChain << "has no camera component, skipping camera deactivation";
                        return;
                    }
                    
                    bool deactivated = m_cameraManager->deactivateCameraByPortChain(device.portChain);
                    if (deactivated) {
                        qCInfo(log_ui_mainwindow) << "✓ Camera deactivated for unplugged device at port:" << device.portChain;
                        stackedLayout->setCurrentIndex(0);
                    } else {
                        qCDebug(log_ui_mainwindow) << "Camera deactivation skipped or not needed for port:" << device.portChain;
                    }
                });
                
        // Connect hotplug monitor to camera manager for auto-switching
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
                this, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindow) << "MainWindow: Attempting camera auto-switch for new device port:" << device.portChain;
                    
                    // Only attempt auto-switch if the device has a camera component
                    if (!device.hasCameraDevice()) {
                        qCDebug(log_ui_mainwindow) << "Device at port" << device.portChain << "has no camera component, skipping auto-switch";
                        return;
                    }
                    
                    bool switchSuccess = m_cameraManager->tryAutoSwitchToNewDevice(device.portChain);
                    if (switchSuccess) {
                        qCInfo(log_ui_mainwindow) << "✓ Camera auto-switched to new device at port:" << device.portChain;
                        stackedLayout->setCurrentIndex(stackedLayout->indexOf(videoPane));
                    } else {
                        qCDebug(log_ui_mainwindow) << "Camera auto-switch skipped or failed for port:" << device.portChain;
                    }
                });
                

        qCDebug(log_ui_mainwindow) << "Connected hotplug monitor to status bar manager and camera manager";
    } else {
        qCWarning(log_ui_mainwindow) << "Failed to get hotplug monitor from device manager";
    }
    
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setLayout(stackedLayout);
    centralWidget->setMouseTracking(true);

    HelpPane *helpPane = new HelpPane;
    stackedLayout->addWidget(helpPane);
    
    // Set size policy and minimum size for videoPane - use proper sizing
    videoPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // videoPane->setMinimumSize(640, 480); // Set reasonable minimum size
    
    // Add videoPane directly to stacked layout without scroll area
    stackedLayout->addWidget(videoPane);

    stackedLayout->setCurrentIndex(0);

    setCentralWidget(centralWidget);
    qCDebug(log_ui_mainwindow) << "Set host manager event callback...";
    HostManager::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe Video HID connected...";
    VideoHid::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Starting AudioManager...";
    AudioManager::getInstance().start();

    qCDebug(log_ui_mainwindow) << "Observe video input changed...";
    // Note: Automatic camera switching on device changes has been disabled
    // Camera devices will only be switched manually through the UI
    // connect(&m_source, &QMediaDevices::videoInputsChanged, this, &MainWindow::updateCameras);

    qCDebug(log_ui_mainwindow) << "Observe Relative/Absolute toggle...";
    connect(ui->actionRelative, &QAction::triggered, this, &MainWindow::onActionRelativeTriggered);
    connect(ui->actionAbsolute, &QAction::triggered, this, &MainWindow::onActionAbsoluteTriggered);

    connect(ui->actionMouseAutoHide, &QAction::triggered, this, &MainWindow::onActionMouseAutoHideTriggered);
    connect(ui->actionMouseAlwaysShow, &QAction::triggered, this, &MainWindow::onActionMouseAlwaysShowTriggered);

    qCDebug(log_ui_mainwindow) << "Observe factory reset HID triggered...";
    connect(ui->actionFactory_reset_HID, &QAction::triggered, this, &MainWindow::onActionFactoryResetHIDTriggered);

    qCDebug(log_ui_mainwindow) << "Observe reset Serial Port triggered...";
    connect(ui->actionResetSerialPort, &QAction::triggered, this, &MainWindow::onActionResetSerialPortTriggered);

    qDebug() << "Observe Hardware change MainWindow triggered...";

    qCDebug(log_ui_mainwindow) << "Creating and setting up ToggleSwitch...";
    toggleSwitch->setFixedSize(78, 28);  // Adjust size as needed
    connect(toggleSwitch, &ToggleSwitch::stateChanged, this, &MainWindow::onToggleSwitchStateChanged);

    // Add the ToggleSwitch as the last button in the cornerWidget's layout



    qCDebug(log_ui_mainwindow) << "Observe switch usb connection trigger...";
    connect(ui->actionTo_Host, &QAction::triggered, this, &MainWindow::onActionSwitchToHostTriggered);
    connect(ui->actionTo_Target, &QAction::triggered, this, &MainWindow::onActionSwitchToTargetTriggered);

    qCDebug(log_ui_mainwindow) << "Observe action paste from host...";
    connect(ui->actionPaste, &QAction::triggered, this, &MainWindow::onActionPasteToTarget);

    addToolBar(Qt::TopToolBarArea, toolbarManager->getToolbar());
    toolbarManager->getToolbar()->setVisible(false);
    
    // Set toolbar manager on layout coordinator for animation coordination
    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->setToolbarManager(toolbarManager);
    }
    
    // Initialize Window Control Manager for auto-hide toolbar behavior
    m_windowControlManager = new WindowControlManager(this, toolbarManager->getToolbar(), this);
    m_windowControlManager->setAutoHideEnabled(true);
    m_windowControlManager->setAutoHideDelay(10000);  // 10 seconds
    m_windowControlManager->setEdgeDetectionThreshold(5);  // 5 pixels from top edge
    m_windowControlManager->setAnimationDuration(300);  // 300ms animation
    
    connect(m_windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
            this, &MainWindow::onToolbarVisibilityChanged);
    
    connect(m_cameraManager, &CameraManager::cameraActiveChanged, this, &MainWindow::updateCameraActive);
    connect(m_cameraManager, &CameraManager::cameraError, this, &MainWindow::displayCameraError);
    connect(m_cameraManager, &CameraManager::imageCaptured, this, &MainWindow::processCapturedImage);
    connect(m_cameraManager, &CameraManager::resolutionsUpdated, this, &MainWindow::onResolutionsUpdated);
    connect(m_cameraManager, &CameraManager::newDeviceAutoConnected, this, [this](const QCameraDevice& device, const QString& portChain) {
        qCInfo(log_ui_mainwindow) << "Camera auto-connected to new device:" << device.description() << "at port:" << portChain;
        // popupMessage(QString("Camera connected to new device: %1").arg(device.description()));
    });
    
    // Connect camera switching status signals to status bar
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitching, 
            m_statusBarManager, &StatusBarManager::showCameraSwitching);
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitchComplete, 
            m_statusBarManager, &StatusBarManager::showCameraSwitchComplete);
    
    // Connect camera switching signals to video pane for frame preservation
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitching,
            videoPane, &VideoPane::onCameraDeviceSwitching);
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitchComplete,
            videoPane, &VideoPane::onCameraDeviceSwitchComplete);
    
    // Connect VideoPane mouse events to status bar
    connect(videoPane, &VideoPane::mouseMoved,
            m_statusBarManager, &StatusBarManager::onLastMouseLocation);
    connect(&VideoHid::getInstance(), &VideoHid::inputResolutionChanged, this, &MainWindow::onInputResolutionChanged);
    connect(&VideoHid::getInstance(), &VideoHid::resolutionChangeUpdate, this, &MainWindow::onResolutionChange);

    
    qCDebug(log_ui_mainwindow) << "Test actionTCPServer true...";
    ui->actionTCPServer->setVisible(true);
    connect(ui->actionTCPServer, &QAction::triggered, this, &MainWindow::startServer);

    qDebug() << "Init camera...";
    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->checkInitSize();
    }
    initCamera();
    
    // Initialize camera with video output for proper startup
    qCDebug(log_ui_mainwindow) << "Initializing camera with video output...";
    QTimer::singleShot(200, this, [this]() {
        bool success = m_cameraManager->initializeCameraWithVideoOutput(videoPane);
        if (success) {
            qDebug() << "✓ Camera successfully initialized with video output";
        } else {
            qCWarning(log_ui_mainwindow) << "Failed to initialize camera with video output";
        }
    });

    // Initialize audio with a slight delay after camera
    qCDebug(log_ui_mainwindow) << "Initializing audio...";
    QTimer::singleShot(300, this, [this]() {
        m_audioManager->initializeAudio();
        qDebug() << "✓ Audio initialization triggered";
    });

    // Connect palette change signal to the slot
    onLastKeyPressed("");
    onLastMouseLocation(QPoint(0, 0), "");

    // Connect zoom buttons
    
    // Set the window title with the version number
    qDebug() << "Set window title" << APP_VERSION;
    QString windowTitle = QString("Openterface Mini-KVM - %1").arg(APP_VERSION);
    setWindowTitle(windowTitle);

    mouseEdgeTimer = new QTimer(this);
    connect(mouseEdgeTimer, &QTimer::timeout, this, &MainWindow::checkMousePosition);
    // mouseEdgeTimer->start(edgeDuration); // Start the timer with the new duration

    // Initialize coordinators
    qCDebug(log_ui_mainwindow) << "Initializing DeviceCoordinator...";
    m_deviceCoordinator = new DeviceCoordinator(ui->menuDevice, m_cameraManager, this);
    
    qCDebug(log_ui_mainwindow) << "Initializing MenuCoordinator...";
    m_menuCoordinator = new MenuCoordinator(ui->menuLanguages, ui->menuBaudrate, m_languageManager, this, this);
    
    // WindowLayoutCoordinator already initialized earlier in constructor
    
    // Connect serial port manager
    connect(&SerialPortManager::getInstance(), &SerialPortManager::connectedPortChanged, this, &MainWindow::onPortConnected);
    
    // Setup device menu through DeviceCoordinator
    if (m_deviceCoordinator) {
        m_deviceCoordinator->connectHotplugMonitor(hotplugMonitor);
        m_deviceCoordinator->setupDeviceMenu();
    }
    
    // Setup menus through MenuCoordinator
    if (m_menuCoordinator) {
        m_menuCoordinator->setupLanguageMenu();
        connect(m_menuCoordinator, &MenuCoordinator::baudrateChanged, [this](int baudrate) {
            m_menuCoordinator->updateBaudrateMenu(baudrate);
        });
    }
    
    connect(&SerialPortManager::getInstance(), &SerialPortManager::armBaudratePerformanceRecommendation, this, &MainWindow::onArmBaudratePerformanceRecommendation);
    
    // Note: Automatic camera device coordination has been disabled
    // Camera devices will only be switched manually through the UI
    
    qApp->installEventFilter(this);

    // Initial position setup
    // QPoint buttonPos = ui->contrastButton->mapToGlobal(QPoint(0, 0));
    // int menuBarHeight = buttonPos.y() - this->mapToGlobal(QPoint(0, 0)).y();
    // cameraAdjust->updatePosition(menuBarHeight, width());

    // Add this line after ui->setupUi(this)
    connect(ui->actionScriptTool, &QAction::triggered, this, &MainWindow::showScriptTool);
    connect(ui->actionRecordingSettings, &QAction::triggered, this, &MainWindow::showRecordingSettings);
    mouseManager = std::make_unique<MouseManager>();
    keyboardMouse = std::make_unique<KeyboardMouse>();
    semanticAnalyzer = std::make_unique<SemanticAnalyzer>(mouseManager.get(), keyboardMouse.get());
    connect(semanticAnalyzer.get(), &SemanticAnalyzer::captureImg, this, &MainWindow::takeImage);
    connect(semanticAnalyzer.get(), &SemanticAnalyzer::captureAreaImg, this, &MainWindow::takeAreaImage);
    scriptTool = new ScriptTool(this);
    // connect(scriptTool, &ScriptTool::syntaxTreeReady, this, &MainWindow::handleSyntaxTree);
    connect(this, &MainWindow::emitScriptStatus, scriptTool, &ScriptTool::resetCommmandLine);
    connect(semanticAnalyzer.get(), &SemanticAnalyzer::commandIncrease, scriptTool, &ScriptTool::handleCommandIncrement);
    // setTooltip();

    // Add this connection after toolbarManager is created
    connect(toolbarManager, &ToolbarManager::toolbarVisibilityChanged,
            this, &MainWindow::onToolbarVisibilityChanged);
    // qCDebug(log_ui_mainwindow) << "full screen...";
    
    connect(m_languageManager, &LanguageManager::languageChanged, this, &MainWindow::updateUI);
    // Language menu already setup by MenuCoordinator
    // fullScreen();
    qDebug() << "finished initialization";
    
}

void MainWindow::startServer(){
    tcpServer = new TcpServer(this);
    tcpServer->startServer(SERVER_PORT);
    qCDebug(log_ui_mainwindow) << "TCP Server start at port 12345";
    connect(m_cameraManager, &CameraManager::lastImagePath, tcpServer, &TcpServer::handleImgPath);
    connect(tcpServer, &TcpServer::syntaxTreeReady, this, &MainWindow::handleSyntaxTree);
    connect(this, &MainWindow::emitTCPCommandStatus, tcpServer, &TcpServer::recvTCPCommandStatus);
}


void MainWindow::updateUI() {
    ui->retranslateUi(this); // Update the UI elements
    // this->menuBar()->clear();
    if (m_menuCoordinator) {
        m_menuCoordinator->setupLanguageMenu();
    }
    if (m_deviceCoordinator) {
        m_deviceCoordinator->updateDeviceMenu(); // Update device menu when UI language changes
    }
}





void MainWindow::initCamera()
{
    qCDebug(log_ui_mainwindow) << "Camera init...";
#ifdef QT_FEATURE_permissions //Permissions API not compatible with Qt < 6.5 and will cause compilation failure on expanding macro in qtconfigmacros.h
#if QT_CONFIG(permissions)
    // camera 
    QCameraPermission cameraPermission;
    switch (qApp->checkPermission(cameraPermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(cameraPermission, this, &MainWindow::initCamera);
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
        qApp->requestPermission(microphonePermission, this, &MainWindow::initCamera);
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
    // calculate_video_position();
    GlobalVar::instance().setWinWidth(this->width());
    GlobalVar::instance().setWinHeight(this->height());
}



void MainWindow::resizeEvent(QResizeEvent *event) {
    qCDebug(log_ui_mainwindow) << "Resize event triggered. New size:" << event->size();

    static qint64 lastResizeTime = 0;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    const qint64 RESIZE_THROTTLE_MS = 50;
    
    // Skip resize if in fullscreen mode or throttled
    bool inFullscreen = m_windowLayoutCoordinator ? m_windowLayoutCoordinator->isFullScreenMode() : false;
    if (inFullscreen || (currentTime - lastResizeTime) < RESIZE_THROTTLE_MS) {
        return;
    }

    // Check if window is maximized - if so, allow resize to proceed
    bool isMaximized = (this->windowState() & Qt::WindowMaximized);
    
    // Track window state changes for WindowControlManager
    static Qt::WindowStates lastWindowState = this->windowState();
    if (lastWindowState != this->windowState() && m_windowControlManager) {
        m_windowControlManager->onWindowStateChanged(lastWindowState, this->windowState());
        lastWindowState = this->windowState();
    }
    
    if (!isMaximized) {
        // Check if new size exceeds screen bounds only for non-maximized windows
        QScreen *currentScreen = this->screen();
        QRect availableGeometry = currentScreen->availableGeometry();
        int availableWidth = availableGeometry.width();
        int availableHeight = availableGeometry.height();
        
        if (event->size().width() >= availableWidth || event->size().height() >= availableHeight) {
            qCDebug(log_ui_mainwindow) << "Resize event ignored due to exceeding screen bounds.";
            return;
        }
    }
    
    lastResizeTime = currentTime;
    QMainWindow::resizeEvent(event);
    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->doResize();
    }
    qCDebug(log_ui_mainwindow) << "mainwindow size: " << this->size() << "VideoPane size:" << videoPane->size();
}







void MainWindow::moveEvent(QMoveEvent *event) {
    // Get the old and new positions
    QPoint oldPos = event->oldPos();
    QPoint newPos = event->pos();
    
    QPoint delta = newPos - oldPos;

    qCDebug(log_ui_mainwindow) << "Window move delta: " << delta;

    // Call the base class implementation
    QWidget::moveEvent(event);
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

    // Note: scrollbars removed - VideoPane handles zooming internally via QGraphicsView
    // No need to update scrollbars since VideoPane manages its own scroll behavior
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

void MainWindow::onActionMouseAutoHideTriggered()
{
    GlobalVar::instance().setMouseAutoHide(true);
    GlobalSetting::instance().setMouseAutoHideEnable(true);
}

void MainWindow::onActionMouseAlwaysShowTriggered()
{
    GlobalVar::instance().setMouseAutoHide(false);
    GlobalSetting::instance().setMouseAutoHideEnable(false);
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

void MainWindow::onResolutionChange(const int& width, const int& height, const float& fps, const float& pixelClk)
{
    GlobalVar::instance().setInputWidth(width);
    GlobalVar::instance().setInputHeight(height);
    m_statusBarManager->setInputResolution(width, height, fps, pixelClk);
}

void MainWindow::onTargetUsbConnected(const bool isConnected)
{
    m_statusBarManager->setTargetUsbConnected(isConnected);
}

void MainWindow::onKeyStatesChanged(bool numLock, bool capsLock, bool scrollLock)
{
    qCDebug(log_ui_mainwindow) << "Key states changed - NumLock:" << numLock << "CapsLock:" << capsLock << "ScrollLock:" << scrollLock;
    m_statusBarManager->setKeyStates(numLock, capsLock, scrollLock);
}

void MainWindow::onActionPasteToTarget()
{
    HostManager::getInstance().pasteTextToTarget(QGuiApplication::clipboard()->text());
}

void MainWindow::onActionScreensaver()
{
    static bool isScreensaverActive = false;
    isScreensaverActive = !isScreensaverActive;

    if (isScreensaverActive) {
        HostManager::getInstance().startAutoMoveMouse();
        m_cornerWidgetManager->screensaverButton->setChecked(true);
        this->popupMessage("Screensaver activated");
    } else {
        HostManager::getInstance().stopAutoMoveMouse();
        m_cornerWidgetManager->screensaverButton->setChecked(false);
        this->popupMessage("Screensaver deactivated");
    }
}

void MainWindow::onToggleVirtualKeyboard()
{
    toolbarManager->toggleToolbar();    
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
    m_cameraManager->queryResolutions();
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

void MainWindow::configScreenScale(){
    if (!m_screenScaleDialog){
        qDebug() << "Creating screen scale dialog";
        m_screenScaleDialog = new ScreenScale(this);
        connect(m_screenScaleDialog, &QDialog::finished, this, [this](){
            m_screenScaleDialog->deleteLater();
            m_screenScaleDialog = nullptr;
        });
        connect(m_screenScaleDialog, &ScreenScale::screenRatio, this, &MainWindow::onScreenRatioChanged);
        m_screenScaleDialog->show();
    }else{
        m_screenScaleDialog->raise();
        m_screenScaleDialog->activateWindow();
    }
}

void MainWindow::onScreenRatioChanged(double ratio) {
    double currentRatio =  GlobalSetting::instance().getScreenRatio();
    if (ratio != currentRatio) {
        qCDebug(log_ui_mainwindow) << "Screen ratio changed to" << ratio;
        GlobalSetting::instance().setScreenRatio(ratio);
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->calculateVideoPosition();
        }
    }
}



void MainWindow::configureSettings() {
    qCDebug(log_ui_mainwindow) << "configureSettings";
    if (!settingDialog){
        qCDebug(log_ui_mainwindow)<< "Creating settings dialog";
        settingDialog = new SettingDialog(m_cameraManager, this);

        VideoPage* videoPage = settingDialog->getVideoPage();
        LogPage* logPage = settingDialog->getLogPage();
        connect(logPage, &LogPage::ScreenSaverInhibitedChanged, m_screenSaverManager, &ScreenSaverManager::setScreenSaverInhibited);
        connect(videoPage, &VideoPage::videoSettingsChanged, this, &MainWindow::onVideoSettingsChanged);
        // connect the finished signal to the set the dialog pointer to nullptr
        connect(settingDialog, &QDialog::finished, this, [this](){
            settingDialog->deleteLater();
            settingDialog = nullptr;
        });
        settingDialog->show();
    }else{
        settingDialog->raise();
        settingDialog->activateWindow();
    }
}

void MainWindow::showRecordingSettings() {
    qDebug() << "showRecordingSettings called";
    if (!recordingSettingsDialog) {
        qDebug() << "Creating recording settings dialog";
        recordingSettingsDialog = new RecordingSettingsDialog(this);
        
        // Get the current backend from camera manager and set it
        MultimediaBackendHandler* backendHandler = m_cameraManager->getBackendHandler();
        if (backendHandler) {
            // CRITICAL FIX: Ensure Qt backend has media recorder set before passing to dialog
            if (backendHandler->getBackendType() == MultimediaBackendType::Qt) {
                qDebug() << "Qt backend detected - ensuring media recorder is set";
                
                QMediaRecorder* mediaRecorder = m_cameraManager->getMediaRecorder();
                QMediaCaptureSession* captureSession = m_cameraManager->getCaptureSession();
                
                if (mediaRecorder && captureSession) {
                    qDebug() << "Setting media recorder on Qt backend:" << (void*)mediaRecorder;
                    qDebug() << "Setting capture session on Qt backend:" << (void*)captureSession;
                    
#ifdef Q_OS_WIN
                    if (auto qtHandler = qobject_cast<QtBackendHandler*>(backendHandler)) {
                        qtHandler->setMediaRecorder(mediaRecorder);
                        qtHandler->setCaptureSession(captureSession);
                        qDebug() << "Media recorder and capture session successfully set on Qt backend";
                    } else {
                        qWarning() << "Failed to cast to QtBackendHandler";
                    }
#else
                    qDebug() << "Qt backend configuration not available on non-Windows platforms";
#endif
                } else {
                    qWarning() << "Missing components - mediaRecorder:" << (void*)mediaRecorder 
                              << "captureSession:" << (void*)captureSession;
                }
            }
            
            recordingSettingsDialog->setBackendHandler(backendHandler);
            
#ifndef Q_OS_WIN
            // Also set FFmpeg backend specifically if it's available for backward compatibility
            FFmpegBackendHandler* ffmpegBackend = m_cameraManager->getFFmpegBackend();
            if (ffmpegBackend) {
                recordingSettingsDialog->setFFmpegBackend(ffmpegBackend);
            }
#endif
        } else {
            qWarning() << "No video backend available for recording";
        }
        
        // Connect the finished signal to clean up the dialog pointer
        connect(recordingSettingsDialog, &QDialog::finished, this, [this]() {
            if (recordingSettingsDialog) {
                recordingSettingsDialog->deleteLater();
                recordingSettingsDialog = nullptr;
            }
        });
        
        recordingSettingsDialog->showDialog();
    } else {
        recordingSettingsDialog->showDialog();
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
            serialPortDebugDialog->deleteLater();
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

void MainWindow::officialLink(){
    QDesktopServices::openUrl(QUrl("https://openterface.com/"));
}

void MainWindow::updateLink()
{
    m_versionInfoManager->checkForUpdates();
}

void MainWindow::aboutLink(){
    m_versionInfoManager->showAbout();
}

void MainWindow::versionInfo()
{
    m_versionInfoManager->showVersionInfo();
}

void MainWindow::onCtrlAltDelPressed()
{
    HostManager::getInstance().sendCtrlAltDel();
}

void MainWindow::onRepeatingKeystrokeChanged(int interval)
{
    HostManager::getInstance().setRepeatingKeystroke(interval);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        toolbarManager->updateStyles();
        m_statusBarManager->updateIconColor();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::record()
{
    m_cameraManager->startRecording();
}

void MainWindow::pause()
{
    m_cameraManager->stopRecording();
}

void MainWindow::setMuted(bool /*muted*/)
{
    // Your implementation here
}

void MainWindow::takeImageDefault(){
    takeImage("");
}

void MainWindow::takeImage(const QString& path)
{
    m_cameraManager->takeImage(path);
}

void MainWindow::takeAreaImage(const QString& path, const QRect& captureArea){
    qCDebug(log_ui_mainwindow) << "mainwindow capture area image";
    m_cameraManager->takeAreaImage(path, captureArea);
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
    if (!m_camera) {
        qCWarning(log_ui_mainwindow) << "Camera pointer is null in displayCameraError";
        return;
    }

    qCWarning(log_ui_mainwindow) << "Camera error: " << m_camera->errorString();
    if (m_camera->error() != QCamera::NoError) {
        qCDebug(log_ui_mainwindow) << "Camera error detected, switching to help pane";
        
        // Safely switch to help pane
        QMetaObject::invokeMethod(this, [this]() {
            stackedLayout->setCurrentIndex(0);
        }, Qt::QueuedConnection);

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

    m_cameraManager->stopCamera();

    SerialPortManager::getInstance().closePort();

    qDebug() << "Camera stopped.";
}

void MainWindow::displayViewfinder()
{
    //ui->stackedWidget->setCurrentIndex(0);
}

void MainWindow::displayCapturedImage()
{
    //ui->stackedWidget->setCurrentIndex(1);
}



void MainWindow::onArmBaudratePerformanceRecommendation(int currentBaudrate)
{
    // Delegate to MenuCoordinator
    if (m_menuCoordinator) {
        m_menuCoordinator->showArmBaudratePerformanceRecommendation(currentBaudrate);
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
    const QList<QCameraDevice> availableCameras = QMediaDevices::videoInputs();
    qCDebug(log_ui_mainwindow) << "Available cameras size: " << availableCameras.size();

    // Note: Automatic camera switching has been disabled
    // This method now only refreshes the available camera list for manual selection
    
    // Check for disconnected cameras and update the list
    if (!m_lastCameraList.isEmpty()) {
        qCDebug(log_ui_mainwindow) << "Checking previously connected cameras...";
        for (const QCameraDevice &camera : m_lastCameraList) {
            qCDebug(log_ui_mainwindow) << "Checking camera: " << camera.description();
            if (!availableCameras.contains(camera)) {
                qCDebug(log_ui_mainwindow) << "Camera disconnected: " << camera.description();
                // Note: We no longer automatically stop camera operations
                // The user will need to manually select a different camera if needed
            }
        }
    }
    
    // Update the camera list for reference but don't automatically switch
    qCDebug(log_ui_mainwindow) << "Updating camera device list...";
    for (const QCameraDevice &camera : availableCameras) {
        if (!m_lastCameraList.contains(camera)) {
            qCDebug(log_ui_mainwindow) << "New camera detected:" << camera.description();
            // Note: Camera will not automatically switch - manual selection required
        }
    }
    
    // Update the stored camera list
    m_lastCameraList = availableCameras;
    
    // Refresh CameraManager's available devices list
    m_cameraManager->refreshAvailableCameraDevices();
    
    qCDebug(log_ui_mainwindow) << "Camera list updated. Manual camera selection required for switching.";
}

void MainWindow::onPortConnected(const QString& port, const int& baudrate) {
    if(baudrate > 0){
        m_statusBarManager->setConnectedPort(port, baudrate);
        if (m_menuCoordinator) {
            m_menuCoordinator->updateBaudrateMenu(baudrate);
        }
        
        // Note: Camera coordination functionality has been removed
        // The DeviceManager singleton now handles device coordination automatically
        qCDebug(log_ui_mainwindow) << "Serial port connected:" << port << "at baudrate:" << baudrate;
    }else{
        m_statusBarManager->setConnectedPort(port, baudrate);
        m_statusBarManager->setTargetUsbConnected(false);
    }
}

void MainWindow::factoryReset(bool isStarted)
{
    m_statusBarManager->factoryReset(isStarted);
}

void MainWindow::serialPortReset(bool isStarted)
{
    m_statusBarManager->serialPortReset(isStarted);
}

void MainWindow::onSerialAutoRestart(int attemptNumber, int maxAttempts, double lossRate)
{
    m_statusBarManager->showSerialAutoRestart(attemptNumber, maxAttempts, lossRate);
}

void MainWindow::onStatusUpdate(const QString& status) {
    m_statusBarManager->setStatusUpdate(status);
}

void MainWindow::onLastKeyPressed(const QString& key) {
    m_statusBarManager->onLastKeyPressed(key);
}

void MainWindow::onLastMouseLocation(const QPoint& location, const QString& mouseEvent) {
    m_statusBarManager->onLastMouseLocation(location, mouseEvent);
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

void MainWindow::checkMousePosition()
{
    if (!videoPane) return;

    // Since VideoPane now handles its own scrolling via QGraphicsView,
    // we don't need to manually handle scroll area edge scrolling.
    // The VideoPane's built-in zoom and pan functionality will handle this.
    
    // This method can be simplified or removed entirely if no longer needed
    // for other mouse position tracking purposes.
}

void MainWindow::onVideoSettingsChanged() {
    if (m_cameraManager) {
        // Reinitialize camera with graphics video output to ensure proper connection
        bool success = m_cameraManager->initializeCameraWithVideoOutput(videoPane);
        if (!success) {
            // Fallback to just setting video output
            m_cameraManager->setVideoOutput(videoPane->getVideoItem());
        }
    }
    
    // Also reinitialize audio when video settings change
    if (m_audioManager) {
        qCDebug(log_ui_mainwindow) << "Reinitializing audio due to video settings change...";
        m_audioManager->initializeAudio();
    }
    int inputWidth = GlobalVar::instance().getInputWidth();
    int inputHeight = GlobalVar::instance().getInputHeight();
    int captureWidth = GlobalVar::instance().getCaptureWidth();
    int captureHeight = GlobalVar::instance().getCaptureHeight();
    QScreen *screen = this->screen();
    QRect availableGeometry = screen->availableGeometry();
    systemScaleFactor = screen->devicePixelRatio();

    // Calculate aspect ratios
    double inputAspectRatio = static_cast<double>(inputWidth) / inputHeight;
    double captureAspectRatio = static_cast<double>(captureWidth) / captureHeight;

    // Resize the window based on aspect ratios
    int newWidth, newHeight;
    if (inputAspectRatio != captureAspectRatio) {
        // Adjust the window size to hide black bars
        newWidth = static_cast<int>(captureHeight * inputAspectRatio);
        newHeight = captureHeight;
    }else{
        newWidth = captureWidth;
        newHeight = captureHeight;
    }
    
    if (captureHeight > availableGeometry.height()){
        newHeight = availableGeometry.height();
        newWidth = static_cast<int>(newHeight * inputAspectRatio);
    }
    
    if(systemScaleFactor!= 1){
        newWidth = static_cast<int>(newWidth / systemScaleFactor);
        newHeight = static_cast<int>(newHeight / systemScaleFactor); 
    }else{
        newWidth = static_cast<int>(newWidth / 1.2);
        newHeight = static_cast<int>(newHeight / 1.2);
    }
    qDebug() << "Resize to onVideoSettingsChanged " << captureWidth << newHeight;
    resize(newWidth, newHeight);

    // Optionally, you might want to center the window on the screen
    int x = (availableGeometry.width() - this->width()) / 2;
    int y = (availableGeometry.height() - this->height()) / 2;
    move(x, y);
    double screenRatio =  GlobalSetting::instance().getScreenRatio();
    if (captureAspectRatio != screenRatio) configScreenScale();
}

void MainWindow::onResolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps, float pixelClk)
{
    m_statusBarManager->setInputResolution(input_width, input_height, input_fps, pixelClk);
    m_statusBarManager->setCaptureResolution(capture_width, capture_height, capture_fps);

    video_height = GlobalVar::instance().getCaptureHeight();
    video_width = GlobalVar::instance().getCaptureWidth();
}

void MainWindow::onInputResolutionChanged()
{
    qCDebug(log_ui_mainwindow) << "Input resolution changed.";
    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->doResize();
    }

    // Calculate the maximum available content height with safety checks
    int contentHeight = this->height() - ui->statusbar->height() - ui->menubar->height();

    qDebug() << "contentHeight: " << contentHeight;
    
    // Set the videoPane to use the full available width and height
    // videoPane->setMinimumSize(videoPane->width(), contentHeight);
    videoPane->resize(videoPane->width(), contentHeight);
    
}

void MainWindow::showScriptTool()
{
    qDebug() << "showScriptTool called";  // Add debug output
    scriptTool->setAttribute(Qt::WA_DeleteOnClose);
    
    // Connect the syntaxTreeReady signal to the handleSyntaxTree slot
    connect(scriptTool, &ScriptTool::syntaxTreeReady, this, &MainWindow::handleSyntaxTree);
    
    scriptTool->show();  // Change exec() to show() for non-modal dialog
}

// run the sematic analyzer
void MainWindow::handleSyntaxTree(std::shared_ptr<ASTNode> syntaxTree) {
    QPointer<QObject> senderObj = sender();
    QPointer<MainWindow> thisPtr(this); // Add protection for this pointer
    taskmanager->addTask([thisPtr, syntaxTree, senderObj]() {
        if (!senderObj || !thisPtr) return; // Check both pointers
        bool runStatus = thisPtr->semanticAnalyzer->analyze(syntaxTree.get());
        qCDebug(log_ui_mainwindow) << "Script run status: " << runStatus;
        emit thisPtr->emitScriptStatus(runStatus);
        
        if (senderObj == thisPtr->tcpServer) {
            qCDebug(log_ui_mainwindow) << "run finish: " << runStatus;
            emit thisPtr->emitTCPCommandStatus(runStatus);
        }
    });
} 

MainWindow::~MainWindow()
{
    qCDebug(log_ui_mainwindow) << "MainWindow destructor called";
    
    // Set global shutdown flag to prevent Qt Multimedia operations
    g_applicationShuttingDown.storeRelease(1);
    
    // 0. CRITICAL: Stop any running animations before cleanup
    QList<QPropertyAnimation*> animations = this->findChildren<QPropertyAnimation*>();
    for (QPropertyAnimation* animation : animations) {
        animation->stop();
        animation->deleteLater();
    }
    
    QList<QParallelAnimationGroup*> animationGroups = this->findChildren<QParallelAnimationGroup*>();
    for (QParallelAnimationGroup* group : animationGroups) {
        group->stop();
        group->deleteLater();
    }
    
    // Process any pending events to ensure cleanup
    QCoreApplication::processEvents();
    
    // 1. Stop all operations first
    stop();
    
    // 1.5. CRITICAL: Stop audio first before anything else to prevent segfault
    if (m_audioManager) {
        m_audioManager->disconnect();
        m_audioManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_audioManager disconnected and cleared successfully";
    }
    
    // Also ensure singleton audio manager is stopped (but only if not already stopped)
    static bool audioManagerStopped = false;
    if (!audioManagerStopped) {
        AudioManager::getInstance().stop();
        audioManagerStopped = true;
        qCDebug(log_ui_mainwindow) << "AudioManager singleton stopped";
    }
    
    // Process any pending events after audio cleanup
    QCoreApplication::processEvents();
    
    // Wait a moment to ensure audio threads are fully stopped
    QThread::msleep(50);
    
    // 2. Stop camera operations and disconnect signals
    if (m_cameraManager) {
        disconnect(m_cameraManager);
        m_cameraManager->stopCamera();
        m_cameraManager->deleteLater();
        m_cameraManager = nullptr;
    }
    
    // 3. Clean up managers in dependency order
    if (m_versionInfoManager) {
        m_versionInfoManager->deleteLater();
        m_versionInfoManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_versionInfoManager destroyed successfully";
    }
    
    if (m_screenSaverManager) {
        m_screenSaverManager->deleteLater();
        m_screenSaverManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_screenSaverManager destroyed successfully";
    }
    
    if (m_cornerWidgetManager) {
        m_cornerWidgetManager->deleteLater();
        m_cornerWidgetManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_cornerWidgetManager destroyed successfully";
    }

    if (m_windowControlManager) {
        m_windowControlManager->setAutoHideEnabled(false); // Disable before cleanup
        m_windowControlManager->deleteLater();
        m_windowControlManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_windowControlManager destroyed successfully";
    }

    if (m_screenScaleDialog) {
        m_screenScaleDialog->deleteLater();
        m_screenScaleDialog = nullptr;
        qCDebug(log_ui_mainwindow) << "m_screenScaleDialog destroyed successfully";
    }

    if (firmwareManagerDialog) {
        firmwareManagerDialog->deleteLater();
        firmwareManagerDialog = nullptr;
        qCDebug(log_ui_mainwindow) << "firmwareManagerDialog destroyed successfully";
    }
    
    // 4. Clean up video pane and related objects - Use direct delete to ensure immediate cleanup
    if (videoPane) {
        // Remove videoPane from any layouts first
        if (stackedLayout) {
            stackedLayout->removeWidget(videoPane);
        }
        delete videoPane; // Direct delete instead of deleteLater
        videoPane = nullptr;
        qCDebug(log_ui_mainwindow) << "videoPane destroyed successfully";
    }
    
    if (stackedLayout) {
        stackedLayout->deleteLater();
        stackedLayout = nullptr;
    }
    
    // 5. Clean up other components
    if (mouseEdgeTimer) {
        mouseEdgeTimer->stop();
        mouseEdgeTimer->deleteLater();
        mouseEdgeTimer = nullptr;
        qCDebug(log_ui_mainwindow) << "mouseEdgeTimer destroyed successfully";
    }
    
    if (toolbarManager) {
        toolbarManager->deleteLater();
        toolbarManager = nullptr;
        qCDebug(log_ui_mainwindow) << "toolbarManager destroyed successfully";
    }
    
    if (toggleSwitch) {
        toggleSwitch->deleteLater();
        toggleSwitch = nullptr;
        qCDebug(log_ui_mainwindow) << "toggleSwitch destroyed successfully";
    }
    
    if (m_audioManager) {
        // AudioManager is now a singleton, and we already disconnected it above
        // Just clear the reference here
        m_audioManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_audioManager reference cleared successfully (already disconnected)";
    }
    
    // 6. Clean up static instances (but skip AudioManager since it's already stopped)
    VideoHid::getInstance().stop();
    // AudioManager::getInstance().stop(); // Already stopped above to prevent double cleanup
    SerialPortManager::getInstance().stop();
    
    // 7. Delete UI last
    if (ui) {
        delete ui;
        ui = nullptr;
    }

    qCDebug(log_ui_mainwindow) << "MainWindow destroyed successfully";
}

void MainWindow::onToolbarVisibilityChanged(bool visible) {
    Q_UNUSED(visible);
    // Prevent repaints during animation
    setUpdatesEnabled(false);
    
    // Block signals during update to prevent recursive calls
    blockSignals(true);
    
    // Update icon
    bool isVisible = toolbarManager->getToolbar()->isVisible();
    QString iconPath = isVisible ? ":/images/keyboard-down.svg" : ":/images/keyboard-up.svg";
    // ui->virtualKeyboardButton->setIcon(QIcon(iconPath));  // Create QIcon from the path

    

    // Use QTimer to delay the video pane repositioning
    // Safety check: Don't schedule animation if window is being destroyed
    if (m_windowLayoutCoordinator && videoPane && this->isVisible() && !this->testAttribute(Qt::WA_DeleteOnClose)) {
        QTimer::singleShot(0, [this]() {
            if (m_windowLayoutCoordinator) {
                m_windowLayoutCoordinator->animateVideoPane();
            }
        });
    }
    
}



void MainWindow::changeKeyboardLayout(const QString& layout) {
    // Pass the layout name directly to HostManager
    qCDebug(log_ui_mainwindow) << "Changing layout";
    GlobalSetting::instance().setKeyboardLayout(layout);
    qCDebug(log_ui_mainwindow) << "Set layout" << layout;
    HostManager::getInstance().setKeyboardLayout(layout);
}

void MainWindow::onKeyboardLayoutCombobox_Changed(const QString &layout) {
    changeKeyboardLayout(layout);
}

void MainWindow::initializeKeyboardLayouts() {
    QStringList layouts = KeyboardLayoutManager::getInstance().getAvailableLayouts();
    qCDebug(log_ui_mainwindow) << "Available layouts:" << layouts;
    
    QString defaultLayout;
    GlobalSetting::instance().getKeyboardLayout(defaultLayout);
    qCDebug(log_ui_mainwindow) << "Read layout" << defaultLayout;

    m_cornerWidgetManager->initializeKeyboardLayouts(layouts, defaultLayout);
    if (layouts.contains(defaultLayout)) {
        changeKeyboardLayout(defaultLayout);
    } else if (!layouts.isEmpty()) {
        changeKeyboardLayout(layouts.first());
    }
}

void MainWindow::showEnvironmentSetupDialog() {
    qCDebug(log_ui_mainwindow) << "Show EnvironmentSetupDialog";
    EnvironmentSetupDialog dialog(this);
    dialog.exec();
}

void MainWindow::showFirmwareManagerDialog() {
    if (!firmwareManagerDialog){
        qDebug() << "Creating serial port debug dialog";
        firmwareManagerDialog = new FirmwareManagerDialog(this);
        // connect the finished signal to the set the dialog pointer to nullptr
        connect(firmwareManagerDialog, &QDialog::finished, this, [this](){
            firmwareManagerDialog->deleteLater();
            firmwareManagerDialog = nullptr;
        });
        firmwareManagerDialog->show();
    }else{
        firmwareManagerDialog->raise();
        firmwareManagerDialog->activateWindow();
    }

}

void MainWindow::updateFirmware() {
    // Check if it's latest firmware
    qDebug() << "Checking for latest firmware version...";
    FirmwareResult firmwareStatus = VideoHid::getInstance().isLatestFirmware();
    std::string currentFirmwareVersion = VideoHid::getInstance().getCurrentFirmwareVersion();
    std::string latestFirmwareVersion = VideoHid::getInstance().getLatestFirmwareVersion();
    qDebug() << "latestFirmwareVersion" << latestFirmwareVersion.c_str();
    FirmwareUpdateConfirmDialog confirmDialog(this);
    bool proceed = false;
    switch (firmwareStatus){
        case FirmwareResult::Latest:
            qDebug() << "Firmware is up to date.";
            QMessageBox::information(this, tr("Firmware Update"), 
            tr("The firmware is up to date.\nCurrent version: ") + 
            QString::fromStdString(currentFirmwareVersion));
            break;
        case FirmwareResult::Upgradable:
            qDebug() << "Firmware is upgradable.";
            proceed = confirmDialog.showConfirmDialog(currentFirmwareVersion, latestFirmwareVersion);
            if (proceed) {
                // Stop video and HID operations before firmware update
                VideoHid::getInstance().stop();
                SerialPortManager::getInstance().stop();
                stop();
                
                close();
                // Create and show firmware update dialog
                FirmwareUpdateDialog *updateDialog = new FirmwareUpdateDialog(this);
                updateDialog->startUpdate();
                // The application will be closed by the dialog if the update is successful
                updateDialog->deleteLater();
            }
            break;
        case FirmwareResult::Timeout:
            qDebug() << "Firmware fetch timeout.";
            QMessageBox::information(this, tr("Firmware fetch timeout"), 
            tr("Firmware retrieval timed out. Please check your network connection and try again.\nCurrent version: ") + 
            QString::fromStdString(currentFirmwareVersion));
            break;
    }
}

void MainWindow::openDeviceSelector() {
    qDebug() << "Opening device selector dialog";
    if (!deviceSelectorDialog) {
        qDebug() << "Creating device selector dialog";
        deviceSelectorDialog = new DeviceSelectorDialog(m_cameraManager, &VideoHid::getInstance(), this);
        
        // Connect the finished signal to clean up
        connect(deviceSelectorDialog, &QDialog::finished, this, [this]() {
            deviceSelectorDialog->deleteLater();
            deviceSelectorDialog = nullptr;
        });
        
        deviceSelectorDialog->show();
    } else {
        deviceSelectorDialog->raise();
        deviceSelectorDialog->activateWindow();
    }
}

void MainWindow::showUpdateDisplaySettingsDialog() {
    qCDebug(log_ui_mainwindow) << "Opening update display settings dialog";
    if (!updateDisplaySettingsDialog) {
        qCDebug(log_ui_mainwindow) << "Creating update display settings dialog";
        updateDisplaySettingsDialog = new UpdateDisplaySettingsDialog(this);
        
        // Connect the finished signal to clean up
        connect(updateDisplaySettingsDialog, &QDialog::finished, this, [this]() {
            updateDisplaySettingsDialog->deleteLater();
            updateDisplaySettingsDialog = nullptr;
        });
        
        updateDisplaySettingsDialog->show();
    } else {
        updateDisplaySettingsDialog->raise();
        updateDisplaySettingsDialog->activateWindow();
    }
}
