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

// Then include your custom headers
#include "../host/audiomanager.h"
#include "ui/statusevents.h"
#include "ui/videopane.h"
#include "ui/toolbar/toggleswitch.h"
#include "ui/preferences/settingdialog.h"
#include "ui/advance/serialportdebugdialog.h"
#include "ui/advance/scripttool.h"
#include "ui/help/versioninfomanager.h"
#include "ui/toolbar/toolbarmanager.h"
#include "ui/TaskManager.h"
#include "ui/languagemanager.h"
#include "ui/statusbar/statusbarmanager.h"
#include "host/usbcontrol.h"
#include "host/cameramanager.h"
#include "scripts/semanticAnalyzer.h"
#include "scripts/AST.h"
#include "ui/languagemanager.h"
#include "ui/screensavermanager.h"


#ifdef ONLINE_VERSION
#define SERVER_PORT 12345
#include "server/tcpServer.h"
#endif


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
#include <QVideoWidget>
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
#include <QPalette>
#include <QStyle>
#include <QEvent>
#include <QScrollArea>
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

QPixmap recolorSvg(const QString &svgPath, const QColor &color, const QSize &size);

class MainWindow : public QMainWindow, public StatusEventCallback
{
    Q_OBJECT

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
    void setTooltip();

    void configureSettings();
    void debugSerialPort();

    void displayCameraError();

    void updateCameraActive(bool active);
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

    void onTargetUsbConnected(const bool isConnected) override;
    
    bool CheckDeviceAccess(uint16_t vid, uint16_t pid);

    void showEnvironmentSetupDialog();

    void updateFirmware(); 

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

    void queryResolutions();

    void onResolutionChange(const int& width, const int& height, const float& fps, const float& pixelClk);

    void onButtonClicked();


private slots:
    void onRepeatingKeystrokeChanged(int index);

    void onCtrlAltDelPressed();
    
    void onBaudrateMenuTriggered(QAction* action);

    void onToggleSwitchStateChanged(int state);

    void onZoomIn();
    void onZoomOut();
    void onZoomReduction();
    void onKeyboardLayoutCombobox_Changed(int index);
    
private slots:
    void checkMousePosition();

private slots:
    void onVideoSettingsChanged();
    void onResolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps, float pixelClk);
    void onInputResolutionChanged(int old_input_width, int old_input_height, int new_input_width, int new_input_height);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Ui::MainWindow *ui;
    AudioManager *m_audioManager;
    VideoPane *videoPane;
    double systemScaleFactor;
    QColor iconColor;
    QScrollArea *scrollArea;

    QStackedLayout *stackedLayout;
    QLabel *mouseLocationLabel;
    QLabel *mouseLabel;
    QLabel *keyPressedLabel;
    QLabel *keyLabel;
    QToolBar *toolbar;
    ToolbarManager *toolbarManager; // Moved up in the declaration orde r
    
    LanguageManager *m_languageManager;
    void updateUI();
    void setupLanguageMenu();
    void onLanguageSelected(QAction *action);

    QMediaDevices m_source;
    QScopedPointer<QImageCapture> m_imageCapture;
    QMediaCaptureSession m_captureSession;
    QScopedPointer<QCamera> m_camera;
    QScopedPointer<QMediaRecorder> m_mediaRecorder;

    bool videoReady = false;
    bool m_isCapturingImage = false;
    bool m_applicationExiting = false;
    bool m_doImageCapture = true;
    int video_width = 1920;
    int video_height = 1080;
    QList<QCameraDevice> m_lastCameraList;

    MetaDataDialog *m_metaDataDialog = nullptr;
    SettingDialog *settingDialog = nullptr;
    SerialPortDebugDialog *serialPortDebugDialog = nullptr;

    QWidget *keyboardPanel = nullptr;

    QPushButton* createFunctionButton(const QString &text);

    void onRepeatingKeystrokeToggled(bool checked);
    QComboBox *repeatingKeystrokeComboBox;
    
    void updateBaudrateMenu(int baudrate);
    ToggleSwitch *toggleSwitch;

    CameraManager *m_cameraManager;
    InputHandler *m_inputHandler;

    void updateScrollbars();
    QPoint lastMousePos;

    double factorScale = 1;
    QTimer *mouseEdgeTimer; // Add this line
    const int edgeThreshold = 50; // Adjust this value as needed
    const int edgeDuration = 125; // Reduced duration for more frequent checks
    const int maxScrollSpeed = 50; // Maximum scroll speed
    VersionInfoManager *m_versionInfoManager;

    StatusBarManager *m_statusBarManager;
    USBControl *usbControl;

    // CameraAdjust *cameraAdjust;
    std::unique_ptr<MouseManager> mouseManager;
    std::unique_ptr<KeyboardMouse> keyboardMouse;
    std::unique_ptr<SemanticAnalyzer> semanticAnalyzer;
    TaskManager* taskmanager;
    void showScriptTool();

    void onToolbarVisibilityChanged(bool visible);

    void animateVideoPane();

    void doResize();

    void centerVideoPane();
    void checkInitSize();
    void fullScreen();
    bool isFullScreenMode();
    bool fullScreenState = false;
    Qt::WindowStates oldWindowState;
    ScriptTool *scriptTool;
    ScreenSaverManager *m_screenSaverManager;
#ifdef ONLINE_VERSION
    void startServer();
    TcpServer *tcpServer;
#endif

};
#endif // MAINWINDOW_H
