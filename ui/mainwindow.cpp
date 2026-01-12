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
#include <QStandardPaths>
#include "device/DeviceManager.h"
#include "device/HotplugMonitor.h"
#include "ui/preferences/settingdialog.h"
#include "ui/help/helppane.h"
#include "ui/videopane.h"
#include "video/videohid.h"
#include "ui/help/versioninfomanager.h"
#include "ui/TaskManager.h"
#include "regex/RegularExpression.h"
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

MainWindow::MainWindow(LanguageManager *languageManager, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_audioManager(&AudioManager::getInstance())
    , videoPane(new VideoPane(this))
    , stackedLayout(new QStackedLayout(this))
    , toolbarManager(new ToolbarManager(this))
    , toggleSwitch(new ToggleSwitch(this))
    , m_cameraManager(new CameraManager(this))
    , m_versionInfoManager(new VersionInfoManager(this))
    , m_languageManager(languageManager)
    , m_screenSaverManager(new ScreenSaverManager(this))
    , m_cornerWidgetManager(new CornerWidgetManager(this))
    , m_windowControlManager(nullptr)
    , m_deviceCoordinator(nullptr)
    , m_menuCoordinator(nullptr)
    , m_deviceAutoSelected(false)
    , mouseEdgeTimer(nullptr)
    , taskmanager(TaskManager::instance())
{
    qCDebug(log_ui_mainwindow) << "Initializing MainWindow...";
    
    ui->setupUi(this);
    
    // Initialize WindowLayoutCoordinator early - needed before checkInitSize()
    m_windowLayoutCoordinator = new WindowLayoutCoordinator(this, videoPane, menuBar(), statusBar(), this);
    
    // Delegate all initialization to initializer
    m_initializer = new MainWindowInitializer(this);
    m_initializer->initialize();
    
    // Start VideoHid
    VideoHid::getInstance().start();
    
    qCDebug(log_ui_mainwindow) << "MainWindow initialization complete, window ID:" << this->winId();
}

void MainWindow::startServer(){
    // 1. create and start TCP server
    tcpServer = new TcpServer(this);
    tcpServer->startServer(SERVER_PORT);
    
    // 2. create and initialize ImageCapturer
    m_imageCapturer = new ImageCapturer(this);
    
    // 3. set default save path
    QString savePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/openterface";
    
    // 4. start periodic auto capture (once per second)
    m_imageCapturer->startCapturingAuto(m_cameraManager, tcpServer, savePath, 1);
    
    // 5. establish signal-slot connections
    connect(m_cameraManager, &CameraManager::lastImagePath, tcpServer, &TcpServer::handleImgPath);
    connect(tcpServer, &TcpServer::syntaxTreeReady, this, &MainWindow::handleSyntaxTree);
    connect(this, &MainWindow::emitTCPCommandStatus, tcpServer, &TcpServer::recvTCPCommandStatus);
    
    qCDebug(log_ui_mainwindow) << "TCP Server start at port 12345 with auto image capture";
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

    qCDebug(log_ui_mainwindow) << "ResizeEvent - current window state:" << this->windowState();
    
    // Check if window is maximized - if so, allow resize to proceed
    bool isMaximized = (this->windowState() & Qt::WindowMaximized);
    bool isMinimized = (this->windowState() & Qt::WindowMinimized);
    qCDebug(log_ui_mainwindow) << "ResizeEvent - isMaximized:" << isMaximized;
    qCDebug(log_ui_mainwindow) << "ResizeEvent - isMinimized:" << isMinimized;
    qCDebug(log_ui_mainwindow) << "ResizeEvent - isFullScreenMode:" 
                            << (m_windowLayoutCoordinator ? m_windowLayoutCoordinator->isFullScreenMode() : false);
    
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
    bool isCH32V208 = SerialPortManager::getInstance().isChipTypeCH32V208();
    if(isCH32V208){
        SerialPortManager::getInstance().switchUsbToHostViaSerial();
    }else{
        qCDebug(log_ui_mainwindow) << "Switchable USB to host...";
        VideoHid::getInstance().switchToHost();
        ui->actionTo_Host->setChecked(true);
        ui->actionTo_Target->setChecked(false);
    }

}

void MainWindow::onActionSwitchToTargetTriggered()
{
    bool isCH32V208 = SerialPortManager::getInstance().isChipTypeCH32V208();
    if(isCH32V208){
        SerialPortManager::getInstance().switchUsbToTargetViaSerial();
    }else{
        qCDebug(log_ui_mainwindow) << "Switchable USB to target...";
        VideoHid::getInstance().switchToTarget();
        ui->actionTo_Host->setChecked(false);
        ui->actionTo_Target->setChecked(true);
    }

}

void MainWindow::onToggleSwitchStateChanged(int state)
{
    // Ignore if this change is from a programmatic status update
    if (m_cornerWidgetManager && m_cornerWidgetManager->isUpdatingFromStatus()) {
        return;
    }

    qCDebug(log_ui_mainwindow) << "Toggle switch state changed to:" << state;
    if (state == Qt::Checked) {
        onActionSwitchToTargetTriggered();
    } else {
        onActionSwitchToHostTriggered();
    }
}

void MainWindow::onResolutionChange(const int& width, const int& height, const float& fps, const float& pixelClk)
{
    // Log the resolution information received from the HID device
    // qCDebug(log_ui_mainwindow) << "Resolution received from HID device - Width:" << width 
    //                           << "Height:" << height << "FPS:" << fps 
    //                           << "PixelClock:" << pixelClk << "MHz";
    
    GlobalVar::instance().setInputWidth(width);
    GlobalVar::instance().setInputHeight(height);
    GlobalVar::instance().setInputFps(fps);
    GlobalVar::instance().setCaptureWidth(width);
    GlobalVar::instance().setCaptureHeight(height);
    GlobalVar::instance().setCaptureFps(fps);
    m_statusBarManager->setInputResolution(width, height, fps, pixelClk);
    m_statusBarManager->setCaptureResolution(width, height, fps);
    
    // No popup message for resolution changes
}

void MainWindow::onGpio0StatusChanged(bool isToTarget)
{
    qCDebug(log_ui_mainwindow) << "GPIO0 status changed to:" << (isToTarget ? "target" : "host");
    toggleSwitch->setChecked(isToTarget);
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
    // REMOVED: m_cameraManager->queryResolutions() - no longer needed with FFmpeg backend
}

void MainWindow::onDeviceSwitchCompleted() {
    updateCameraActive(m_cameraManager->hasActiveCameraDevice());
}

void MainWindow::onDeviceSelected(const QString &portChain, bool success, const QString &message) {
    if (!m_cameraManager->hasActiveCameraDevice()) {
        // Try to auto-select the "Openterface" camera if available
        const QList<QCameraDevice> availableCameras = QMediaDevices::videoInputs();
        for (const QCameraDevice &camera : availableCameras) {
            if (camera.description() == "Openterface") {
                qCInfo(log_ui_mainwindow) << "Auto-selecting Openterface camera for device:" << portChain;
                m_cameraManager->switchToCameraDevice(camera, portChain);
                break;
            }
        }
    }
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

void MainWindow::toggleRecording() {
    qDebug() << "toggleRecording called";
    
    // Make sure the camera system is ready and recording controller is initialized
    if (!m_recordingController) {
        qWarning() << "Recording controller is not initialized";
        QMessageBox::critical(this, tr("Recording Error"), 
            tr("Recording system is not initialized. Please restart the application."));
        return;
    }
    
    if (!m_cameraManager) {
        qWarning() << "Camera manager is not initialized";
        QMessageBox::critical(this, tr("Recording Error"), 
            tr("Camera system is not initialized. Please restart the application."));
        return;
    }
    
    if (!m_cameraManager->hasActiveCameraDevice()) {
        qWarning() << "No active camera device available for recording";
        
        // Check if any cameras are available but not active
        if (m_cameraManager->getAvailableCameraDevices().isEmpty()) {
            QMessageBox::information(this, tr("No Camera Available"), 
                tr("No camera devices detected. Please connect a camera and try again."));
        } else {
            QMessageBox::information(this, tr("Camera Not Active"), 
                tr("Camera is not active. Please start the camera preview before recording."));
        }
        return;
    }
    
    try {
        if (m_recordingController->isRecording()) {
            m_recordingController->stopRecording();
        } else {
            m_recordingController->startRecording();
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception during recording operation:" << e.what();
        QMessageBox::critical(this, tr("Recording Error"), 
            tr("An unexpected error occurred: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception during recording operation";
        QMessageBox::critical(this, tr("Recording Error"), 
            tr("An unexpected error occurred. Please try again or restart the application."));
    }
}

void MainWindow::toggleMute() {
    qDebug() << "toggleMute called";
    
    // Get the AudioManager singleton
    AudioManager& audioManager = AudioManager::getInstance();
    
    // Get current volume
    qreal currentVolume = audioManager.getVolume();
    
    // Check if currently muted (volume is 0)
    if (currentVolume == 0.0) {
        // Unmute - restore to default volume (1.0)
        audioManager.setVolume(1.0);
        GlobalSetting::instance().setAudioMuted(false);
        qDebug() << "Audio unmuted - volume set to 1.0";
    } else {
        // Mute - set volume to 0
        audioManager.setVolume(0.0);
        GlobalSetting::instance().setAudioMuted(true);
        qDebug() << "Audio muted - volume set to 0.0";
    }
}

void MainWindow::showRecordingSettings() {
    qDebug() << "showRecordingSettings called";
    
    // Stop any active recording before showing settings
    if (m_recordingController && m_recordingController->isRecording()) {
        QMessageBox::StandardButton response = QMessageBox::question(
            this, 
            tr("Active Recording"),
            tr("There is an active recording session. Do you want to stop it before changing settings?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );
        
        if (response == QMessageBox::Yes) {
            m_recordingController->stopRecording();
        }
    }
    
    if (!recordingSettingsDialog) {
        qDebug() << "Creating recording settings dialog";
        recordingSettingsDialog = new RecordingSettingsDialog(this);
        
        // Get the current backend from camera manager and set it
        MultimediaBackendHandler* backendHandler = m_cameraManager->getBackendHandler();
        if (backendHandler) {
            // Set the backend handler for recording settings
            recordingSettingsDialog->setBackendHandler(backendHandler);
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
    // Use the recording controller if available, otherwise fall back to CameraManager
    if (m_recordingController) {
        m_recordingController->startRecording();
    } else {
        m_cameraManager->startRecording();
    }
}

void MainWindow::pause()
{
    // Use the recording controller if available, otherwise fall back to CameraManager
    if (m_recordingController) {
        if (m_recordingController->isRecording()) {
            if (m_recordingController->isPaused()) {
                m_recordingController->resumeRecording();
            } else {
                m_recordingController->pauseRecording();
            }
        } else {
            m_recordingController->stopRecording();
        }
    } else {
        m_cameraManager->stopRecording();
    }
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
    if (m_applicationExiting) {
        qDebug() << "Image saved during shutdown, quitting application...";
        QApplication::quit();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_isCapturingImage) {
        setEnabled(false);
        m_applicationExiting = true;
        event->ignore();
    } else {
        event->accept();
        // Explicitly quit the application to ensure all threads are properly terminated
        qDebug() << "Close event accepted, quitting application...";
        QApplication::quit();
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
        
        // Auto-select device if there's only one connected and not already auto-selected
        DeviceManager& deviceManager = DeviceManager::getInstance();
        QList<DeviceInfo> devices = deviceManager.getCurrentDevices();
        if (GlobalSetting::instance().getOpenterfacePortChain().isEmpty() && devices.size() == 1 && !m_deviceAutoSelected) {
            const DeviceInfo& device = devices.first();
            qCDebug(log_ui_mainwindow) << "Only one device connected, auto-selecting and starting all components:" << device.getUniqueKey();
            m_deviceAutoSelected = true; // Prevent multiple auto-selections
            
            // Defer the device switching to allow device info to be populated
            // Camera device info may take a moment to be detected and populated
            QTimer::singleShot(500, this, [this, device]() {
                DeviceManager& deviceManager = DeviceManager::getInstance();
                
                // Retry camera switch if it fails (device info may not be ready yet)
                auto attemptCameraSwitch = [this, &device]() -> bool {
                    bool cameraSuccess = m_cameraManager->switchToCameraDeviceByPortChain(device.portChain);
                    if (!cameraSuccess) {
                        qCDebug(log_ui_mainwindow) << "Camera switch failed, will retry...";
                    }
                    return cameraSuccess;
                };
                
                // Switch to the device's HID and audio components first (serial already connected)
                bool hidSuccess = deviceManager.switchHIDDeviceByPortChain(device.portChain);
                bool audioSuccess = deviceManager.switchAudioDeviceByPortChain(device.portChain);
                
                // Try camera switch with retry mechanism
                bool cameraSuccess = attemptCameraSwitch();
                if (!cameraSuccess) {
                    // Retry after another 500ms if camera info wasn't ready
                    // Force device re-discovery to populate camera/audio info
                    QTimer::singleShot(500, this, [this, device, hidSuccess, audioSuccess]() {
                        qCDebug(log_ui_mainwindow) << "Triggering device re-discovery to populate camera info...";
                        DeviceManager::getInstance().forceRefresh();
                        
                        // Give device info time to populate after discovery
                        QTimer::singleShot(300, this, [this, device, hidSuccess, audioSuccess]() {
                            bool cameraSuccess = m_cameraManager->switchToCameraDeviceByPortChain(device.portChain);
                            
                            // If port chain matching still fails, reinitialize and start camera with any Openterface device
                            if (!cameraSuccess) {
                                qCWarning(log_ui_mainwindow) << "Port chain camera switch failed, attempting to reinitialize and start camera...";
                                // Reinitialize with startCapture=true to find device and start capture
                                cameraSuccess = m_cameraManager->initializeCameraWithVideoOutput(videoPane, true);
                                if (cameraSuccess) {
                                    qCDebug(log_ui_mainwindow) << "Camera reinitialized and started successfully (bypassing port chain)";
                                } else {
                                    qCWarning(log_ui_mainwindow) << "Failed to reinitialize camera";
                                }
                            }
                            
                            if (hidSuccess && audioSuccess && cameraSuccess) {
                                qCDebug(log_ui_mainwindow) << "Successfully auto-selected all device components (after retry)";
                            } else {
                                qCWarning(log_ui_mainwindow) << "Failed to auto-select some device components (after retry) - HID:" 
                                                            << hidSuccess << " Audio:" << audioSuccess << " Camera:" << cameraSuccess;
                            }
                        });
                    });
                } else {
                    if (hidSuccess && audioSuccess && cameraSuccess) {
                        qCDebug(log_ui_mainwindow) << "Successfully auto-selected and started device components (HID, audio, camera)";
                    } else {
                        qCWarning(log_ui_mainwindow) << "Failed to auto-select some device components - HID:" 
                                                    << hidSuccess << " Audio:" << audioSuccess << " Camera:" << cameraSuccess;
                    }
                }
            });
        }
        
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
        // Do not start capture again since it's already running with new settings
        bool success = m_cameraManager->initializeCameraWithVideoOutput(videoPane, false);
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
    if (!syntaxTree) {
        qCDebug(log_ui_mainwindow) << "handleSyntaxTree: empty tree";
        emit emitScriptStatus(false);
        return;
    }

    if (!scriptRunner) {
        qCDebug(log_ui_mainwindow) << "No ScriptRunner available";
        emit emitScriptStatus(false);
        return;
    }

    QObject* origin = sender();
    scriptRunner->runTree(std::move(syntaxTree), origin);
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
    
    // No need to process events - deleteLater will handle cleanup asynchronously
    
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
    
    // No need to process events or sleep - let cleanup happen naturally
    // Audio manager uses proper threading internally
    
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
    
    // Clean up image capturer
    if (m_imageCapturer) {
        m_imageCapturer->stopCapturing();
        m_imageCapturer->deleteLater();
        m_imageCapturer = nullptr;
        qCDebug(log_ui_mainwindow) << "m_imageCapturer destroyed successfully";
    }
    
    if (m_audioManager) {
        // AudioManager is now a singleton, and we already disconnected it above
        // Just clear the reference here
        m_audioManager = nullptr;
        qCDebug(log_ui_mainwindow) << "m_audioManager reference cleared successfully (already disconnected)";
    }
    
    // 6. Clean up static instances (but skip AudioManager since it's already stopped)
    if (m_initializer && m_initializer->getHidThread()) {
        m_initializer->getHidThread()->quit();
        m_initializer->getHidThread()->wait(3000); // Wait up to 3 seconds for thread to finish
    }
    VideoHid::getInstance().stop();
    // AudioManager::getInstance().stop(); // Already stopped above to prevent double cleanup
    SerialPortManager::getInstance().stop();
    
    // Delete initializer
    delete m_initializer;
    m_initializer = nullptr;
    
    // 7. Delete UI last
    if (ui) {
        delete ui;
        ui = nullptr;
    }

    qCDebug(log_ui_mainwindow) << "MainWindow destroyed successfully";
}

void MainWindow::onToolbarVisibilityChanged(bool visible) {
    qCDebug(log_ui_mainwindow) << "Toolbar visibility changed:" << visible;
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
    m_cornerWidgetManager->updatePosition(width(), menuBar()->height(), m_windowLayoutCoordinator->isFullScreenMode());
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
