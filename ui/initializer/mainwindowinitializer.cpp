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
#include "../recording/recordingcontroller.h"
#include "../../serial/SerialPortManager.h"
#include "../../device/DeviceManager.h"
#include "../../device/HotplugMonitor.h"
#include "../help/helppane.h"
#include "../videopane.h"
#include "../../video/videohid.h"
#include <QThread>
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
#include <QShortcut>

// Define the logging category with inline to avoid multiple definition errors
inline Q_LOGGING_CATEGORY(log_ui_mainwindowinitializer, "opf.ui.mainwindowinitializer")

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
    , m_hidThread(nullptr)  // Will be created during initialization
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
    setupKeyboardShortcuts();
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

    if (m_windowLayoutCoordinator) {
        m_windowLayoutCoordinator->checkInitSize();
    }
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();

    if (m_deviceCoordinator) {
        m_deviceCoordinator->connectHotplugMonitor(hotplugMonitor);
        m_deviceCoordinator->setupDeviceMenu();
    }
    
    if (m_menuCoordinator) {
        m_menuCoordinator->setupLanguageMenu();
        
        // CRITICAL FIX: Capture specific pointer instead of 'this'
        MenuCoordinator* menuCoordinator = m_menuCoordinator;
        connect(m_menuCoordinator, &MenuCoordinator::baudrateChanged, [menuCoordinator](int baudrate) {
            if (menuCoordinator) {
                menuCoordinator->updateBaudrateMenu(baudrate);
            }
        });
    }
}

void MainWindowInitializer::connectCornerWidgetSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting corner widget signals...";
    m_cornerWidgetManager->setMenuBar(m_ui->menubar);

    // CRITICAL FIX: Capture specific pointers instead of 'this' to avoid dangling reference
    // MainWindowInitializer is destroyed after constructor completes, so capturing 'this' causes crash
    WindowLayoutCoordinator* coordinator = m_windowLayoutCoordinator;
    MainWindow* mainWindow = m_mainWindow;
    
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomInClicked, m_mainWindow, [coordinator, mainWindow]() {
        if (coordinator) {
            coordinator->zoomIn();
            if (mainWindow && mainWindow->mouseEdgeTimer) {
                mainWindow->mouseEdgeTimer->start(mainWindow->edgeDuration);
            }
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomOutClicked, m_mainWindow, [coordinator]() {
        if (coordinator) {
            coordinator->zoomOut();
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::zoomReductionClicked, m_mainWindow, [coordinator, mainWindow]() {
        if (coordinator) {
            coordinator->zoomReduction();
            if (mainWindow && mainWindow->mouseEdgeTimer && mainWindow->mouseEdgeTimer->isActive()) {
                mainWindow->mouseEdgeTimer->stop();
            }
        }
    });
    connect(m_cornerWidgetManager, &CornerWidgetManager::screenScaleClicked, m_mainWindow, &MainWindow::configScreenScale);
    connect(m_cornerWidgetManager, &CornerWidgetManager::virtualKeyboardClicked, m_mainWindow, &MainWindow::onToggleVirtualKeyboard);
    connect(m_cornerWidgetManager, &CornerWidgetManager::captureClicked, m_mainWindow, &MainWindow::takeImageDefault);
    
    connect(m_cornerWidgetManager, &CornerWidgetManager::fullScreenClicked, m_mainWindow, [coordinator]() {
        if (coordinator) {
            qCDebug(log_ui_mainwindowinitializer) << "*** Fullscreen button clicked - toggling fullscreen ***";
            coordinator->fullScreen();
        }
    });
    
    connect(m_cornerWidgetManager, &CornerWidgetManager::pasteClicked, m_mainWindow, &MainWindow::onActionPasteToTarget);
    connect(m_cornerWidgetManager, &CornerWidgetManager::screensaverClicked, m_mainWindow, &MainWindow::onActionScreensaver);
    connect(m_cornerWidgetManager, &CornerWidgetManager::toggleSwitchChanged, m_mainWindow, &MainWindow::onToggleSwitchStateChanged);
    connect(m_cornerWidgetManager, &CornerWidgetManager::keyboardLayoutChanged, m_mainWindow, &MainWindow::onKeyboardLayoutCombobox_Changed);
    connect(m_cornerWidgetManager, &CornerWidgetManager::recordingToggled, m_mainWindow, &MainWindow::toggleRecording);
    connect(m_cornerWidgetManager, &CornerWidgetManager::muteToggled, m_mainWindow, &MainWindow::toggleMute);

    // Connect layout changes to update corner widget position
    // CRITICAL FIX: Capture specific pointers instead of 'this' to avoid dangling reference
    CornerWidgetManager* cornerWidgetManager = m_cornerWidgetManager;
    QMenuBar* menuBar = m_ui->menubar;
    connect(coordinator, &WindowLayoutCoordinator::layoutChanged, cornerWidgetManager, [cornerWidgetManager, menuBar, coordinator](const QSize &size) {
        cornerWidgetManager->updatePosition(size.width(), menuBar->height(), coordinator->isFullScreenMode());
    });
}

void MainWindowInitializer::connectDeviceManagerSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting device manager signals...";
    m_statusBarManager = new StatusBarManager(m_ui->statusbar, m_mainWindow);
    m_mainWindow->m_statusBarManager = m_statusBarManager;
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    if (hotplugMonitor) {
        // CRITICAL FIX: Capture specific pointers instead of 'this' to avoid dangling reference
        StatusBarManager* statusBarManager = m_statusBarManager;
        CameraManager* cameraManager = m_cameraManager;
        QStackedLayout* stackedLayout = m_stackedLayout;
        VideoPane* videoPane = m_videoPane;
        
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn, 
                m_statusBarManager, [statusBarManager](const DeviceInfo& device) {
                    if (statusBarManager) {
                        qCDebug(log_ui_mainwindowinitializer) << "Received newDevicePluggedIn for port:" << device.portChain;
                        statusBarManager->showNewDevicePluggedIn(device.portChain);
                    }
                });
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged, 
                m_statusBarManager, [statusBarManager](const DeviceInfo& device) {
                    if (statusBarManager) {
                        qCDebug(log_ui_mainwindowinitializer) << "Received deviceUnplugged for port:" << device.portChain;
                        statusBarManager->showDeviceUnplugged(device.portChain);
                    }
                });
                
        connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
                m_mainWindow, [cameraManager, stackedLayout](const DeviceInfo& device) {
                    if (!device.hasCameraDevice()) return;
                    if (!cameraManager || !stackedLayout) return;
                    bool deactivated = cameraManager->deactivateCameraByPortChain(device.portChain);
                    if (deactivated) {
                        qCInfo(log_ui_mainwindowinitializer) << "✓ Camera deactivated for port:" << device.portChain;
                        stackedLayout->setCurrentIndex(0);
                    }
                });
                
        connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
                m_mainWindow, [cameraManager, stackedLayout, videoPane](const DeviceInfo& device) {
                    if (!device.hasCameraDevice()) return;
                    if (!cameraManager || !stackedLayout || !videoPane) return;
                    bool switchSuccess = cameraManager->tryAutoSwitchToNewDevice(device.portChain);
                    if (switchSuccess) {
                        qCInfo(log_ui_mainwindowinitializer) << "✓ Camera auto-switched to port:" << device.portChain;
                        stackedLayout->setCurrentIndex(stackedLayout->indexOf(videoPane));
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
    
    // Note: Passing m_mainWindow as both window and parent is correct:
    // - First param: the window to monitor/control
    // - Third param: QObject parent for memory management
    m_windowControlManager = new WindowControlManager(m_mainWindow, m_toolbarManager->getToolbar(), m_mainWindow);
    m_mainWindow->m_windowControlManager = m_windowControlManager;
    
    // CRITICAL DEBUG: Temporarily disable WindowControlManager to test if it's blocking menus
    // m_windowControlManager->setAutoHideEnabled(true);
    qDebug() << "[DEBUG] WindowControlManager auto-hide DISABLED for menu testing";
    
    m_windowControlManager->setAutoHideDelay(5000);  // 5 seconds auto-hide delay
    m_windowControlManager->setEdgeDetectionThreshold(5);
    m_windowControlManager->setAnimationDuration(300);
    
    // Connect toolbar visibility changes
    // Note: Only connect WindowControlManager's signal to avoid duplicate calls
    // WindowControlManager emits after both manual toggles and auto-hide operations
    connect(m_windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
            m_mainWindow, &MainWindow::onToolbarVisibilityChanged);
            
    // Set up the recording controller
    // setupRecordingController();
}

void MainWindowInitializer::connectCameraSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting camera signals...";
    connect(m_cameraManager, &CameraManager::cameraActiveChanged, m_mainWindow, &MainWindow::updateCameraActive);
    connect(m_cameraManager, &CameraManager::cameraError, m_mainWindow, &MainWindow::displayCameraError);
    connect(m_cameraManager, &CameraManager::imageCaptured, m_mainWindow, &MainWindow::processCapturedImage);
    connect(m_deviceCoordinator, &DeviceCoordinator::deviceSwitchCompleted, m_mainWindow, &MainWindow::onDeviceSwitchCompleted);
    connect(m_deviceCoordinator, &DeviceCoordinator::deviceSelected, m_mainWindow, &MainWindow::onDeviceSelected);
    connect(m_cameraManager, &CameraManager::resolutionsUpdated, m_mainWindow, &MainWindow::onResolutionsUpdated);
    
    // This lambda only does logging, so it's safe, but fix for consistency
    connect(m_cameraManager, &CameraManager::newDeviceAutoConnected, m_mainWindow, [](const QCameraDevice&, const QString& portChain) {
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
    connect(m_cameraManager, &CameraManager::cameraActiveChanged,
            m_videoPane, &VideoPane::onCameraActiveChanged);
}

void MainWindowInitializer::connectVideoHidSignals()
{
    qCDebug(log_ui_mainwindowinitializer) << "Connecting video HID signals...";
    connect(m_videoPane, &VideoPane::mouseMoved,
            m_statusBarManager, &StatusBarManager::onLastMouseLocation);
    connect(&VideoHid::getInstance(), &VideoHid::inputResolutionChanged, m_mainWindow, &MainWindow::onInputResolutionChanged);
    connect(&VideoHid::getInstance(), &VideoHid::resolutionChangeUpdate, m_mainWindow, &MainWindow::onResolutionChange);
}

void MainWindowInitializer::setupRecordingController()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up recording controller...";
    
    // Create the recording controller
    m_mainWindow->m_recordingController = new RecordingController(m_mainWindow, m_cameraManager);

    // Create and show the floating duration-only widget instead of adding full controls to toolbar
    QWidget* floatingDuration = m_mainWindow->m_recordingController->createFloatingDurationWidget(m_mainWindow);
    if (floatingDuration) {
        floatingDuration->adjustSize();
        // Place it near the top-right, below the menu bar
        int x = m_mainWindow->width() - floatingDuration->width() - 12;
        int y = m_ui->menubar->height() + 6;
        floatingDuration->move(x, y);
        floatingDuration->hide(); // Hide by default, will be shown when recording starts
        // Reposition when layout changes
        WindowLayoutCoordinator* coordinator = m_windowLayoutCoordinator;
        QWidget* fd = floatingDuration;
        QMenuBar* menuBar = m_ui->menubar;
        connect(coordinator, &WindowLayoutCoordinator::layoutChanged, m_mainWindow, [coordinator, fd, menuBar](const QSize& size) {
            fd->adjustSize();
            int xNew = coordinator->isFullScreenMode() ? (size.width() - fd->width() - 8) : (size.width() - fd->width() - 12);
            int yNew = menuBar->height() + 6;
            fd->move(xNew, yNew);
        });
    }
}

void MainWindowInitializer::initializeCamera()
{
    qCDebug(log_ui_mainwindowinitializer) << "Initializing camera...";
    m_mainWindow->initCamera();
    
    // Set up VideoPane with FFmpeg backend BEFORE device auto-selection
    // This ensures the video pipeline is ready when the device is switched
    CameraManager* cameraManager = m_cameraManager;
    VideoPane* videoPane = m_videoPane;
    
    // Initialize camera video pipeline WITHOUT starting capture yet
    // The device auto-selection will start the capture with the correct device
    bool success = cameraManager->initializeCameraWithVideoOutput(videoPane, false);
    if (success) {
        qCDebug(log_ui_mainwindowinitializer) << "✓ Camera video pipeline initialized (waiting for device selection)";
    } else {
        qCWarning(log_ui_mainwindowinitializer) << "Failed to initialize camera video pipeline";
    }

    // Capture audioManager pointer directly to avoid dangling reference
    AudioManager* audioManager = m_mainWindow->m_audioManager;
    CornerWidgetManager* cornerWidgetManager = m_cornerWidgetManager;
    QTimer::singleShot(300, m_mainWindow, [audioManager, cornerWidgetManager]() {
        audioManager->initializeAudio();
        qDebug() << "✓ Audio initialization triggered";
        
        // Restore mute state from settings
        bool isMuted = GlobalSetting::instance().getAudioMuted();
        if (isMuted) {
            audioManager->setVolume(0.0);
            qDebug() << "✓ Audio restored to muted state";
        }
        
        // Update the mute button to reflect the saved state
        if (cornerWidgetManager) {
            cornerWidgetManager->restoreMuteState(isMuted);
            qDebug() << "✓ Mute button state restored:" << (isMuted ? "muted" : "unmuted");
        }
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

void MainWindowInitializer::setupKeyboardShortcuts()
{
    qCDebug(log_ui_mainwindowinitializer) << "Setting up keyboard shortcuts...";
    
    // Alt+F11: Toggle fullscreen
    QShortcut *fullscreenShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_F11), m_mainWindow);
    
    // CRITICAL FIX: Capture specific pointers instead of 'this' to avoid dangling reference
    // MainWindowInitializer is destroyed after constructor completes, so capturing 'this' causes crash
    MainWindow* mainWindow = m_mainWindow;
    WindowLayoutCoordinator* coordinator = m_windowLayoutCoordinator;
    
    // Add debug logging for when shortcut is activated
    QObject::connect(fullscreenShortcut, &QShortcut::activated, [mainWindow, coordinator]() {
        if (!mainWindow || !coordinator) {
            qCCritical(log_ui_mainwindowinitializer) << "CRITICAL: mainWindow or coordinator is null in shortcut handler!";
            return;
        }
        
        qCDebug(log_ui_mainwindowinitializer) << "*** Alt+F11 SHORTCUT ACTIVATED - Toggling fullscreen ***";
        qCDebug(log_ui_mainwindowinitializer) << "Window state BEFORE fullScreen() call:" << mainWindow->windowState();
        qCDebug(log_ui_mainwindowinitializer) << "Window ID BEFORE fullScreen() call:" << mainWindow->winId();
        qCDebug(log_ui_mainwindowinitializer) << "Window geometry BEFORE fullScreen() call:" << mainWindow->geometry();
        qCDebug(log_ui_mainwindowinitializer) << "Window isVisible BEFORE fullScreen() call:" << mainWindow->isVisible();
        coordinator->fullScreen();
    });
    
    qCDebug(log_ui_mainwindowinitializer) << "Registered Alt+F11 shortcut for fullscreen toggle";
    qCDebug(log_ui_mainwindowinitializer) << "Shortcut context:" << fullscreenShortcut->context();
    qCDebug(log_ui_mainwindowinitializer) << "Shortcut enabled:" << fullscreenShortcut->isEnabled();
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

