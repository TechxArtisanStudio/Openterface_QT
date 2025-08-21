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

#ifndef MULTIMEDIABACKEND_H
#define MULTIMEDIABACKEND_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QGraphicsVideoItem>
#include <QCameraFormat>
#include <QLoggingCategory>
#include <memory>

Q_DECLARE_LOGGING_CATEGORY(log_multimedia_backend)

/**
 * @brief Enumeration of supported multimedia backends
 */
enum class MultimediaBackendType {
    Unknown,
    QtMultimedia,    // Qt's native multimedia backend
    FFmpeg,
    GStreamer
};

/**
 * @brief Configuration parameters for multimedia backend operations
 */
struct MultimediaBackendConfig {
    // Camera setup delays (in milliseconds)
    int cameraInitDelay = 0;
    int deviceSwitchDelay = 0;
    int videoOutputSetupDelay = 0;
    int captureSessionDelay = 0;
    
    // Frame rate handling
    bool useConservativeFrameRates = false;
    bool requireExactFrameRateMatch = false;
    
    // Video output handling
    bool requireVideoOutputReset = false;
    bool useGradualVideoOutputSetup = false;
    
    // Error recovery settings
    int maxRetryAttempts = 1;
    int retryDelay = 100;
    
    // Backend-specific flags
    bool enableVerboseLogging = false;
    bool useStandardFrameRatesOnly = false;
};

/**
 * @brief Abstract base class for multimedia backend handling
 */
class MultimediaBackendHandler : public QObject
{
    Q_OBJECT

public:
    explicit MultimediaBackendHandler(QObject *parent = nullptr);
    virtual ~MultimediaBackendHandler() = default;

    virtual MultimediaBackendType getBackendType() const = 0;
    virtual QString getBackendName() const = 0;
    virtual MultimediaBackendConfig getDefaultConfig() const;

    // Camera lifecycle management
    virtual void prepareCameraCreation(QCamera* oldCamera = nullptr);
    virtual void configureCameraDevice(QCamera* camera, const QCameraDevice& device);
    virtual void setupCaptureSession(QMediaCaptureSession* session, QCamera* camera);
    virtual void prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput);
    virtual void finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput);
    virtual void startCamera(QCamera* camera);
    virtual void stopCamera(QCamera* camera);
    virtual void cleanupCamera(QCamera* camera);

    // Format and frame rate handling
    virtual QList<int> getSupportedFrameRates(const QCameraFormat& format) const;
    virtual bool isFrameRateSupported(const QCameraFormat& format, int frameRate) const;
    virtual QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                            const QSize& resolution, 
                                            int desiredFrameRate,
                                            QVideoFrameFormat::PixelFormat pixelFormat) const;

    // Error handling and recovery
    virtual void handleCameraError(QCamera::Error error, const QString& errorString);
    virtual bool shouldRetryOperation(int attemptCount) const;

signals:
    void backendMessage(const QString& message);
    void backendWarning(const QString& warning);
    void backendError(const QString& error);

protected:
    MultimediaBackendConfig m_config;
    
    void logBackendMessage(const QString& message) const;
    void logBackendWarning(const QString& warning) const;
    void logBackendError(const QString& error) const;
};

/**
 * @brief Factory class for creating multimedia backend handlers
 */
class MultimediaBackendFactory
{
public:
    static MultimediaBackendType detectBackendType();
    static MultimediaBackendType parseBackendType(const QString& backendName);
    static QString backendTypeToString(MultimediaBackendType type);
    
    static std::unique_ptr<MultimediaBackendHandler> createHandler(MultimediaBackendType type, QObject* parent = nullptr);
    static std::unique_ptr<MultimediaBackendHandler> createHandler(const QString& backendName, QObject* parent = nullptr);
    static std::unique_ptr<MultimediaBackendHandler> createAutoDetectedHandler(QObject* parent = nullptr);
};

#endif // MULTIMEDIABACKEND_H
