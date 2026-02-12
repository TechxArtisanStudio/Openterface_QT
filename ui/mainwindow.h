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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// Include Qt headers first
#include <QtWidgets>
#include <QtMultimedia>
#include <QtMultimediaWidgets>
#include <QDateTime>

// Then include your custom headers
#include "../host/audiomanager.h"
#include "ui/statusevents.h"
#include "ui/videopane.h"
#include "ui/toolbar/toggleswitch.h"
#include "ui/preferences/settingdialog.h"
#include "ui/advance/recordingsettingsdialog.h"
#include "ui/advance/serialportdebugdialog.h"
#include "ui/recording/recordingcontroller.h"
#include "ui/advance/DeviceSelectorDialog.h"
#include "ui/advance/scripttool.h"
#include "ui/advance/firmwaremanagerdialog.h"
#include "ui/advance/updatedisplaysettingsdialog.h"
#include "ui/advance/devicediagnosticsdialog.h"
#include "ui/help/versioninfomanager.h"
#include "ui/toolbar/toolbarmanager.h"
#include "ui/TaskManager.h"
#include "ui/languagemanager.h"
#include "ui/statusbar/statusbarmanager.h"
#include "host/cameramanager.h"
#include "scripts/scriptRunner.h"
#include "scripts/scriptExecutor.h"
#include "scripts/AST.h"
#include "ui/screensavermanager.h"
#include "ui/screenscale.h"
#include "ui/cornerwidget/cornerwidgetmanager.h"
#include "ui/windowcontrolmanager.h"
#include "ui/coordinator/devicecoordinator.h"
#include "ui/coordinator/menucoordinator.h"
#include "ui/coordinator/windowlayoutcoordinator.h"

#include "ui/initializer/mainwindowinitializer.h"

#define SERVER_PORT 12345
#include "server/tcpServer.h"
#include "host/imagecapturer.h"

#include <QAudioInput>
#include <QAudioOutput>
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDecoder>
#include <QCamera>
#include <QImageCapture>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMediaRecorder>
#include <QScopedPointer>
#include <QMainWindow>
#include <QStackedLayout>
#include <QMediaDevices>
#include <QLoggingCategory>
#include <QAudioBuffer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QApplication>
#include <QShortcut>
#include <QPalette>
#include <QStyle>
#include <QEvent>
#include <libusb-1.0/libusb.h>
#include <QMessageBox>


Q_DECLARE_LOGGING_CATEGORY(log_ui_mainwindow)

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class QActionGroup;
QT_END_NAMESPACE

class MetaDataDialog;

#ifdef Q_OS_WIN
class QtBackendHandler;
#endif

QPixmap recolorSvg(const QString &svgPath, const QColor &color, const QSize &size);

enum class ratioType{
    EQUAL,
    LARGER,
    SMALLER
};

class MainWindow : public QMainWindow, public StatusEventCallback
{
    Q_OBJECT
    friend class MainWindowInitializer;

public:
    MainWindow(LanguageManager *languageManager, QWidget *parent = nullptr);
    void calculate_video_position();
    void stop();
    // Add this line to declare the destructor
    ~MainWindow() override;

signals:
    void emitTCPCommandStatus(bool status);
    void emitScriptStatus(bool status);

public slots:
    void handleSyntaxTree(std::shared_ptr<ASTNode> syntaxTree);
    void changeKeyboardLayout(const QString& layout);
    void initializeKeyboardLayouts();
    void onScreenRatioChanged(double ratio);
    void showRecordingSettings();
    void toggleRecording();
    void toggleMute();

private slots:
    void initCamera();

    void record();
    void pause();
    void setMuted(bool);

    void takeImage(const QString& path = "");
    void takeAreaImage(const QString& path, const QRect& captureArea);
    void takeImageDefault();
    void displayCaptureError(int, QImageCapture::Error, const QString &errorString);

    void versionInfo();

    void purchaseLink();
    void feedbackLink();
    void officialLink();
    void aboutLink();
    void updateLink();

    void configureSettings();
    void debugSerialPort();
    void openDeviceSelector();

    void displayCameraError();

    void updateCameraActive(bool active);
    void onDeviceSwitchCompleted();
    void onDeviceSelected(const QString &portChain, bool success, const QString &message);
    void setExposureCompensation(int index);

    void updateRecordTime();

    void processCapturedImage(int requestId, const QImage &img);

    void displayViewfinder();
    void displayCapturedImage();

    void imageSaved(int id, const QString &fileName);

    void updateCameras();

    void popupMessage(QString message);

    void onPortConnected(const QString& port, const int& baudrate) override;

    void onLastKeyPressed(const QString& key) override;

    void onLastMouseLocation(const QPoint& location, const QString& mouseEvent) override;

    void onStatusUpdate(const QString& port) override;

    void onSwitchableUsbToggle(const bool isToHost) override;

    void onKeyStatesChanged(bool numLock, bool capsLock, bool scrollLock) override;

    void factoryReset(bool isStarted) override;

    void serialPortReset(bool isStarted) override;
    
    void onSerialAutoRestart(int attemptNumber, int maxAttempts, double lossRate) override;

    void showEnvironmentSetupDialog();

    void showFirmwareManagerDialog();

    void showUpdateDisplaySettingsDialog();

    void showHardwareDiagnostics();

    void updateFirmware(); 

    void onRepeatingKeystrokeChanged(int index);

    void onCtrlAltDelPressed();
    
    void onArmBaudratePerformanceRecommendation(int currentBaudrate);

    void onToggleSwitchStateChanged(int state);


    void onKeyboardLayoutCombobox_Changed(const QString &layout);
    
    void checkMousePosition();

    void onVideoSettingsChanged();
    void onResolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps, float pixelClk);
    void onInputResolutionChanged();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent *event) override;
   
    void onActionRelativeTriggered();
    void onActionAbsoluteTriggered();

    void onActionMouseAutoHideTriggered();
    void onActionMouseAlwaysShowTriggered();

    void onActionResetHIDTriggered();
    void onActionResetSerialPortTriggered();
    void onActionFactoryResetHIDTriggered();

    void onActionSwitchToHostTriggered();
    void onActionSwitchToTargetTriggered();
    void onActionPasteToTarget();
    void onActionScreensaver();
    void onToggleVirtualKeyboard();

    void onResolutionChange(const int& width, const int& height, const float& fps, const float& pixelClk);
    void onGpio0StatusChanged(bool isToTarget);

    bool eventFilter(QObject *watched, QEvent *event) override;

    friend class MainWindowInitializer;
private:
    Ui::MainWindow *ui;
    AudioManager *m_audioManager;
    VideoPane *videoPane;
    double systemScaleFactor;
    QColor iconColor;

    QStackedLayout *stackedLayout;
    QLabel *mouseLocationLabel;
    QLabel *mouseLabel;
    QLabel *keyPressedLabel;
    QLabel *keyLabel;
    QToolBar *toolbar;
    ToolbarManager *toolbarManager; // Moved up in the declaration orde r
    
    
    void updateUI();

    QMediaDevices m_source;
    QScopedPointer<QImageCapture> m_imageCapture;
    QMediaCaptureSession m_captureSession;
    QScopedPointer<QCamera> m_camera;
    QScopedPointer<QMediaRecorder> m_mediaRecorder;

    bool videoReady = false;
    bool m_isCapturingImage = false;
    bool m_applicationExiting = false;
    bool m_doImageCapture = true;
    bool m_deviceAutoSelected = false; // Flag to prevent multiple auto-selections
    bool m_closeEventHandled = false; // Flag to prevent closeEvent re-entrance
    int video_width = 1920;
    int video_height = 1080;
    QList<QCameraDevice> m_lastCameraList;

    MetaDataDialog *m_metaDataDialog = nullptr;
    SettingDialog *settingDialog = nullptr;
    RecordingSettingsDialog *recordingSettingsDialog = nullptr;
    RecordingController *m_recordingController = nullptr;
    SerialPortDebugDialog *serialPortDebugDialog = nullptr;
    DeviceSelectorDialog *deviceSelectorDialog = nullptr;
    FirmwareManagerDialog *firmwareManagerDialog = nullptr;
    UpdateDisplaySettingsDialog *updateDisplaySettingsDialog = nullptr;

    QWidget *keyboardPanel = nullptr;

    QComboBox *repeatingKeystrokeComboBox;
    
    ToggleSwitch *toggleSwitch;

    CameraManager *m_cameraManager;
    InputHandler *m_inputHandler;
    DeviceCoordinator *m_deviceCoordinator;
    MenuCoordinator *m_menuCoordinator;
    WindowLayoutCoordinator *m_windowLayoutCoordinator;
    MainWindowInitializer *m_initializer;

    void updateScrollbars();
    QPoint lastMousePos;

    double factorScale = 1;
    QTimer *mouseEdgeTimer; // Add this line
    const int edgeThreshold = 50; // Adjust this value as needed
    const int edgeDuration = 125; // Reduced duration for more frequent checks
    const int maxScrollSpeed = 50; // Maximum scroll speed
    VersionInfoManager *m_versionInfoManager;
    LanguageManager *m_languageManager;
    StatusBarManager *m_statusBarManager;

    // CameraAdjust *cameraAdjust;
    std::unique_ptr<MouseManager> mouseManager;
    std::unique_ptr<KeyboardMouse> keyboardMouse;
    std::unique_ptr<ScriptExecutor> scriptExecutor;
    std::unique_ptr<ScriptRunner> scriptRunner;
    TaskManager* taskmanager;
    void showScriptTool();

    void onToolbarVisibilityChanged(bool visible);

    ScriptTool *scriptTool;
    ScreenSaverManager *m_screenSaverManager;
    ScreenScale *m_screenScaleDialog = nullptr;
    CornerWidgetManager *m_cornerWidgetManager = nullptr;
    WindowControlManager *m_windowControlManager = nullptr;
    void configScreenScale();
    
    ratioType currentRatioType = ratioType::EQUAL;
    void startServer();
    TcpServer *tcpServer;
    ImageCapturer *m_imageCapturer;

};
#endif // MAINWINDOW_H
