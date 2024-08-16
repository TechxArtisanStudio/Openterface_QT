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

#ifndef CAMERA_H
#define CAMERA_H

#include "../host/audiomanager.h"
#include "ui/statuswidget.h"
#include "ui/statusevents.h"
#include "ui/videopane.h"

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


Q_DECLARE_LOGGING_CATEGORY(log_ui_mainwindow)

QT_BEGIN_NAMESPACE
namespace Ui {
class Camera;
}
class QActionGroup;
QT_END_NAMESPACE

class MetaDataDialog;


class Camera : public QMainWindow, public StatusEventCallback
{
    Q_OBJECT

public:
    Camera();
    void calculate_video_position();
    void stop();

private slots:
    void init();

    void setCamera(const QCameraDevice &cameraDevice);
    void loadCameraSettingAndSetCamera();

    void record();
    void pause();
    void setMuted(bool);

    void takeImage();
    void displayCaptureError(int, QImageCapture::Error, const QString &errorString);

    void configureSettings();

    void displayCameraError();

    void updateCameraDevice(QAction *action);

    void updateCameraActive(bool active);
    void setExposureCompensation(int index);

    void updateRecordTime();

    void processCapturedImage(int requestId, const QImage &img);

    void displayViewfinder();
    void displayCapturedImage();
    void imageSaved(int id, const QString &fileName);

    void updateCameras();

    void popupMessage(QString message);

    void onPortConnected(const QString& port) override;

    void onLastKeyPressed(const QString& key) override;

    void onLastMouseLocation(const QPoint& location, const QString& mouseEvent) override;

    void onStatusUpdate(const QString& port) override;

    void onSwitchableUsbToggle(const bool isToHost) override;

    void onResolutionChange(const int& width, const int& height, const float& fps) override;

    void onTargetUsbConnected(const bool isConnected) override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent *event) override;

    void checkCameraConnection();
    
    void onActionRelativeTriggered();
    void onActionAbsoluteTriggered();

    void onActionResetHIDTriggered();
    void onActionResetSerialPortTriggered();
    void onActionFactoryResetHIDTriggered();

    void onActionSwitchToHostTriggered();
    void onActionSwitchToTargetTriggered();
    void onFollowSwitchTriggered();

    void queryResolutions();

    void updateResolutions(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps);

    void handlePasteFromHost();

private:
    Ui::Camera *ui;
    AudioManager *m_audioManager;
    VideoPane *videoPane;
    QStackedLayout *stackedLayout;
    //QActionGroup *videoDevicesGroup = nullptr;

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
    StatusWidget *statusWidget;
};

#endif
