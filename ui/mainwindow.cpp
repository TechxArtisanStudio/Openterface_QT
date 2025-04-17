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
#include "loghandler.h"
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
                            m_screenSaverManager(new ScreenSaverManager(this))
                            // cameraAdjust(new CameraAdjust(this))
{
    Q_UNUSED(parent);
    // QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);


    qCDebug(log_ui_mainwindow) << "Init camera...";
    
    ui->setupUi(this);
    

    initializeKeyboardLayouts();

    GlobalVar::instance().setMouseAutoHide(GlobalSetting::instance().getMouseAutoHideEnable());

    m_statusBarManager = new StatusBarManager(ui->statusbar, this);
    taskmanager = TaskManager::instance();
    
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

    ui->menubar->setCornerWidget(ui->cornerWidget, Qt::TopRightCorner);

    setCentralWidget(centralWidget);
    qCDebug(log_ui_mainwindow) << "Set host manager event callback...";
    HostManager::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe Video HID connected...";
    VideoHid::getInstance().setEventCallback(this);

    qCDebug(log_ui_mainwindow) << "Observe video input changed...";
    connect(&m_source, &QMediaDevices::videoInputsChanged, this, &MainWindow::updateCameras);

    qCDebug(log_ui_mainwindow) << "Observe Relative/Absolute toggle...";
    connect(ui->actionRelative, &QAction::triggered, this, &MainWindow::onActionRelativeTriggered);
    connect(ui->actionAbsolute, &QAction::triggered, this, &MainWindow::onActionAbsoluteTriggered);

    connect(ui->actionMouseAutoHide, &QAction::triggered, this, &MainWindow::onActionMouseAutoHideTriggered);
    connect(ui->actionMouseAlwaysShow, &QAction::triggered, this, &MainWindow::onActionMouseAlwaysShowTriggered);

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


    qCDebug(log_ui_mainwindow) << "Observe switch usb connection trigger...";
    connect(ui->actionTo_Host, &QAction::triggered, this, &MainWindow::onActionSwitchToHostTriggered);
    connect(ui->actionTo_Target, &QAction::triggered, this, &MainWindow::onActionSwitchToTargetTriggered);

    qCDebug(log_ui_mainwindow) << "Observe action paste from host...";
    connect(ui->actionPaste, &QAction::triggered, this, &MainWindow::onActionPasteToTarget);
    connect(ui->pasteButton, &QPushButton::released, this, &MainWindow::onActionPasteToTarget);

    connect(ui->screensaverButton, &QPushButton::released, this, &MainWindow::onActionScreensaver);

    connect(ui->virtualKeyboardButton, &QPushButton::released, this, &MainWindow::onToggleVirtualKeyboard);

    addToolBar(Qt::TopToolBarArea, toolbarManager->getToolbar());
    toolbarManager->getToolbar()->setVisible(false);
    
    connect(m_cameraManager, &CameraManager::cameraActiveChanged, this, &MainWindow::updateCameraActive);
    connect(m_cameraManager, &CameraManager::cameraError, this, &MainWindow::displayCameraError);
    connect(m_cameraManager, &CameraManager::imageCaptured, this, &MainWindow::processCapturedImage);                                         
    connect(m_cameraManager, &CameraManager::resolutionsUpdated, this, &MainWindow::onResolutionsUpdated);
    connect(&VideoHid::getInstance(), &VideoHid::inputResolutionChanged, this, &MainWindow::onInputResolutionChanged);
    connect(&VideoHid::getInstance(), &VideoHid::resolutionChangeUpdate, this, &MainWindow::onResolutionChange);

    #ifdef ONLINE_VERSION
        qCDebug(log_ui_mainwindow) << "Test actionTCPServer true...";
        ui->actionTCPServer->setVisible(true);
        connect(ui->actionTCPServer, &QAction::triggered, this, &MainWindow::startServer);
    #else
        qCDebug(log_ui_mainwindow) << "Test actionTCPServer false...";
        ui->actionTCPServer->setVisible(false);
    #endif

    qDebug() << "Init camera...";
    checkInitSize();
    initCamera();

    // Connect palette change signal to the slot
    onLastKeyPressed("");
    onLastMouseLocation(QPoint(0, 0), "");

    // Connect zoom buttons
    connect(ui->ZoomInButton, &QPushButton::clicked, this, &MainWindow::onZoomIn);
    connect(ui->ZoomOutButton, &QPushButton::clicked, this, &MainWindow::onZoomOut);
    connect(ui->ZoomReductionButton, &QPushButton::clicked, this, &MainWindow::onZoomReduction);
    
    connect(ui->captureButton, &QPushButton::clicked, this, &MainWindow::takeImageDefault);
    scrollArea->ensureWidgetVisible(videoPane);

    // Set the window title with the version number
    qDebug() << "Set window title" << APP_VERSION;
    QString windowTitle = QString("Openterface Mini-KVM - %1").arg(APP_VERSION);
    setWindowTitle(windowTitle);

    mouseEdgeTimer = new QTimer(this);
    connect(mouseEdgeTimer, &QTimer::timeout, this, &MainWindow::checkMousePosition);
    // mouseEdgeTimer->start(edgeDuration); // Start the timer with the new duration

    // Initialize the virtual keyboard button icon
    QIcon icon(":/images/keyboard-down.svg");
    ui->virtualKeyboardButton->setIcon(icon);

    // Add this after other menu connections
    connect(ui->menuBaudrate, &QMenu::triggered, this, &MainWindow::onBaudrateMenuTriggered);
    connect(&SerialPortManager::getInstance(), &SerialPortManager::connectedPortChanged, this, &MainWindow::onPortConnected);
    
    qApp->installEventFilter(this);

    // usbControl = new USBControl(this);
    // connect(ui->contrastButton, &QPushButton::clicked, cameraAdjust, &CameraAdjust::toggleVisibility);
    // connect(ui->contrastButton, &QPushButton::toggled, cameraAdjust, &CameraAdjust::setVisible);

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
    setTooltip();

    // Add this connection after toolbarManager is created
    connect(toolbarManager, &ToolbarManager::toolbarVisibilityChanged,
            this, &MainWindow::onToolbarVisibilityChanged);
    // qCDebug(log_ui_mainwindow) << "full screen...";
    connect(ui->fullScreenButton, &QPushButton::clicked, this, &MainWindow::fullScreen);

    connect(ui->keyboardLayoutComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onKeyboardLayoutCombobox_Changed(int)));


    connect(m_languageManager, &LanguageManager::languageChanged, this, &MainWindow::updateUI);
    setupLanguageMenu();
    // fullScreen();
    // qCDebug(log_ui_mainwindow) << "full finished";
    
}

#ifdef ONLINE_VERSION
    void MainWindow::startServer(){
        tcpServer = new TcpServer(this);
        tcpServer->startServer(SERVER_PORT);
        qCDebug(log_ui_mainwindow) << "TCP Server start at port 12345";
        connect(m_cameraManager, &CameraManager::lastImagePath, tcpServer, &TcpServer::handleImgPath);
        connect(tcpServer, &TcpServer::syntaxTreeReady, this, &MainWindow::handleSyntaxTree);
        connect(this, &MainWindow::emitTCPCommandStatus, tcpServer, &TcpServer::recvTCPCommandStatus);
    }
#endif

void MainWindow::updateUI() {
    ui->retranslateUi(this); // Update the UI elements
    // this->menuBar()->clear();
    setupLanguageMenu();
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

void MainWindow::setTooltip(){
    ui->ZoomInButton->setToolTip(tr("Zoom in"));
    ui->ZoomOutButton->setToolTip(tr("Zoom out"));
    ui->ZoomReductionButton->setToolTip(tr("Restore original size"));
    ui->virtualKeyboardButton->setToolTip(tr("Function key and composite key"));
    ui->pasteButton->setToolTip(tr("Paste text to target"));
    ui->screensaverButton->setToolTip(tr("Mouse dance"));
    ui->captureButton->setToolTip(tr("Full screen capture"));
    ui->fullScreenButton->setToolTip(tr("Full screen mode"));
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
    

    // m_cameraManager->loadCameraSettingAndSetCamera();

    GlobalVar::instance().setWinWidth(this->width());
    GlobalVar::instance().setWinHeight(this->height());
}

void MainWindow::checkInitSize(){
    QScreen *currentScreen = this->screen();
    systemScaleFactor = currentScreen->devicePixelRatio();
    if(systemScaleFactor != 1.0){
        resize(int(this->width() / systemScaleFactor), int(this->height() / systemScaleFactor));

        qCDebug(log_ui_mainwindow) << "Resize now: " << this->width() << this->height();
        qCDebug(log_ui_mainwindow) << "Resize now: " << this->width() / systemScaleFactor 
        << this->height() / systemScaleFactor;
    }
    qCDebug(log_ui_mainwindow) << "System scale factor: " << systemScaleFactor;
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    static bool isResizing = false;

    if (isResizing || isFullScreenMode()) {
        return;
    }

    isResizing = true;

    qCDebug(log_ui_mainwindow) << "Handle window resize event.";
    QMainWindow::resizeEvent(event);  // Call base class implementation
    
    doResize();

    // Update global variables with the new window size
    isResizing = false;

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
    if(GlobalVar::instance().getCaptureWidth() && GlobalVar::instance().getCaptureHeight()){
        video_width = GlobalVar::instance().getCaptureWidth();
        video_height = GlobalVar::instance().getCaptureHeight();
    }
    qreal aspect_ratio = static_cast<qreal>(video_width) / video_height;
    if(GlobalVar::instance().getInputAspectRatio()){
        aspect_ratio = GlobalVar::instance().getInputAspectRatio();
    }

    QScreen *currentScreen = this->screen();
    QRect availableGeometry = currentScreen->availableGeometry();
    // Get the available screen width and height
    int availableWidth = availableGeometry.width();
    int availableHeight = availableGeometry.height();
    // Get the current window size
    int currentWidth = this->width();
    int currentHeight = this->height();

    // Calculate the height of the title bar, menu bar, and status bar
    int titleBarHeight = this->frameGeometry().height() - this->geometry().height();
    int menuBarHeight = this->menuBar()->height();
    int statusBarHeight = ui->statusbar->height();

    // Calculate the maximum content height (excluding title bar, menu bar, and status bar)
    int maxContentHeight = availableHeight - titleBarHeight - menuBarHeight - statusBarHeight;

    // Check if the current width or height exceeds the available screen size
    qCDebug(log_ui_mainwindow) << "current height: " << currentHeight << "available height: " << availableHeight;
    qCDebug(log_ui_mainwindow) << "current width: " << currentWidth << "available width: " << currentWidth;

    if (currentWidth >= availableWidth || currentHeight >= availableHeight) {
        // Calculate the new size while maintaining the aspect ratio
        int videoHeight = maxContentHeight;
        int videoWidth = static_cast<int>(videoHeight * aspect_ratio);
        if (currentWidth >= availableWidth) {
            currentWidth = availableWidth;
            videoWidth = currentWidth;
            videoHeight = static_cast<int>(currentWidth / aspect_ratio);
        }
        if (currentHeight >= availableHeight || videoHeight >= maxContentHeight) {
            // Use the maximum content height and adjust the window height accordingly
            videoHeight = maxContentHeight;
            // currentHeight = maxContentHeight + menuBarHeight + statusBarHeight + 6;
            currentHeight = static_cast<int>(availableWidth / aspect_ratio) - menuBarHeight - statusBarHeight;
            qCDebug(log_ui_mainwindow) << "video height: " << videoHeight << "mainwindow height: " << currentHeight;
            videoWidth = static_cast<int>(videoHeight * aspect_ratio);
        }

        // Set the new size of the window
        qCDebug(log_ui_mainwindow) << "Resize to " << currentWidth << "x" << currentHeight;
        qCDebug(log_ui_mainwindow) << "available height: " << availableHeight << "video height: " << videoHeight;
        qCDebug(log_ui_mainwindow) << "video height: "<< videoHeight <<"video width: " << videoWidth;

        if (currentWidth != availableWidth && currentHeight != availableHeight){
            // Resize the window only when it's not maximized
            resize(currentWidth, currentHeight);
        }

        // Calculate the horizontal offset to center the videoPane
        int horizontalOffset = (currentWidth - videoWidth) / 2;

        // Set the minimum size and resize the videoPane
        videoPane->setMinimumSize(videoWidth, videoHeight);
        videoPane->resize(videoWidth, videoHeight);


        // Move the videoPane to the center horizontally
        
        // Resize the scrollArea to match the videoPane size
        scrollArea->resize(videoWidth, videoHeight);
        videoPane->move(horizontalOffset, videoPane->y());
        scrollArea->move(horizontalOffset, videoPane->y());
        GlobalVar::instance().setWinWidth(currentWidth);
        GlobalVar::instance().setWinHeight(currentHeight);
    } else {
        // If the window size is within the available screen size, use the original logic
        qCDebug(log_ui_mainwindow) << "Aspect ratio:" << aspect_ratio << ", Width:" << video_width << "Height:" << video_height;
        qCDebug(log_ui_mainwindow) << "menuBar height:" << menuBarHeight << ", statusbar height:" << statusBarHeight << ", titleBarHeight" << titleBarHeight;

        // Calculate the new height based on the width and the aspect ratio
        int new_height = static_cast<int>(currentWidth / aspect_ratio) + menuBarHeight + statusBarHeight;

        // Set the new size of the window
        qCDebug(log_ui_mainwindow) << "Resize to " << currentWidth << "x" << new_height;
        resize(currentWidth, new_height);

        int contentHeight = this->height() - statusBarHeight - menuBarHeight;
        qCDebug(log_ui_mainwindow) << "this height: "<< this->height();
        videoPane->setMinimumSize(this->width(), contentHeight);
        videoPane->resize(this->width(), contentHeight);
        qCDebug(log_ui_mainwindow) << "video height: "<< contentHeight <<"video width: " << this->width();
        scrollArea->resize(this->width(), contentHeight);
        GlobalVar::instance().setWinWidth(this->width());
        GlobalVar::instance().setWinHeight(this->height());
    }
}

void MainWindow::moveEvent(QMoveEvent *event) {
    // Get the old and new positions
    QPoint oldPos = event->oldPos();
    QPoint newPos = event->pos();
    
    // scrollTimer->start(100);     // problem here
    // Calculate the position delta
    QPoint delta = newPos - oldPos;

    qCDebug(log_ui_mainwindow) << "Window move delta: " << delta;

    // Call the base class implementation
    QWidget::moveEvent(event);

    //calculate_video_position();
}

void MainWindow::calculate_video_position(){
    double aspect_ratio = static_cast<double>(video_width) / video_height;
    if(GlobalVar::instance().getInputAspectRatio()){
        aspect_ratio = GlobalVar::instance().getInputAspectRatio();
    } 

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
        ui->screensaverButton->setChecked(true);
        this->popupMessage("Screensaver activated");
    } else {
        HostManager::getInstance().stopAutoMoveMouse();
        ui->screensaverButton->setChecked(false);
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

void MainWindow::configureSettings() {
    qDebug() << "configureSettings";
    if (!settingDialog){
        qDebug() << "Creating settings dialog";
        settingDialog = new SettingDialog(m_cameraManager, this);

        VideoPage* videoPage = settingDialog->getVideoPage();
        LogPage* logPage = settingDialog->getLogPage();
        connect(logPage, &LogPage::ScreenSaverInhibitedChanged, m_screenSaverManager, &ScreenSaverManager::setScreenSaverInhibited);
        connect(videoPage, &VideoPage::cameraSettingsApplied, m_cameraManager, &CameraManager::loadCameraSettingAndSetCamera);
        // connect(settingDialog, &SettingDialog::cameraSettingsApplied, m_cameraManager, &CameraManager::loadCameraSettingAndSetCamera);
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

    // If the last camera list is not empty, check if available cameras still include the last camera
    if (!m_lastCameraList.isEmpty()) {
        qCDebug(log_ui_mainwindow) << "Checking previously connected cameras...";
        for (const QCameraDevice &camera : m_lastCameraList) {
            qCDebug(log_ui_mainwindow) << "Checking camera: " << camera.description();
            if (!availableCameras.contains(camera)) {
                qCDebug(log_ui_mainwindow) << "Camera disconnected, stopping camera operations...";
                stop();
                m_lastCameraList.clear();
                return;
            }
        }
    }
    qDebug() << "Checking for new cameras...";
    // Check for new cameras
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
            m_cameraManager->setCamera(camera, videoPane);
            // Add the new camera to the last camera list
            m_lastCameraList.append(camera);
            break;
        }
    }
    qDebug() << "Update cameras done.";
}

void MainWindow::onPortConnected(const QString& port, const int& baudrate) {
    if(baudrate > 0){
        m_statusBarManager->setConnectedPort(port, baudrate);
        updateBaudrateMenu(baudrate);
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
    int inputWidth = GlobalVar::instance().getInputWidth();
    int inputHeight = GlobalVar::instance().getInputHeight();
    int captureWidth = GlobalVar::instance().getCaptureWidth();
    int captureHeight = GlobalVar::instance().getCaptureHeight();

    // Calculate aspect ratios
    double inputAspectRatio = static_cast<double>(inputWidth) / inputHeight;
    double captureAspectRatio = static_cast<double>(captureWidth) / captureHeight;

    // Resize the window based on aspect ratios
    if (inputAspectRatio != captureAspectRatio) {
        // Adjust the window size to hide black bars
        int newWidth = static_cast<int>(captureHeight * inputAspectRatio);
        qDebug() << "Resize to " << newWidth << captureHeight;
        resize(newWidth, captureHeight);
    } else {
        // If aspect ratios are the same, just resize normally
        resize(captureWidth + 1, captureHeight + 1);
    }

    // Optionally, you might want to center the window on the screen
    QScreen *screen = this->screen();
    QRect availableGeometry = screen->availableGeometry();
    int x = (availableGeometry.width() - captureWidth) / 2;
    int y = (availableGeometry.height() - captureHeight) / 2;
    move(x, y);
}

void MainWindow::onResolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps, float pixelClk)
{
    m_statusBarManager->setInputResolution(input_width, input_height, input_fps, pixelClk);
    m_statusBarManager->setCaptureResolution(capture_width, capture_height, capture_fps);

    video_height = GlobalVar::instance().getCaptureHeight();
    video_width = GlobalVar::instance().getCaptureWidth();
}

void MainWindow::onInputResolutionChanged(int old_input_width, int old_input_height, int new_input_width, int new_input_height)
{
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
        #ifdef ONLINE_VERSION
            if (senderObj == tcpServer) {
                qCDebug(log_ui_mainwindow) << "run finish: " << runStatus;
                emit emitTCPCommandStatus(runStatus);
            }
        #endif
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
    
    qCDebug(log_ui_mainwindow) << "MainWindow destroyed successfully";
}

bool MainWindow::CheckDeviceAccess(uint16_t vid, uint16_t pid) {
    libusb_context *context;
    int result = libusb_init(&context);
    if (result < 0) return false;
    libusb_device_handle *handle = libusb_open_device_with_vid_pid(context, vid, pid);
    if (!handle) {
        qDebug() << "Failed to open device: " << libusb_error_name(result);
        libusb_close(handle);
        libusb_exit(context);
        return false;
    }

    int r = libusb_claim_interface(handle, 0);
    if (r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_BUSY) {
        qDebug() << "Failed to claim interface: " << libusb_error_name(r);
        libusb_close(handle);
        
        libusb_exit(context);
        return false;
    }
    return true;
    

    libusb_exit(context);
    
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
    ui->virtualKeyboardButton->setIcon(QIcon(iconPath));  // Create QIcon from the path

    

    // Use QTimer to delay the video pane repositioning
    QTimer::singleShot(0, this, &MainWindow::animateVideoPane);
    
}

void MainWindow::centerVideoPane(){
    if (this->width() > videoPane->width()){
        int horizontalOffset = (this->width() - videoPane->width()) / 2;
        scrollArea->move(horizontalOffset, scrollArea->y());
    }
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

void MainWindow::onKeyboardLayoutCombobox_Changed(int index){
    Q_UNUSED(index);
    QString currentLayout = ui->keyboardLayoutComboBox->currentText();
    changeKeyboardLayout(currentLayout);
}

void MainWindow::initializeKeyboardLayouts() {
    // Fetch available layouts from KeyboardLayoutManager
    QStringList layouts = KeyboardLayoutManager::getInstance().getAvailableLayouts();
    qCDebug(log_ui_mainwindow) << "Available layouts:" << layouts;
    
    // Clear existing items in the combo box
    ui->keyboardLayoutComboBox->clear();
    
    // Add fetched layouts to the combo box
    ui->keyboardLayoutComboBox->addItems(layouts);
    
    // Set US QWERTY as default layout if it exists
    
    QString defaultLayout;
    GlobalSetting::instance().getKeyboardLayout(defaultLayout);
    qCDebug(log_ui_mainwindow) << "Read layout" << defaultLayout;
    if (layouts.contains(defaultLayout)) {
        changeKeyboardLayout(defaultLayout);
        ui->keyboardLayoutComboBox->setCurrentText(defaultLayout);
    } else if (!layouts.isEmpty()) {
        // Fallback to the first available layout if US QWERTY is not found
        changeKeyboardLayout(layouts.first());
    }
}

void MainWindow::showEnvironmentSetupDialog() {
    qCDebug(log_ui_mainwindow) << "Show EnvironmentSetupDialog";
    EnvironmentSetupDialog dialog(this);
    dialog.exec();
}

void MainWindow::updateFirmware() {
    // Check if it's latest firmware
    if (VideoHid::getInstance().isLatestFirmware()) {
        std::string currentFirmwareVersion = VideoHid::getInstance().getFirmwareVersion();
        QMessageBox::information(this, tr("Firmware Update"), 
            tr("The firmware is up to date.\nCurrent version: ") + 
            QString::fromStdString(currentFirmwareVersion));
        return;
    }


    std::string currentFirmwareVersion = VideoHid::getInstance().getFirmwareVersion();
    std::string latestFirmwareVersion = VideoHid::getInstance().getLatestFirmwareVersion();
    
    // Create and show the confirmation dialog
    FirmwareUpdateConfirmDialog confirmDialog(this);
    bool proceed = confirmDialog.showConfirmDialog(currentFirmwareVersion, latestFirmwareVersion);

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
}
