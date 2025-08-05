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
                            m_audioManager(new AudioManager(this)),
                            videoPane(new VideoPane(this)),
                            scrollArea(new QScrollArea(this)),
                            stackedLayout(new QStackedLayout(this)),
                            toolbarManager(new ToolbarManager(this)),
                            toggleSwitch(new ToggleSwitch(this)),
                            m_cameraManager(new CameraManager(this)),
                            m_versionInfoManager(new VersionInfoManager(this)),
                            m_languageManager(languageManager),
                            m_screenSaverManager(new ScreenSaverManager(this)),
                            m_cornerWidgetManager(new CornerWidgetManager(this))
                            // cameraAdjust(new CameraAdjust(this))
{
    Q_UNUSED(parent);

    qCDebug(log_ui_mainwindow) << "Init camera...";
    
    ui->setupUi(this);
    m_cornerWidgetManager->setMenuBar(ui->menubar);
    // ui->menubar->setCornerWidget(m_cornerWidgetManager->getCornerWidget(), Qt::TopRightCorner);

    initializeKeyboardLayouts();
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomInClicked, this, &MainWindow::onZoomIn);
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomOutClicked, this, &MainWindow::onZoomOut);
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomReductionClicked, this, &MainWindow::onZoomReduction);
    connect(m_cornerWidgetManager, &CornerWidgetManager::screenScaleClicked, this, &MainWindow::configScreenScale);
    connect(m_cornerWidgetManager, &CornerWidgetManager::virtualKeyboardClicked, this, &MainWindow::onToggleVirtualKeyboard);
    connect(m_cornerWidgetManager, &CornerWidgetManager::captureClicked, this, &MainWindow::takeImageDefault);
    connect(m_cornerWidgetManager, &CornerWidgetManager::fullScreenClicked, this, &MainWindow::fullScreen);
    connect(m_cornerWidgetManager, &CornerWidgetManager::pasteClicked, this, &MainWindow::onActionPasteToTarget);
    connect(m_cornerWidgetManager, &CornerWidgetManager::screensaverClicked, this, &MainWindow::onActionScreensaver);
    connect(m_cornerWidgetManager, &CornerWidgetManager::toggleSwitchChanged, this, &MainWindow::onToggleSwitchStateChanged);
    connect(m_cornerWidgetManager, &CornerWidgetManager::keyboardLayoutChanged, this, &MainWindow::onKeyboardLayoutCombobox_Changed);
    

    GlobalVar::instance().setMouseAutoHide(GlobalSetting::instance().getMouseAutoHideEnable());

    m_statusBarManager = new StatusBarManager(ui->statusbar, this);
    taskmanager = TaskManager::instance();
    
    // Connect DeviceManager hotplug events to StatusBarManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    if (hotplugMonitor) {
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn, 
                m_statusBarManager, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindow) << "MainWindow: Received newDevicePluggedIn signal for port:" << device.portChain;
                    m_statusBarManager->showNewDevicePluggedIn(device.portChain);
                    updateDeviceMenu(); // Update device menu when new device is plugged in
                });
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged, 
                m_statusBarManager, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindow) << "MainWindow: Received deviceUnplugged signal for port:" << device.portChain;
                    m_statusBarManager->showDeviceUnplugged(device.portChain);
                    updateDeviceMenu(); // Update device menu when device is unplugged
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

    setCentralWidget(centralWidget);
    qCDebug(log_ui_mainwindow) << "Set host manager event callback...";
    HostManager::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe Video HID connected...";
    VideoHid::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe video input changed...";
    // Note: Automatic camera switching on device changes has been disabled
    // Camera devices will only be switched manually through the UI
    // connect(&m_source, &QMediaDevices::videoInputsChanged, this, &MainWindow::updateCameras);

    qCDebug(log_ui_mainwindow) << "Observe Relative/Absolute toggle...";
    connect(ui->actionRelative, &QAction::triggered, this, &MainWindow::onActionRelativeTriggered);
    connect(ui->actionAbsolute, &QAction::triggered, this, &MainWindow::onActionAbsoluteTriggered);

    connect(ui->actionMouseAutoHide, &QAction::triggered, this, &MainWindow::onActionMouseAutoHideTriggered);
    connect(ui->actionMouseAlwaysShow, &QAction::triggered, this, &MainWindow::onActionMouseAlwaysShowTriggered);

    qCDebug(log_ui_mainwindow) << "Observe reset HID triggered...";
    connect(ui->actionResetHID, &QAction::triggered, this, &MainWindow::onActionResetHIDTriggered);

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
    connect(&VideoHid::getInstance(), &VideoHid::inputResolutionChanged, this, &MainWindow::onInputResolutionChanged);
    connect(&VideoHid::getInstance(), &VideoHid::resolutionChangeUpdate, this, &MainWindow::onResolutionChange);

    
    qCDebug(log_ui_mainwindow) << "Test actionTCPServer true...";
    ui->actionTCPServer->setVisible(true);
    connect(ui->actionTCPServer, &QAction::triggered, this, &MainWindow::startServer);

    qDebug() << "Init camera...";
    checkInitSize();
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

    // Connect palette change signal to the slot
    onLastKeyPressed("");
    onLastMouseLocation(QPoint(0, 0), "");

    // Connect zoom buttons
    
    scrollArea->ensureWidgetVisible(videoPane);

    // Set the window title with the version number
    qDebug() << "Set window title" << APP_VERSION;
    QString windowTitle = QString("Openterface Mini-KVM - %1").arg(APP_VERSION);
    setWindowTitle(windowTitle);

    mouseEdgeTimer = new QTimer(this);
    connect(mouseEdgeTimer, &QTimer::timeout, this, &MainWindow::checkMousePosition);
    // mouseEdgeTimer->start(edgeDuration); // Start the timer with the new duration



    // Add this after other menu connections
    connect(ui->menuBaudrate, &QMenu::triggered, this, &MainWindow::onBaudrateMenuTriggered);
    connect(ui->menuDevice, &QMenu::triggered, this, &MainWindow::onDeviceSelected);
    connect(&SerialPortManager::getInstance(), &SerialPortManager::connectedPortChanged, this, &MainWindow::onPortConnected);
    
    // Note: Automatic camera device coordination has been disabled
    // Camera devices will only be switched manually through the UI
    
    qApp->installEventFilter(this);

    // Initial position setup
    // QPoint buttonPos = ui->contrastButton->mapToGlobal(QPoint(0, 0));
    // int menuBarHeight = buttonPos.y() - this->mapToGlobal(QPoint(0, 0)).y();
    // cameraAdjust->updatePosition(menuBarHeight, width());

    // Add this line after ui->setupUi(this)
    connect(ui->actionScriptTool, &QAction::triggered, this, &MainWindow::showScriptTool);
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
    setupLanguageMenu();
    setupDeviceMenu();
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
    setupLanguageMenu();
    updateDeviceMenu(); // Update device menu when UI language changes
}

void MainWindow::setupLanguageMenu() {
    // Clear existing language actions
    ui->menuLanguages->clear();
    QStringList languages = m_languageManager->availableLanguages();
    for (const QString &lang : languages) {
       qCDebug(log_ui_mainwindow) << "Available language: " << lang; 
    }
    if (languages.isEmpty()) {
        languages << "en" << "fr" << "de" << "da" << "ja" << "se";
    }

    QActionGroup *languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);

    QMap<QString, QString> languageNames = {
        {"en", "English"},
        {"fr", "Français"},
        {"de", "German"},
        {"da", "Danish"},
        {"ja", "Japanese"},
        {"se", "Swedish"}
    };
    for (const QString &lang : languages) {
        QString displayName = languageNames.value(lang, lang);
        QAction *action = new QAction(displayName, this);
        action->setCheckable(true);
        action->setData(lang);
        if (lang == m_languageManager->currentLanguage()) {
            action->setChecked(true);
        }
        ui->menuLanguages->addAction(action);
        languageGroup->addAction(action);
    }
    connect(languageGroup, &QActionGroup::triggered, this, &MainWindow::onLanguageSelected);
}

void MainWindow::setupDeviceMenu() {
    // Initialize device menu group
    m_deviceMenuGroup = new QActionGroup(this);
    m_deviceMenuGroup->setExclusive(true);
    
    // Connect to device menu group
    connect(m_deviceMenuGroup, &QActionGroup::triggered, this, &MainWindow::onDeviceSelected);
    
    // Initial population of device menu
    updateDeviceMenu();
}

void MainWindow::updateDeviceMenu() {
    if (!m_deviceMenuGroup) {
        return;
    }
    
    // Clear existing device actions
    ui->menuDevice->clear();
    qDeleteAll(m_deviceMenuGroup->actions());
    
    // Get available devices from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.discoverDevices(); // Force discovery for up-to-date list
    
    // Get currently selected device port chain
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    qCDebug(log_ui_mainwindow) << "Updating device menu with" << devices.size() << "devices. Current port chain:" << currentPortChain;
    
    if (devices.isEmpty()) {
        // Add "No devices available" placeholder
        QAction *noDevicesAction = new QAction("No devices available", this);
        noDevicesAction->setEnabled(false);
        ui->menuDevice->addAction(noDevicesAction);
        return;
    }
    
    // Deduplicate devices by port chain (similar to DeviceSelectorDialog)
    QMap<QString, DeviceInfo> uniqueDevicesByPortChain;
    for (const auto& device : devices) {
        if (!device.portChain.isEmpty()) {
            if (uniqueDevicesByPortChain.contains(device.portChain)) {
                const DeviceInfo& existing = uniqueDevicesByPortChain[device.portChain];
                // Choose the device with more interfaces
                if (device.getInterfaceCount() > existing.getInterfaceCount()) {
                    uniqueDevicesByPortChain[device.portChain] = device;
                }
            } else {
                uniqueDevicesByPortChain[device.portChain] = device;
            }
        }
    }
    
    // Add action for each unique device
    for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
        const DeviceInfo& device = it.value();
        QString displayText = QString("Port %1").arg(device.portChain);
        QAction *deviceAction = new QAction(displayText, this);
        deviceAction->setCheckable(true);
        deviceAction->setData(device.portChain);
        
        // Mark current device with a dot
        if (device.portChain == currentPortChain) {
            deviceAction->setChecked(true);
            deviceAction->setText(QString("• %1").arg(displayText));
        }
        
        ui->menuDevice->addAction(deviceAction);
        m_deviceMenuGroup->addAction(deviceAction);
    }
}

void MainWindow::onDeviceSelected(QAction *action) {
    QString portChain = action->data().toString();
    qCDebug(log_ui_mainwindow) << "Device selected from menu:" << portChain;
    
    if (portChain.isEmpty()) {
        return;
    }
    
    // Use the centralized device switching function
    DeviceManager& deviceManager = DeviceManager::getInstance();
    auto result = deviceManager.switchToDeviceByPortChainWithCamera(portChain, m_cameraManager);
    
    // Log the result
    if (result.success) {
        qCInfo(log_ui_mainwindow) << "✓ Device switch successful:" << result.statusMessage;
    } else {
        qCWarning(log_ui_mainwindow) << "Device switch failed or partial:" << result.statusMessage;
    }
    
    // Update device menu to reflect current selection
    updateDeviceMenu();
}

void MainWindow::onLanguageSelected(QAction *action) {
    QString language = action->data().toString();
    m_languageManager->switchLanguage(language);
}

bool MainWindow::isFullScreenMode() {
    // return fullScreenState;
    return this->isFullScreen();
}

void MainWindow::fullScreen(){
    qreal aspect_ratio = static_cast<qreal>(video_width) / video_height;
    QScreen *currentScreen = this->screen();
    QRect screenGeometry = currentScreen->geometry();
    int videoAvailibleHeight = screenGeometry.height() - ui->menubar->height();
    int videoAvailibleWidth = videoAvailibleHeight * aspect_ratio;
    int horizontalOffset = (screenGeometry.width() - videoAvailibleWidth) / 2;
    if(!isFullScreenMode()){
         
        ui->statusbar->hide();
        // Calculate the horizontal offset after resizing
        
        // Resize the videoPane and scrollArea first
        videoPane->setMinimumSize(videoAvailibleWidth, videoAvailibleHeight);
        videoPane->resize(videoAvailibleWidth, videoAvailibleHeight);
        scrollArea->resize(videoAvailibleWidth, videoAvailibleHeight);
        qCDebug(log_ui_mainwindow) << "Resize to Width " << videoAvailibleWidth << "\tHeight: " << videoAvailibleHeight;
        // Move the videoPane and scrollArea to the center
        fullScreenState = true;
        this->showFullScreen();
        qCDebug(log_ui_mainwindow) << "offset: " << horizontalOffset;
        videoPane->move(horizontalOffset, videoPane->y());
        scrollArea->move(horizontalOffset, videoPane->y());
    } else {
        this->showNormal();
        ui->statusbar->show();
        fullScreenState = false;
    }
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

    mouseEdgeTimer->start(edgeDuration); // Check every edge Duration
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
    if (mouseEdgeTimer->isActive()) {
        mouseEdgeTimer->stop();
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
    calculate_video_position();
    GlobalVar::instance().setWinWidth(this->width());
    GlobalVar::instance().setWinHeight(this->height());
}

void MainWindow::checkInitSize(){
    QScreen *currentScreen = this->screen();
    systemScaleFactor = currentScreen->devicePixelRatio();
    
    // Get screen geometry
    QRect screenGeometry = currentScreen->geometry();
    int screenWidth = screenGeometry.width();
    int screenHeight = screenGeometry.height();
    
    // Calculate 50% of screen size
    int windowWidth = screenWidth / 2;
    int windowHeight = screenHeight / 2;
    
    // Set window size to 50% of screen size
    resize(windowWidth, windowHeight);
    
    // Center the window on screen
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;
    move(x, y);
    
    qCDebug(log_ui_mainwindow) << "checkInitSize: Screen size:" << screenWidth << "x" << screenHeight;
    qCDebug(log_ui_mainwindow) << "checkInitSize: Window size set to:" << windowWidth << "x" << windowHeight;
    qCDebug(log_ui_mainwindow) << "checkInitSize: Window position:" << x << "," << y;
    qCDebug(log_ui_mainwindow) << "System scale factor: " << systemScaleFactor;
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    qCDebug(log_ui_mainwindow) << "Resize event triggered. New size:" << event->size();

    static qint64 lastResizeTime = 0;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    if (isFullScreenMode() || (currentTime - lastResizeTime) < 50) { // 100ms
        return;
    }

    // Get the available screen width and height
    QScreen *currentScreen = this->screen();
    QRect availableGeometry = currentScreen->availableGeometry();
    int availableWidth = availableGeometry.width();
    int availableHeight = availableGeometry.height();
    if (event->size().width() >= availableWidth || event->size().height() >= availableHeight) {
        qCDebug(log_ui_mainwindow) << "Resize event ignored due to exceeding screen bounds.";
        return;
    }
    
    lastResizeTime = currentTime;
    QMainWindow::resizeEvent(event);
    doResize();
    
    // m_cornerWidgetManager->updateButtonVisibility(this->width());
    m_cornerWidgetManager->updatePosition(this->width(), ui->menubar->height(), isFullScreenMode());

} // end resize event function

void MainWindow::doResize(){
    // Check if the window is maximized
    if (this->windowState() & Qt::WindowMaximized) {
        // Handle maximized state
        qCDebug(log_ui_mainwindow) << "Window is maximized.";
        // You can update the window icon here if needed
    } else {
        // Handle normal state
        qCDebug(log_ui_mainwindow) << "Window is normal.";
        // You can update the window icon here if needed
    }
    // Define the desired aspect ratio
    QScreen *currentScreen = this->screen();
    QRect availableGeometry = currentScreen->availableGeometry();
    systemScaleFactor = currentScreen->devicePixelRatio();
    double captureAspectRatio;
    if(GlobalVar::instance().getCaptureWidth() && GlobalVar::instance().getCaptureHeight()){
        video_width = GlobalVar::instance().getCaptureWidth();
        video_height = GlobalVar::instance().getCaptureHeight();
        captureAspectRatio = static_cast<double>(video_width) / video_height;
    }
    double aspect_ratio = GlobalSetting::instance().getScreenRatio();
    
    // Get the available screen width and height
    int availableWidth = availableGeometry.width();
    int availableHeight = availableGeometry.height();
    // Get the current window size
    int currentHeight = this->height();
    int currentWidth = this->width();
    
    // Calculate the height of the title bar, menu bar, and status bar
    int titleBarHeight = this->frameGeometry().height() - this->geometry().height();
    int menuBarHeight = this->menuBar()->height();
    int statusBarHeight = ui->statusbar->height();
    int maxContentHeight = availableHeight - titleBarHeight - menuBarHeight - statusBarHeight;
    bool needResize = (currentWidth >= availableWidth || currentHeight >= availableHeight);
    if (needResize) {
        // Adjust size while maintaining aspect ratio
        if (currentWidth >= availableWidth) {
            currentWidth = availableWidth;
        }
        if (currentHeight >= maxContentHeight) {
            currentHeight = std::min(maxContentHeight + menuBarHeight + statusBarHeight, availableHeight);
        }

        int newVideoHeight = std::min(currentHeight - menuBarHeight - statusBarHeight, maxContentHeight);
        int newVideoWidth = static_cast<int>(newVideoHeight * aspect_ratio);

        if (currentWidth < newVideoWidth) {
            // If video width is larger than the window's width, adjust based on width
            newVideoWidth = currentWidth;
            newVideoHeight = static_cast<int>(newVideoWidth / aspect_ratio);
        }

        // Calculate horizontal offset for centering
        int horizontalOffset = (currentWidth - newVideoWidth) / 2;

        // Apply changes to UI components
        videoPane->setMinimumSize(newVideoWidth, newVideoHeight);
        videoPane->resize(newVideoWidth, newVideoHeight);
        scrollArea->resize(newVideoWidth, newVideoHeight);
        videoPane->move(horizontalOffset, videoPane->y());
        scrollArea->move(horizontalOffset, videoPane->y());
        
        // Resize main window if necessary
        if (currentWidth != availableWidth && currentHeight != availableHeight) {
            qCDebug(log_ui_mainwindow) << "Resize to Width " << currentWidth << "\tHeight: " << currentHeight << ", due to exceeding screen bounds.";
            qCDebug(log_ui_mainwindow) << "Available Width " << availableWidth << "\tHeight: " << availableHeight;
            resize(currentWidth, currentHeight);
        }
 
    } else {
        // When within screen bounds, adjust height according to width and aspect ratio
        int contentHeight = static_cast<int>(currentWidth / aspect_ratio) + menuBarHeight + statusBarHeight;
        int adjustedContentHeight = contentHeight - menuBarHeight - statusBarHeight;
        if (aspect_ratio < 1.0){
            currentWidth = static_cast<int>(currentHeight * aspect_ratio);
            adjustedContentHeight = currentHeight - menuBarHeight - statusBarHeight;
            int offsetX = static_cast<int>((videoPane->width()-currentWidth) /2);
            int offsetY = static_cast<int>((videoPane->height()-adjustedContentHeight) /2);
            int contentwidth = static_cast<int>(adjustedContentHeight * captureAspectRatio);
            videoPane->setMinimumSize(contentwidth, adjustedContentHeight);
            videoPane->resize(contentwidth, adjustedContentHeight);
            qDebug() << "setDisplayRegion Resize videoPane to width: " << currentWidth << " height: " << currentHeight << " offset: " << offsetX << offsetY << "videoPane width: " << videoPane->width();
            setMinimumSize(100, 500);
            qCDebug(log_ui_mainwindow) << "Resize to Width " << currentWidth << "\tHeight: " << currentHeight << ", due to aspect ratio < 1.0.";
            resize(currentWidth, currentHeight);
        }
        else{
            videoPane->setMinimumSize(currentWidth, adjustedContentHeight);
            videoPane->resize(currentWidth, adjustedContentHeight);
            scrollArea->resize(currentWidth, adjustedContentHeight);
            qCDebug(log_ui_mainwindow) << "Resize to Width " << currentWidth << "\tHeight: " << currentHeight << ", due to aspect ratio >= 1.0.";
            resize(currentWidth, contentHeight);
        }
        
    }
    // Update global state
    GlobalVar::instance().setWinWidth(this->width());
    GlobalVar::instance().setWinHeight(this->height());
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
        calculate_video_position(); 
    }
}

void MainWindow::calculate_video_position(){
    qCDebug(log_ui_mainwindow) << "Calculate video position...";
    double currentRatio = GlobalSetting::instance().getScreenRatio();
    double input_aspect_ratio = double(GlobalVar::instance().getCaptureWidth()) / double(GlobalVar::instance().getCaptureHeight());
    
    if(currentRatio > input_aspect_ratio){
        currentRatioType = ratioType::LARGER;
    }else if (currentRatio < input_aspect_ratio){
        currentRatioType = ratioType::SMALLER;
    }else if (currentRatio == input_aspect_ratio){
        currentRatioType = ratioType::EQUAL;
    }
    doResize();
    QScreen *screen = this->screen();
    QRect availableGeometry = screen->availableGeometry();
    int x = (availableGeometry.width() - this->width()) / 2;
    int y = (availableGeometry.height() - this->height()) / 2;
    move(x, y);
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
        updateBaudrateMenu(baudrate);
        
        // Note: Camera coordination functionality has been removed
        // The DeviceManager singleton now handles device coordination automatically
        qCDebug(log_ui_mainwindow) << "Serial port connected:" << port << "at baudrate:" << baudrate;
    }else{
        m_statusBarManager->setConnectedPort(port, baudrate);
        m_statusBarManager->setTargetUsbConnected(false);
    }
}

void MainWindow::updateBaudrateMenu(int baudrate){
    QMenu* baudrateMenu = ui->menuBaudrate;
    if (baudrateMenu) {
        QList<QAction*> actions = baudrateMenu->actions();
        for (QAction* action : actions) {
            if (baudrate == 0) {
                action->setChecked(false);
            } else {
                bool ok;
                int actionBaudrate = action->text().toInt(&ok);
                if (ok && actionBaudrate == baudrate) {
                    action->setChecked(true);
                } else {
                    action->setChecked(false);
                }
            }
        }
    }
    else {
        qWarning() << "Baudrate menu not found!";
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
    if (!scrollArea || !videoPane) return;

    QPoint mousePos = mapFromGlobal(QCursor::pos());
    QRect viewRect = scrollArea->viewport()->rect();

    int deltaX = 0;
    int deltaY = 0;

    // Calculate the distance from the edge
    int leftDistance = mousePos.x() - viewRect.left();
    int rightDistance = viewRect.right() - mousePos.x();
    int topDistance = mousePos.y() - viewRect.top();
    int bottomDistance = viewRect.bottom() - mousePos.y();

    // Adjust the scroll speed based on the distance from the edge
    if (leftDistance <= edgeThreshold) {
        deltaX = -maxScrollSpeed * (edgeThreshold - leftDistance) / edgeThreshold;
    } else if (rightDistance <= edgeThreshold) {
        deltaX = maxScrollSpeed * (edgeThreshold - rightDistance) / edgeThreshold;
    }

    if (topDistance <= edgeThreshold) {
        deltaY = -maxScrollSpeed * (edgeThreshold - topDistance) / edgeThreshold;
    } else if (bottomDistance <= edgeThreshold) {
        deltaY = maxScrollSpeed * (edgeThreshold - bottomDistance) / edgeThreshold;
    }

    if (deltaX != 0 || deltaY != 0) {
        scrollArea->horizontalScrollBar()->setValue(scrollArea->horizontalScrollBar()->value() + deltaX);
        scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->value() + deltaY);
    }
}

void MainWindow::onVideoSettingsChanged() {
    if (m_cameraManager) {
        // Reinitialize camera with video output to ensure proper connection
        bool success = m_cameraManager->initializeCameraWithVideoOutput(videoPane);
        if (!success) {
            // Fallback to just setting video output
            m_cameraManager->setVideoOutput(videoPane);
        }
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
    doResize();

    // Calculate the maximum available content height with safety checks
    int contentHeight = this->height() - ui->statusbar->height() - ui->menubar->height();

    qDebug() << "contentHeight: " << contentHeight;
    
    // Set the videoPane to use the full available width and height
    videoPane->setMinimumSize(videoPane->width(), contentHeight);
    videoPane->resize(videoPane->width(), contentHeight);
    
    // Ensure scrollArea is also resized appropriately
    scrollArea->resize(videoPane->width(), contentHeight);
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
    taskmanager->addTask([this, syntaxTree, senderObj]() {
        if (!senderObj) return;
        bool runStatus = semanticAnalyzer->analyze(syntaxTree.get());
        qCDebug(log_ui_mainwindow) << "Script run status: " << runStatus;
        emit emitScriptStatus(runStatus);
        
        if (senderObj == tcpServer) {
            qCDebug(log_ui_mainwindow) << "run finish: " << runStatus;
            emit emitTCPCommandStatus(runStatus);
        }
    });
} 

MainWindow::~MainWindow()
{
    qCDebug(log_ui_mainwindow) << "MainWindow destructor called";
    
    // Stop all camera operations
    stop();
    
    // Delete UI
    if (ui) {
        delete ui;
        ui = nullptr;
    }

    m_cameraManager->stopCamera();
    delete m_cameraManager;
    m_cameraManager = nullptr;
    
    delete m_versionInfoManager;
    m_versionInfoManager = nullptr;
    
    delete m_screenSaverManager;
    m_screenSaverManager = nullptr;
    
    delete m_cornerWidgetManager;
    m_cornerWidgetManager = nullptr;

    delete m_screenScaleDialog;
    m_screenScaleDialog = nullptr;

    delete firmwareManagerDialog;
    firmwareManagerDialog = nullptr;

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
    QTimer::singleShot(0, this, &MainWindow::animateVideoPane);
    
}

void MainWindow::animateVideoPane() {
    if (!videoPane || !scrollArea) {
        setUpdatesEnabled(true);
        blockSignals(false);
        return;
    }

    // Get toolbar visibility and window state
    bool isToolbarVisible = toolbarManager->getToolbar()->isVisible();
    // bool isMaximized = windowState() & Qt::WindowMaximized;

    // Calculate content height based on toolbar visibility
    int contentHeight;
    if (!isFullScreenMode()) contentHeight = this->height() - ui->statusbar->height() - ui->menubar->height();
    else contentHeight = this->height() - ui->menubar->height();

    int contentWidth;
    double aspect_ratio = static_cast<double>(video_width) / video_height;
    if (isToolbarVisible) {
        contentHeight -= toolbarManager->getToolbar()->height();
        contentWidth = static_cast<int>(contentHeight * aspect_ratio);
        qCDebug(log_ui_mainwindow) << "toolbarHeigth" << toolbarManager->getToolbar()->height() << "content height" <<contentHeight << "content width" << contentWidth;
    }else{
        if (!isFullScreenMode()) contentHeight = this->height() - ui->statusbar->height() - ui->menubar->height();
        else contentHeight = this->height() - ui->menubar->height();
        contentWidth = static_cast<int>(contentHeight * aspect_ratio);
    }

    // If window is not maximized and toolbar is invisible, resize the panes
    
    videoPane->setMinimumSize(contentWidth, contentHeight);
    videoPane->resize(contentWidth, contentHeight);
    scrollArea->resize(contentWidth, contentHeight);
    

    if (this->width() > videoPane->width()) {
        // Calculate new position
        int horizontalOffset = (this->width() - videoPane->width()) / 2;
        
        // Also animate the scrollArea
        QPropertyAnimation *scrollAnimation = new QPropertyAnimation(scrollArea, "pos");
        scrollAnimation->setDuration(150);
        scrollAnimation->setStartValue(scrollArea->pos());
        scrollAnimation->setEndValue(QPoint(horizontalOffset, scrollArea->y()));
        scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);

        // Create animation group
        QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
        group->addAnimation(scrollAnimation);
        
        // Cleanup after animation
        connect(group, &QParallelAnimationGroup::finished, this, [this]() {
            setUpdatesEnabled(true);
            blockSignals(false);
            update();
        });
        
        group->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        setUpdatesEnabled(true);
        blockSignals(false);
        update();
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
