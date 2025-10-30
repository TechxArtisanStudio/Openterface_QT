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

#ifndef QTBACKENDHANDLER_H
#define QTBACKENDHANDLER_H

#include "../multimediabackend.h"
#include <QObject>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QMediaCaptureSession>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>

/**
 * @brief Windows-specific backend handler using Qt's native multimedia framework
 * 
 * This backend handler provides recording functionality on Windows using Qt's
 * QMediaRecorder instead of FFmpeg or GStreamer, offering a native Windows
 * multimedia experience with the same interface as other backend handlers.
 */
class QtBackendHandler : public MultimediaBackendHandler
{
    Q_OBJECT

public:
    explicit QtBackendHandler(QObject *parent = nullptr);
    ~QtBackendHandler() override;

    // MultimediaBackendHandler interface implementation
    MultimediaBackendType getBackendType() const override;
    QString getBackendName() const override;
    bool isBackendAvailable() const override;
    
    // Camera management (for Windows, these delegate to Qt framework)
    void configureCameraDevice(QCamera* camera, const QCameraDevice& device) override;
    void prepareCameraCreation(QCamera* camera) override;
    void setupCaptureSession(QMediaCaptureSession* session, QCamera* camera) override;
    
    // Format and capability queries
    QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                     const QSize& preferredResolution, 
                                     int preferredFrameRate, 
                                     QVideoFrameFormat::PixelFormat preferredPixelFormat) const override;
    
    QList<int> getSupportedFrameRates(const QCameraFormat& format) const override;
    bool isFrameRateSupported(const QCameraFormat& format, int frameRate) const override;
    int getOptimalFrameRate(const QCameraFormat& format, int desiredFrameRate) const override;
    void validateCameraFormat(const QCameraFormat& format) const override;
    
    // Recording functionality - Windows-specific implementation
    bool startRecording(const QString& outputPath, const QString& format, int videoBitrate) override;
    bool stopRecording() override;
    void pauseRecording() override;
    void resumeRecording() override;
    bool isRecording() const override;
    QString getCurrentRecordingPath() const override;
    qint64 getRecordingDuration() const override;

public slots:
    void setMediaRecorder(QMediaRecorder* recorder);
    void setCaptureSession(QMediaCaptureSession* captureSession);

private slots:
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);
    void onRecorderError(QMediaRecorder::Error error, const QString& errorString);
    void onRecorderDurationChanged(qint64 duration);
    void updateRecordingDuration();

private:
    void setupRecorderConnections();
    void setupRecorderSettings(const QString& outputPath, const QString& format, int videoBitrate);
    QMediaFormat::FileFormat getFileFormatFromString(const QString& format);
    QMediaFormat::VideoCodec getVideoCodecFromFormat(const QString& format);
    
private:
    QMediaRecorder* m_mediaRecorder;          // Reference to the camera manager's recorder
    QMediaCaptureSession* m_captureSession;   // Reference to the camera manager's capture session
    bool m_recordingActive;
    bool m_recordingPaused;
    QString m_currentOutputPath;
    QElapsedTimer m_recordingTimer;
    QTimer* m_durationUpdateTimer;
    qint64 m_recordingStartTime;
    qint64 m_totalPausedDuration;
    qint64 m_lastPauseTime;
};

#endif // QTBACKENDHANDLER_H