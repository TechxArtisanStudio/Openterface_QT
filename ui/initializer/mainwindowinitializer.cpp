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

#include "mainwindowinitializer.h"
#include "../mainwindow.h"
#include "mainwindow_ui_access.h"
#include "../../global.h"
#include "../globalsetting.h"
#include "../statusbar/statusbarmanager.h"
#include "../../host/HostManager.h"
#include "../../host/cameramanager.h"
#include "../../serial/SerialPortManager.h"
#include "../../device/DeviceManager.h"
#include "../../device/HotplugMonitor.h"
#include "../help/helppane.h"
#include "../videopane.h"
#include "../../video/videohid.h"
#include "ui/TaskManager.h"
#include "../coordinator/windowlayoutcoordinator.h"
#include "../toolbar/toolbarmanager.h"
#include "../windowcontrolmanager.h"
#include "../cornerwidget/cornerwidgetmanager.h"
#include "../coordinator/devicecoordinator.h"
#include "../coordinator/menucoordinator.h"
#include "../../ui/advance/scripttool.h"
#include "../../target/MouseManager.h"
#include "../../scripts/KeyboardMouse.h"
#include "../../scripts/semanticAnalyzer.h"
#include "../../host/audiomanager.h"
#include "../../server/tcpServer.h"

#include <QTimer>
#include <QStackedLayout>

Q_LOGGING_CATEGORY(log_ui_mainwindowinitializer, "opf.ui.mainwindowinitializer")

MainWindowInitializer::MainWindowInitializer(MainWindow *mainWindow, QObject *parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_ui(mainWindow->ui)
    , m_stackedLayout(mainWindow->stackedLayout)
    , m_videoPane(mainWindow->videoPane)
    , m_cameraManager(mainWindow->m_cameraManager)
    , m_statusBarManager(nullptr)  // Will be created during initialization
    , m_cornerWidgetManager(mainWindow->m_cornerWidgetManager)
    , m_windowLayoutCoordinator(mainWindow->m_windowLayoutCoordinator)
    , m_toolbarManager(mainWindow->toolbarManager)
    , m_windowControlManager(nullptr)  // Will be created during initialization
    , m_deviceCoordinator(nullptr)  // Will be created during initialization
    , m_menuCoordinator(nullptr)  // Will be created during initialization
    , m_languageManager(mainWindow->m_languageManager)
    , m_mouseEdgeTimer(nullptr)  // Will be created during initialization
{
    qCDebug(log_ui_mainwindowinitializer) << "MainWindowInitializer created";
}

MainWindowInitializer::~MainWindowInitializer()
{
    qCDebug(log_ui_mainwindowinitializer) << "MainWindowInitializer destroyed";
}

void MainWindowInitializer::initialize()
{
    qCDebug(log_ui_mainwindowinitializer) << "Starting initialization sequence...";
    
    setupCentralWidget();
    setupCoordinators();
    connectCornerWidgetSignals();
    connectDeviceManagerSignals();
    connectActionSignals();
    setupToolbar();
    connectCameraSignals();
    connectVideoHidSignals();
    initializeCamera();
    setupScriptComponents();
    setupEventCallbacks();
    finalize();
    
    qCDebug(log_ui_mainwindowinitializer) << "Initialization sequence complete";
}

void MainWindowInitializer::setupCentralWidget()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up central widget...";
    QWidget *centralWidget = new QWidget(m_mainWindow);
    centralWidget->setLayout(m_stackedLayout);
    centralWidget->setMouseTracking(true);

    HelpPane *helpPane = new HelpPane;
    m_stackedLayout->addWidget(helpPane);
    
    m_videoPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stackedLayout->addWidget(m_videoPane);

    m_stackedLayout->setCurrentIndex(0);

    m_mainWindow->setCentralWidget(centralWidget);
}

void MainWindowInitializer::setupCoordinators()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up coordinators...";
    // WindowLayoutCoordinator is initialized early in MainWindow constructor
    
    m_deviceCoordinator = new DeviceCoordinator(m_ui->menuDevice, m_cameraManager, m_mainWindow);
    m_mainWindow->m_deviceCoordinator = m_deviceCoordinator;

    m_menuCoordinator = new MenuCoordinator(m_ui->menuLanguages, m_ui->menuBaudrate, m_languageManager, m_mainWindow, m_mainWindow);
    m_mainWindow->m_menuCoordinator = m_menuCoordinator;

    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();

    if (m_deviceCoordinator) {
        m_deviceCoordinator->connectHotplugMonitor(hotplugMonitor);
        m_deviceCoordinator->setupDeviceMenu();
    }
    
    if (m_menuCoordinator) {
        m_menuCoordinator->setupLanguageMenu();
        connect(m_menuCoordinator, &MenuCoordinator::baudrateChanged, [this](int baudrate) {
            m_menuCoordinator->updateBaudrateMenu(baudrate);
        });
    }
}

void MainWindowInitializer::connectCornerWidgetSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting corner widget signals...";
    m_cornerWidgetManager->setMenuBar(m_ui->menubar);

    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomInClicked, m_mainWindow, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->zoomIn();
            if (m_mainWindow->mouseEdgeTimer) {
                m_mainWindow->mouseEdgeTimer->start(m_mainWindow->edgeDuration);
            }
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomOutClicked, m_mainWindow, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->zoomOut();
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomReductionClicked, m_mainWindow, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->zoomReduction();
            if (m_mainWindow->mouseEdgeTimer && m_mainWindow->mouseEdgeTimer->isActive()) {
                m_mainWindow->mouseEdgeTimer->stop();
            }
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::screenScaleClicked, m_mainWindow, &MainWindow::configScreenScale);
    connect(m_cornerWidgetManager, &CornerWidgetManager::virtualKeyboardClicked, m_mainWindow, &MainWindow::onToggleVirtualKeyboard);
    connect(m_cornerWidgetManager, &CornerWidgetManager::captureClicked, m_mainWindow, &MainWindow::takeImageDefault);
    connect(m_cornerWidgetManager, &CornerWidgetManager::fullScreenClicked, m_mainWindow, [this]() {
        if (m_windowLayoutCoordinator) {
            m_windowLayoutCoordinator->fullScreen();
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::pasteClicked, m_mainWindow, &MainWindow::onActionPasteToTarget);
    connect(m_cornerWidgetManager, &CornerWidgetManager::screensaverClicked, m_mainWindow, &MainWindow::onActionScreensaver);
    connect(m_cornerWidgetManager, &CornerWidgetManager::toggleSwitchChanged, m_mainWindow, &MainWindow::onToggleSwitchStateChanged);
    connect(m_cornerWidgetManager, &CornerWidgetManager::keyboardLayoutChanged, m_mainWindow, &MainWindow::onKeyboardLayoutCombobox_Changed);
}

void MainWindowInitializer::connectDeviceManagerSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting device manager signals...";
    m_statusBarManager = new StatusBarManager(m_ui->statusbar, m_mainWindow);
    m_mainWindow->m_statusBarManager = m_statusBarManager;
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    if (hotplugMonitor) {
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn, 
                m_statusBarManager, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindowinitializer) << "Received newDevicePluggedIn for port:" << device.portChain;
                    m_statusBarManager->showNewDevicePluggedIn(device.portChain);
                });
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged, 
                m_statusBarManager, [this](const DeviceInfo& device) {
                    qCDebug(log_ui_mainwindowinitializer) << "Received deviceUnplugged for port:" << device.portChain;
                    m_statusBarManager->showDeviceUnplugged(device.portChain);
                });
                
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
                m_mainWindow, [this](const DeviceInfo& device) {
                    if (!device.hasCameraDevice()) return;
                    bool deactivated = m_cameraManager->deactivateCameraByPortChain(device.portChain);
                    if (deactivated) {
                        qCInfo(log_ui_mainwindowinitializer) << "✓ Camera deactivated for port:" << device.portChain;
                        m_stackedLayout->setCurrentIndex(0);
                    }
                });
                
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
                m_mainWindow, [this](const DeviceInfo& device) {
                    if (!device.hasCameraDevice()) return;
                    bool switchSuccess = m_cameraManager->tryAutoSwitchToNewDevice(device.portChain);
                    if (switchSuccess) {
                        qCInfo(log_ui_mainwindowinitializer) << "✓ Camera auto-switched to port:" << device.portChain;
                        m_stackedLayout->setCurrentIndex(m_stackedLayout->indexOf(m_videoPane));
                    }
                });
        qCDebug(log_ui_mainwindowinitializer) << "Connected hotplug monitor signals";
    } else {
        qCWarning(log_ui_mainwindowinitializer) << "Failed to get hotplug monitor";
    }
}

void MainWindowInitializer::connectActionSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting action signals...";
    connect(m_ui->actionRelative, &QAction::triggered, m_mainWindow, &MainWindow::onActionRelativeTriggered);
    connect(m_ui->actionAbsolute, &QAction::triggered, m_mainWindow, &MainWindow::onActionAbsoluteTriggered);
    connect(m_ui->actionMouseAutoHide, &QAction::triggered, m_mainWindow, &MainWindow::onActionMouseAutoHideTriggered);
    connect(m_ui->actionMouseAlwaysShow, &QAction::triggered, m_mainWindow, &MainWindow::onActionMouseAlwaysShowTriggered);
    connect(m_ui->actionFactory_reset_HID, &QAction::triggered, m_mainWindow, &MainWindow::onActionFactoryResetHIDTriggered);
    connect(m_ui->actionResetSerialPort, &QAction::triggered, m_mainWindow, &MainWindow::onActionResetSerialPortTriggered);
    connect(m_ui->actionTo_Host, &QAction::triggered, m_mainWindow, &MainWindow::onActionSwitchToHostTriggered);
    connect(m_ui->actionTo_Target, &QAction::triggered, m_mainWindow, &MainWindow::onActionSwitchToTargetTriggered);
    connect(m_ui->actionPaste, &QAction::triggered, m_mainWindow, &MainWindow::onActionPasteToTarget);
    connect(m_ui->actionTCPServer, &QAction::triggered, m_mainWindow, &MainWindow::startServer);
    connect(m_ui->actionScriptTool, &QAction::triggered, m_mainWindow, &MainWindow::showScriptTool);
    connect(m_ui->actionRecordingSettings, &QAction::triggered, m_mainWindow, &MainWindow::showRecordingSettings);
}

void MainWindowInitializer::setupToolbar()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up toolbar...";
    m_mainWindow->addToolBar(Qt::TopToolBarArea, m_toolbarManager->getToolbar());
    m_toolbarManager->getToolbar()->setVisible(false);
    
    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->setToolbarManager(m_toolbarManager);
    }
    
    m_windowControlManager = new WindowControlManager(m_mainWindow, m_toolbarManager->getToolbar(), m_mainWindow);
    m_mainWindow->m_windowControlManager = m_windowControlManager;
    m_windowControlManager->setAutoHideEnabled(true);
    m_windowControlManager->setAutoHideDelay(10000);
    m_windowControlManager->setEdgeDetectionThreshold(5);
    m_windowControlManager->setAnimationDuration(300);
    
    connect(m_windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
            m_mainWindow, &MainWindow::onToolbarVisibilityChanged);
    connect(m_toolbarManager, &ToolbarManager::toolbarVisibilityChanged,
            m_mainWindow, &MainWindow::onToolbarVisibilityChanged);
}

void MainWindowInitializer::connectCameraSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting camera signals...";
    connect(m_cameraManager, &CameraManager::cameraActiveChanged, m_mainWindow, &MainWindow::updateCameraActive);
    connect(m_cameraManager, &CameraManager::cameraError, m_mainWindow, &MainWindow::displayCameraError);
    connect(m_cameraManager, &CameraManager::imageCaptured, m_mainWindow, &MainWindow::processCapturedImage);
    connect(m_cameraManager, &CameraManager::resolutionsUpdated, m_mainWindow, &MainWindow::onResolutionsUpdated);
    connect(m_cameraManager, &CameraManager::newDeviceAutoConnected, m_mainWindow, [this](const QCameraDevice&, const QString& portChain) {
        qCInfo(log_ui_mainwindowinitializer) << "Camera auto-connected to new device at port:" << portChain;
    });
    
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitching, 
            m_statusBarManager, &StatusBarManager::showCameraSwitching);
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitchComplete, 
            m_statusBarManager, &StatusBarManager::showCameraSwitchComplete);
    
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitching,
            m_videoPane, &VideoPane::onCameraDeviceSwitching);
    connect(m_cameraManager, &CameraManager::cameraDeviceSwitchComplete,
            m_videoPane, &VideoPane::onCameraDeviceSwitchComplete);
}

void MainWindowInitializer::connectVideoHidSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting video HID signals...";
    connect(m_videoPane, &VideoPane::mouseMoved,
            m_statusBarManager, &StatusBarManager::onLastMouseLocation);
    connect(&VideoHid::getInstance(), &VideoHid::inputResolutionChanged, m_mainWindow, &MainWindow::onInputResolutionChanged);
    connect(&VideoHid::getInstance(), &VideoHid::resolutionChangeUpdate, m_mainWindow, &MainWindow::onResolutionChange);
}

void MainWindowInitializer::initializeCamera()
{
    qCDebug(log_ui_mainwindowinitializer) << "Initializing camera...";
    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->checkInitSize();
    }
    m_mainWindow->initCamera();
    
    QTimer::singleShot(200, m_mainWindow, [this]() {
        bool success = m_cameraManager->initializeCameraWithVideoOutput(m_videoPane);
        if (success) {
            qDebug() << "✓ Camera successfully initialized with video output";
        } else {
            qCWarning(log_ui_mainwindowinitializer) << "Failed to initialize camera with video output";
        }
    });

    QTimer::singleShot(300, m_mainWindow, [this]() {
        m_mainWindow->m_audioManager->initializeAudio();
        qDebug() << "✓ Audio initialization triggered";
    });
}

void MainWindowInitializer::setupScriptComponents()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up script components...";
    m_mainWindow->mouseManager = std::make_unique<MouseManager>();
    m_mainWindow->keyboardMouse = std::make_unique<KeyboardMouse>();
    m_mainWindow->semanticAnalyzer = std::make_unique<SemanticAnalyzer>(m_mainWindow->mouseManager.get(), m_mainWindow->keyboardMouse.get());
    connect(m_mainWindow->semanticAnalyzer.get(), &SemanticAnalyzer::captureImg, m_mainWindow, &MainWindow::takeImage);
    connect(m_mainWindow->semanticAnalyzer.get(), &SemanticAnalyzer::captureAreaImg, m_mainWindow, &MainWindow::takeAreaImage);
    
    m_mainWindow->scriptTool = new ScriptTool(m_mainWindow);
    connect(m_mainWindow, &MainWindow::emitScriptStatus, m_mainWindow->scriptTool, &ScriptTool::resetCommmandLine);
    connect(m_mainWindow->semanticAnalyzer.get(), &SemanticAnalyzer::commandIncrease, m_mainWindow->scriptTool, &ScriptTool::handleCommandIncrement);
}

void MainWindowInitializer::setupEventCallbacks()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up event callbacks...";
    HostManager::getInstance().setEventCallback(m_mainWindow);
    VideoHid::getInstance().setEventCallback(m_mainWindow);
    qApp->installEventFilter(m_mainWindow);
    AudioManager::getInstance().start();
}

void MainWindowInitializer::finalize()
{
    qCDebug(log_ui_mainwindowinitializer) << "Finalizing initialization...";
    QString windowTitle = QString("Openterface Mini-KVM - %1").arg(APP_VERSION);
    m_mainWindow->setWindowTitle(windowTitle);

    m_mainWindow->mouseEdgeTimer = new QTimer(m_mainWindow);
    connect(m_mainWindow->mouseEdgeTimer, &QTimer::timeout, m_mainWindow, &MainWindow::checkMousePosition);

    connect(m_languageManager, &LanguageManager::languageChanged, m_mainWindow, &MainWindow::updateUI);
    
    connect(&SerialPortManager::getInstance(), &SerialPortManager::connectedPortChanged, m_mainWindow, &MainWindow::onPortConnected);
    connect(&SerialPortManager::getInstance(), &SerialPortManager::armBaudratePerformanceRecommendation, m_mainWindow, &MainWindow::onArmBaudratePerformanceRecommendation);

    m_mainWindow->onLastKeyPressed("");
    m_mainWindow->onLastMouseLocation(QPoint(0, 0), "");

    GlobalVar::instance().setMouseAutoHide(GlobalSetting::instance().getMouseAutoHideEnable());
    m_mainWindow->initializeKeyboardLayouts();
}
