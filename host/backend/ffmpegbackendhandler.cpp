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

#include "ffmpegbackendhandler.h"
#include "../../ui/videopane.h"
#include "global.h"
#include "ui/globalsetting.h"
#include "device/DeviceManager.h"
#include "device/HotplugMonitor.h"
#include "device/DeviceInfo.h"

#include <QThread>
#include <QDebug>
#include <QLoggingCategory>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QGraphicsVideoItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QTimer>
#include <QFileInfo>
#include <QMediaDevices>
#include <QCameraDevice>

// FFmpeg includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

// libjpeg-turbo includes (conditional compilation)
#ifdef HAVE_LIBJPEG_TURBO
#include <turbojpeg.h>
#endif

Q_LOGGING_CATEGORY(log_ffmpeg_backend, "opf.backend.ffmpeg")

// Capture thread extracted to host/backend/ffmpeg/capturethread.h/.cpp
#include "ffmpeg/capturethread.h"
#include "ffmpeg/ffmpeg_hardware_accelerator.h"
#include "ffmpeg/ffmpeg_device_manager.h"
#include "ffmpeg/ffmpeg_frame_processor.h"
#include "ffmpeg/ffmpeg_recorder.h"
#include "ffmpeg/ffmpeg_device_validator.h"
#include "ffmpeg/ffmpeg_hotplug_handler.h"
#include "ffmpeg/ffmpeg_capture_manager.h"

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent), 
    m_packet(nullptr),
    m_deviceManager(std::make_unique<FFmpegDeviceManager>()),
    m_hardwareAccelerator(std::make_unique<FFmpegHardwareAccelerator>()),
    m_frameProcessor(std::make_unique<FFmpegFrameProcessor>()),
    m_recorder(std::make_unique<FFmpegRecorder>()),
    m_deviceValidator(std::make_unique<FFmpegDeviceValidator>()),
    m_hotplugHandler(nullptr),  // Created after validator
    m_captureManager(nullptr),  // Created after dependencies
    m_captureRunning(false),
    m_videoStreamIndex(-1),
    m_frameCount(0),
    m_lastFrameTime(0),
    m_interruptRequested(false),
    m_operationStartTime(0),
    m_suppressErrors(false),
    m_graphicsVideoItem(nullptr),
    m_videoPane(nullptr),
    m_recordingActive(false)
{
    m_config = getDefaultConfig();
    m_preferredHwAccel = GlobalSetting::instance().getHardwareAcceleration();
    
    // Create hotplug handler with device validator
    m_hotplugHandler = std::make_unique<FFmpegHotplugHandler>(m_deviceValidator.get(), this);
    
    // Create capture manager with all its dependencies
    m_captureManager = std::make_unique<FFmpegCaptureManager>(
        m_deviceManager.get(),
        m_hardwareAccelerator.get(),
        m_deviceValidator.get(),
        this
    );
    
    // Connect capture manager signals to backend handler
    connect(m_captureManager.get(), &FFmpegCaptureManager::FrameAvailable,
            this, &FFmpegBackendHandler::processFrame, Qt::DirectConnection);
    
    qCDebug(log_ffmpeg_backend) << "Connected FFmpegCaptureManager::FrameAvailable to processFrame";
    
    connect(m_captureManager.get(), &FFmpegCaptureManager::DeviceDisconnected,
            this, [this]() {
                if (m_hotplugHandler) {
                    QTimer::singleShot(0, this, [this]() {
                        handleDeviceDeactivation(m_currentDevice);
                    });
                }
            });
    
    connect(m_captureManager.get(), &FFmpegCaptureManager::CaptureError,
            this, &FFmpegBackendHandler::captureError);
    
    // Connect hotplug handler signals to backend handler
    connect(m_hotplugHandler.get(), &FFmpegHotplugHandler::DeviceActivated,
            this, [this](const QString& devicePath) {
                // Start capture when device is activated
                QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(0, 0);
                int framerate = m_currentFramerate > 0 ? m_currentFramerate : 0;
                if (startDirectCapture(devicePath, resolution, framerate)) {
                    emit deviceActivated(devicePath);
                } else {
                    emit captureError("Failed to start capture on activated device: " + devicePath);
                }
            });
    
    connect(m_hotplugHandler.get(), &FFmpegHotplugHandler::DeviceDeactivated,
            this, &FFmpegBackendHandler::deviceDeactivated);
    
    connect(m_hotplugHandler.get(), &FFmpegHotplugHandler::WaitingForDevice,
            this, &FFmpegBackendHandler::waitingForDevice);
    
    connect(m_hotplugHandler.get(), &FFmpegHotplugHandler::CaptureError,
            this, &FFmpegBackendHandler::captureError);
    
    connect(m_hotplugHandler.get(), &FFmpegHotplugHandler::RequestStopCapture,
            this, [this]() {
                if (m_captureRunning) {
                    stopDirectCapture();
                }
            });
    
    // Connect to hotplug monitor
    connectToHotplugMonitor();
    
    // Initialize FFmpeg
    if (!initializeFFmpeg()) {
        qCCritical(log_ffmpeg_backend) << "Failed to initialize FFmpeg";
    }
    
    // Setup performance monitoring
    m_performanceTimer = new QTimer(this);
    m_performanceTimer->setInterval(5000); // Report every 5 seconds
    connect(m_performanceTimer, &QTimer::timeout, this, [this]() {
        if (m_frameCount > 0) {
            double fps = m_frameCount / 5.0;
            qCDebug(log_ffmpeg_backend) << QString("FFmpeg capture performance: %1 FPS").arg(fps, 0, 'f', 1);
            emit fpsChanged(fps);
            m_frameCount = 0;
        }
    });
}

FFmpegBackendHandler::~FFmpegBackendHandler()
{
    // Stop recording first if active
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->StopRecording();
    }
    
    // Disconnect from hotplug monitor
    disconnectFromHotplugMonitor();
    
    stopDirectCapture();
    cleanupFFmpeg();
}

MultimediaBackendType FFmpegBackendHandler::getBackendType() const
{
    return MultimediaBackendType::FFmpeg;
}

QString FFmpegBackendHandler::getBackendName() const
{
    return "FFmpeg Direct Capture";
}

QStringList FFmpegBackendHandler::getAvailableHardwareAccelerations() const
{
    QStringList availableHwAccel;
    availableHwAccel << "none";  // CPU option
    availableHwAccel << "auto";  // Always include auto

    // Check for CUDA/NVDEC
    const AVCodec* cudaCodec = avcodec_find_decoder_by_name("mjpeg_cuvid");
    if (cudaCodec) {
        availableHwAccel << "cuda";
    }

    // Check for Intel QSV
    const AVCodec* qsvCodec = avcodec_find_decoder_by_name("mjpeg_qsv");
    if (qsvCodec) {
        availableHwAccel << "qsv";
    }

    // Check for other hardware types if needed
    // For now, focus on CUDA and QSV as they are the main ones for MJPEG

    return availableHwAccel;
}

bool FFmpegBackendHandler::isDirectCaptureRunning() const
{
    return m_captureRunning;
}

MultimediaBackendConfig FFmpegBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig config;
    config.cameraInitDelay = 10;
    config.captureSessionDelay = 10;
    config.useConservativeFrameRates = false;
    config.requireVideoOutputReset = false;
    config.useGradualVideoOutputSetup = false;
    return config;
}

void FFmpegBackendHandler::prepareCameraCreation()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Preparing camera creation";
    stopDirectCapture();
    // QThread::msleep(m_config.deviceSwitchDelay);
}

void FFmpegBackendHandler::configureCameraDevice()
{
    return;
}

void FFmpegBackendHandler::setupCaptureSession(QMediaCaptureSession* session)
{
    // For FFmpeg backend, skip Qt capture session setup to avoid device conflicts
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Skipping Qt capture session setup - using direct capture";
    
    // Do not call session->setCamera(camera) for FFmpeg backend
    // The direct capture will handle video rendering without Qt camera
}

void FFmpegBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Preparing video output connection";
    
    // Check if videoOutput is a VideoPane
    if (VideoPane* videoPane = qobject_cast<VideoPane*>(videoOutput)) {
        setVideoOutput(videoPane);
        qCDebug(log_ffmpeg_backend) << "FFmpeg: Set VideoPane for direct rendering";
        return;
    }
    
    // Check if videoOutput is a QGraphicsVideoItem
    if (QGraphicsVideoItem* graphicsVideoItem = qobject_cast<QGraphicsVideoItem*>(videoOutput)) {
        setVideoOutput(graphicsVideoItem);
        qCDebug(log_ffmpeg_backend) << "FFmpeg: Set graphics video item for direct rendering";
        return;
    }
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Video output type not supported for direct rendering";
}

void FFmpegBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    // For FFmpeg backend, skip Qt video output setup
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Skipping Qt video output setup - using direct rendering";
    
    // Do not call session->setVideoOutput(videoOutput) for FFmpeg backend
    // The direct rendering will handle video display
}

void FFmpegBackendHandler::startCamera()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Starting camera with direct capture";
    
    qCDebug(log_ffmpeg_backend) << "Current device:" << m_currentDevice;
    qCDebug(log_ffmpeg_backend) << "Current resolution:" << m_currentResolution;
    qCDebug(log_ffmpeg_backend) << "Current framerate:" << m_currentFramerate;
    
    // Skip availability check - just try to open the device directly
    // The checkCameraAvailable() opens the device which can interfere with immediate reopening
    // We'll rely on startDirectCapture() to handle device availability
    
    // Use direct FFmpeg capture
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Using direct capture";
    
    // Start direct FFmpeg capture
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(0, 0);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 0;
    
    if (!startDirectCapture(m_currentDevice, resolution, framerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to start FFmpeg direct capture";
        emit captureError("Failed to start FFmpeg video capture");
    } else {
        qCDebug(log_ffmpeg_backend) << "FFmpeg direct capture started successfully";
        emit deviceActivated(m_currentDevice);
    }
}

void FFmpegBackendHandler::stopCamera()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Stopping camera";
    
    // Stop direct capture
    stopDirectCapture();
}

QCameraFormat FFmpegBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                       const QSize& resolution,
                                                       int desiredFrameRate,
                                                       QVideoFrameFormat::PixelFormat pixelFormat) const
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Selecting optimal format with flexible frame rate matching";
    
    // Store resolution and framerate for direct capture
    const_cast<FFmpegBackendHandler*>(this)->m_currentResolution = resolution;
    const_cast<FFmpegBackendHandler*>(this)->m_currentFramerate = desiredFrameRate;
    
    // Return the first available format since we'll handle capture directly
    return formats.isEmpty() ? QCameraFormat() : formats.first();
}

// Direct FFmpeg capture implementation

bool FFmpegBackendHandler::initializeFFmpeg()
{
    qCDebug(log_ffmpeg_backend) << "Initializing FFmpeg";
    
    // Initialize FFmpeg
    av_log_set_level(AV_LOG_WARNING); // Reduce FFmpeg log noise
    avdevice_register_all();
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg initialization completed";
    return true;
}

void FFmpegBackendHandler::cleanupFFmpeg()
{
    qCDebug(log_ffmpeg_backend) << "Cleaning up FFmpeg";
    
    closeInputDevice();
    
    // Cleanup hardware acceleration
    cleanupHardwareAcceleration();
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg cleanup completed";
}

void FFmpegBackendHandler::cleanupFFmpegResources()
{
    closeInputDevice();
}

bool FFmpegBackendHandler::initializeHardwareAcceleration()
{
    if (!m_hardwareAccelerator) {
        qCWarning(log_ffmpeg_backend) << "Hardware accelerator not initialized";
        return false;
    }
    
    return m_hardwareAccelerator->Initialize(m_preferredHwAccel);
}

void FFmpegBackendHandler::updatePreferredHardwareAcceleration()
{
    m_preferredHwAccel = GlobalSetting::instance().getHardwareAcceleration();
    qCDebug(log_ffmpeg_backend) << "Updated preferred hardware acceleration to:" << m_preferredHwAccel;
    
    if (m_hardwareAccelerator) {
        m_hardwareAccelerator->UpdatePreferredAcceleration(m_preferredHwAccel);
    }
}

bool FFmpegBackendHandler::tryHardwareDecoder(const AVCodecParameters* codecpar, 
                                               const AVCodec** outCodec, 
                                               bool* outUsingHwDecoder)
{
    if (!m_hardwareAccelerator) {
        return false;
    }
    
    return m_hardwareAccelerator->TryHardwareDecoder(codecpar, outCodec, outUsingHwDecoder);
}

void FFmpegBackendHandler::cleanupHardwareAcceleration()
{
    if (m_hardwareAccelerator) {
        m_hardwareAccelerator->Cleanup();
    }
}

bool FFmpegBackendHandler::getMaxCameraCapability(const QString& devicePath, CameraCapability& capability)
{
    if (!m_deviceValidator) {
        qCWarning(log_ffmpeg_backend) << "Device validator not initialized";
        return false;
    }
    
    FFmpegDeviceValidator::CameraCapability validatorCap;
    bool result = m_deviceValidator->GetMaxCameraCapability(devicePath, validatorCap);
    
    if (result) {
        capability.resolution = validatorCap.resolution;
        capability.framerate = validatorCap.framerate;
    }
    
    return result;
}

bool FFmpegBackendHandler::startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Capture already running, stopping first";
        stopDirectCapture();
    }
    
    // Set current device
    m_currentDevice = devicePath;
    
    // Apply scaling quality setting to frame processor
    if (m_frameProcessor) {
        QString scalingQuality = GlobalSetting::instance().getScalingQuality();
        m_frameProcessor->SetScalingQuality(scalingQuality);
        m_frameProcessor->StartCapture();
        qCDebug(log_ffmpeg_backend) << "Applied scaling quality:" << scalingQuality;
    }
    
    // Delegate to capture manager
    if (m_captureManager && m_captureManager->StartCapture(devicePath, resolution, framerate)) {
        m_captureRunning = true;
        
        // Notify hotplug handler that capture is running
        if (m_hotplugHandler) {
            m_hotplugHandler->SetCaptureRunning(true);
        }
        
        // Start performance monitoring
        if (m_performanceTimer) {
            m_performanceTimer->start();
        }
        
        qCDebug(log_ffmpeg_backend) << "Direct FFmpeg capture started successfully";
        return true;
    }
    
    qCWarning(log_ffmpeg_backend) << "Failed to start direct FFmpeg capture";
    return false;
}

void FFmpegBackendHandler::stopDirectCapture()
{
    {
        QMutexLocker locker(&m_mutex);
        
        if (!m_captureRunning) {
            return;
        }
        
        qCDebug(log_ffmpeg_backend) << "Stopping direct FFmpeg capture";
        
        m_captureRunning = false;
        
        // Signal frame processor to stop processing
        if (m_frameProcessor) {
            m_frameProcessor->StopCaptureGracefully();
        }
        
        // Notify hotplug handler that capture is stopping
        if (m_hotplugHandler) {
            m_hotplugHandler->SetCaptureRunning(false);
        }
        
        // Delegate to capture manager
        if (m_captureManager) {
            m_captureManager->StopCapture();
        }
    } // Release mutex
    
    // Stop performance monitoring
    if (m_performanceTimer) {
        m_performanceTimer->stop();
    }
    
    qCDebug(log_ffmpeg_backend) << "Direct FFmpeg capture stopped";
}

// Static interrupt callback for FFmpeg operations to prevent blocking
int FFmpegBackendHandler::interruptCallback(void* ctx)
{
    FFmpegBackendHandler* handler = static_cast<FFmpegBackendHandler*>(ctx);
    if (!handler) {
        return 0;
    }
    
    // Check if interrupt was explicitly requested
    if (handler->m_interruptRequested) {
        qCDebug(log_ffmpeg_backend) << "FFmpeg operation interrupted by request";
        return 1; // Interrupt the operation
    }
    
    // Check if operation has timed out
    if (handler->m_operationStartTime > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - handler->m_operationStartTime;
        if (elapsed > FFMPEG_OPERATION_TIMEOUT_MS) {
            qCWarning(log_ffmpeg_backend) << "FFmpeg operation timed out after" << elapsed << "ms";
            return 1; // Interrupt the operation
        }
    }
    
    return 0; // Continue the operation
}

bool FFmpegBackendHandler::openInputDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    if (!m_deviceManager) {
        qCCritical(log_ffmpeg_backend) << "Device manager not initialized";
        return false;
    }
    
    // Reset interrupt state for this new operation
    m_interruptRequested = false;
    m_operationStartTime = QDateTime::currentMSecsSinceEpoch();
    
    // CRITICAL: Initialize hardware acceleration BEFORE opening device
    // This ensures hardware decoders are available when setting up the device
    updatePreferredHardwareAcceleration();
    
    if (m_hardwareAccelerator && !m_hardwareAccelerator->IsHardwareAccelEnabled()) {
        initializeHardwareAcceleration();
    }
    
    if (!m_deviceManager->OpenDevice(devicePath, resolution, framerate, m_hardwareAccelerator.get())) {
        qCWarning(log_ffmpeg_backend) << "Failed to open device via device manager";
        return false;
    }
    
    // Update local video stream index from device manager
    m_videoStreamIndex = m_deviceManager->GetVideoStreamIndex();
    
    // Allocate packet
    m_packet = make_av_packet();
    
    if (!m_packet) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate packet";
        return false;
    }
    
    // Reset operation timer - device opened successfully
    m_operationStartTime = 0;
    
    return true;
}

void FFmpegBackendHandler::closeInputDevice()
{
    // Free packet
    if (m_packet) {
        AV_PACKET_RESET(m_packet);
    }
    
    // Close device via device manager
    if (m_deviceManager) {
        m_deviceManager->CloseDevice();
    }
    
    m_videoStreamIndex = -1;
}

bool FFmpegBackendHandler::readFrame()
{
    // Delegate to capture manager
    if (m_captureManager) {
        return m_captureManager->ReadFrame();
    }
    return false;
}

void FFmpegBackendHandler::processFrame()
{
    // Check if capture is stopping or resources are gone (no lock to avoid blocking)
    AVFormatContext* formatContext = m_deviceManager ? m_deviceManager->GetFormatContext() : nullptr;
    AVCodecContext* codecContext = m_deviceManager ? m_deviceManager->GetCodecContext() : nullptr;
    
    if (!m_captureRunning || !formatContext || !codecContext || !m_frameProcessor) {
        qCWarning(log_ffmpeg_backend) << "processFrame: Early exit - captureRunning:" << m_captureRunning 
                                      << "formatContext:" << (formatContext != nullptr)
                                      << "codecContext:" << (codecContext != nullptr)
                                      << "frameProcessor:" << (m_frameProcessor != nullptr);
        return; // Exit early if capture is stopping or resources are gone
    }

    // Get packet from capture manager
    AVPacket* packet = m_captureManager ? m_captureManager->GetPacket() : nullptr;
    if (!packet) {
        qCWarning(log_ffmpeg_backend) << "processFrame: No packet available from capture manager";
        return;
    }
    
    // Check if recording is active
    bool isRecording = m_recorder && m_recorder->IsRecording() && !m_recorder->IsPaused();
    
    // Get viewport size from VideoPane if available
    QSize viewportSize;
    if (m_videoPane) {
        viewportSize = m_videoPane->viewport()->size();
    }
    
    // QImage is thread-safe and can be passed across thread boundaries efficiently
    QImage image = m_frameProcessor->ProcessPacketToImage(packet, codecContext, isRecording, viewportSize);
    
    // Clean up packet
    av_packet_unref(packet);
    
    if (!image.isNull()) {
        m_frameCount++;
        
        // Log first few frames for debugging
        if (m_frameCount <= 5 || m_frameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Processed frame" << m_frameCount << "size:" << image.size();
        }
        
        // Emit QImage to UI (QueuedConnection ensures thread safety)
        if (m_captureRunning) {
            if (m_frameCount <= 5) {
                qCDebug(log_ffmpeg_backend) << "Emitting frameReadyImage signal for frame" << m_frameCount;
            }
            emit frameReadyImage(image);
        }
        
        // Write frame to recording file if recording is active
        if (m_recorder && m_recorder->IsRecording() && !m_recorder->IsPaused()) {
            // FRAME RATE CONTROL: Only write frames at the target recording framerate
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            
            if (m_recorder->ShouldWriteFrame(currentTime)) {
                // Pass QImage directly to recorder (worker thread safe)
                if (m_recorder->WriteFrame(image)) {
                    // Update recording duration periodically
                    static int recordingFrameCount = 0;
                    if (++recordingFrameCount % 30 == 0) { // Every 30 frames (~1 second at 30fps)
                        emit recordingDurationChanged(m_recorder->GetRecordingDuration());
                    }
                }
            }
        }
        
        // Reduce success logging frequency for performance
        if (m_frameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "frameReady signal emitted successfully for frame" << m_frameCount;
        }
        
        // REMOVED: QCoreApplication::processEvents() - this was causing excessive CPU usage
        // Let Qt's event loop handle frame processing naturally
    } else {
        // Only warn for decode failures on packets with actual data
        if (packet->size > 0) {
            qCWarning(log_ffmpeg_backend) << "Failed to decode frame - pixmap is null";
            qCWarning(log_ffmpeg_backend) << "Frame decode failure details:";
            qCWarning(log_ffmpeg_backend) << "  - Packet size:" << packet->size;
            qCWarning(log_ffmpeg_backend) << "  - Codec ID:" << (codecContext ? codecContext->codec_id : -1);
            qCWarning(log_ffmpeg_backend) << "  - Stream index:" << packet->stream_index;
        }
    }
}

void FFmpegBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
    QMutexLocker locker(&m_mutex);
    
    // Disconnect previous connections
    disconnect(this, &FFmpegBackendHandler::frameReady, this, nullptr);
    disconnect(this, &FFmpegBackendHandler::frameReadyImage, this, nullptr);
    
    m_graphicsVideoItem = videoItem;
    m_videoPane = nullptr;
    
    if (videoItem) {
        qCDebug(log_ffmpeg_backend) << "Graphics video item set for FFmpeg direct rendering";
        
        // Try to find parent VideoPane to make proper connection
        VideoPane* parentVideoPane = nullptr;
        QObject* parent = videoItem->parentObject();
        while (parent) {
            parentVideoPane = qobject_cast<VideoPane*>(parent);
            if (parentVideoPane) {
                break;
            }
            parent = parent->parent();
        }
        
        if (parentVideoPane) {
            qCDebug(log_ffmpeg_backend) << "Found parent VideoPane, connecting frameReadyImage";
            // Connect frameReadyImage to VideoPane's method that handles QGraphicsVideoItem
            connect(this, &FFmpegBackendHandler::frameReadyImage,
                    parentVideoPane, [parentVideoPane, videoItem](const QImage& image) {
                        if (parentVideoPane && videoItem) {
                            parentVideoPane->updateGraphicsVideoItemFromImage(videoItem, image);
                        }
                    }, Qt::QueuedConnection);
        } else {
            qCWarning(log_ffmpeg_backend) << "Could not find parent VideoPane for QGraphicsVideoItem";
            // Fallback: just emit frameReadyImage, UI must connect manually
        }
    }
}

void FFmpegBackendHandler::setVideoOutput(VideoPane* videoPane)
{
    QMutexLocker locker(&m_mutex);
    
    // Disconnect previous connections
    disconnect(this, &FFmpegBackendHandler::frameReady, this, nullptr);
    disconnect(this, &FFmpegBackendHandler::frameReadyImage, this, nullptr);
    
    m_videoPane = videoPane;
    m_graphicsVideoItem = nullptr;
    
    if (videoPane) {
        qCDebug(log_ffmpeg_backend) << "VideoPane set for FFmpeg direct rendering";
        
        // OPTIMIZATION: Connect frameReadyImage signal to receive QImage
        // Use QueuedConnection to ensure thread safety and prevent blocking capture thread
        connect(this, &FFmpegBackendHandler::frameReadyImage,
                videoPane, &VideoPane::updateVideoFrameFromImage,
                Qt::QueuedConnection);
        
        // Connect viewport size changes to update frame scaling
        connect(videoPane, &VideoPane::viewportSizeChanged,
                this, [this](const QSize& size) {
                    qCDebug(log_ffmpeg_backend) << "Viewport size changed to:" << size;
                    // The next frame will automatically use the new viewport size in processFrame
                });
        
        qCDebug(log_ffmpeg_backend) << "Connected frameReadyImage signal to VideoPane::updateVideoFrameFromImage with QueuedConnection";
        
        // Enable direct FFmpeg mode in the VideoPane
        videoPane->enableDirectFFmpegMode(true);
        qCDebug(log_ffmpeg_backend) << "Enabled direct FFmpeg mode in VideoPane";
    }
}

// Device availability and hotplug support methods
bool FFmpegBackendHandler::checkCameraAvailable(const QString& devicePath)
{
    QString device = devicePath.isEmpty() ? m_currentDevice : devicePath;
    
    if (device.isEmpty()) {
        qCDebug(log_ffmpeg_backend) << "No device path provided for availability check";
        return false;
    }
    
    if (!m_deviceValidator) {
        qCWarning(log_ffmpeg_backend) << "Device validator not initialized";
        return false;
    }
    
    bool waitingForDevice = m_hotplugHandler ? m_hotplugHandler->IsWaitingForDevice() : false;
    return m_deviceValidator->CheckCameraAvailable(device, m_currentDevice, 
                                                    m_captureRunning, waitingForDevice);
}

bool FFmpegBackendHandler::isCurrentDeviceAvailable() const
{
    // If capture is currently running, assume device is available to avoid interference
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Capture is running, assuming device is available";
        return true;
    }
    
    return const_cast<FFmpegBackendHandler*>(this)->checkCameraAvailable(m_currentDevice);
}

void FFmpegBackendHandler::handleDeviceDisconnection()
{
    qCDebug(log_ffmpeg_backend) << "Handling device disconnection for FFmpeg backend";
    
    // Stop current capture if running
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Stopping capture due to device disconnection";
        stopDirectCapture();
    }
    
    // Emit device deactivation signal
    emit deviceDeactivated(m_currentDevice);
    
    // Emit error signal to notify UI
    emit captureError("Camera device disconnected: " + m_currentDevice);
    
    qCDebug(log_ffmpeg_backend) << "Device disconnection handled";
}

bool FFmpegBackendHandler::restartCaptureWithDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    qCDebug(log_ffmpeg_backend) << "Attempting to restart capture with device:" << devicePath;
    
    // Stop current capture first
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Stopping current capture before restart";
        stopDirectCapture();
        QThread::msleep(200); // Give time for cleanup
    }
    
    // Check if new device is available
    if (!checkCameraAvailable(devicePath)) {
        qCWarning(log_ffmpeg_backend) << "New device not available for restart:" << devicePath;
        return false;
    }
    
    // Update current device
    m_currentDevice = devicePath;
    m_currentResolution = resolution.isValid() ? resolution : QSize(1920, 1080);
    m_currentFramerate = framerate > 0 ? framerate : 30;
    
    qCDebug(log_ffmpeg_backend) << "Restarting capture with device:" << m_currentDevice
                                << "resolution:" << m_currentResolution 
                                << "framerate:" << m_currentFramerate;
    
    // Start capture with new device
    bool success = startDirectCapture(m_currentDevice, m_currentResolution, m_currentFramerate);
    
    if (success) {
        qCDebug(log_ffmpeg_backend) << "Successfully restarted capture with new device";
        emit deviceActivated(m_currentDevice);
    } else {
        qCWarning(log_ffmpeg_backend) << "Failed to restart capture with new device";
    }
    
    return success;
}

// Enhanced hotplug support methods
void FFmpegBackendHandler::connectToHotplugMonitor()
{
    if (!m_hotplugHandler) {
        qCWarning(log_ffmpeg_backend) << "Hotplug handler not initialized";
        return;
    }
    
    m_hotplugHandler->ConnectToHotplugMonitor();
}

void FFmpegBackendHandler::disconnectFromHotplugMonitor()
{
    if (!m_hotplugHandler) {
        return;
    }
    
    m_hotplugHandler->DisconnectFromHotplugMonitor();
}

void FFmpegBackendHandler::waitForDeviceActivation(const QString& devicePath, int timeoutMs)
{
    if (!m_hotplugHandler) {
        qCWarning(log_ffmpeg_backend) << "Hotplug handler not initialized";
        return;
    }
    
    m_hotplugHandler->WaitForDeviceActivation(devicePath, timeoutMs);
}

void FFmpegBackendHandler::handleDeviceActivation(const QString& devicePath, const QString& portChain)
{
    if (!m_hotplugHandler) {
        qCWarning(log_ffmpeg_backend) << "Hotplug handler not initialized";
        return;
    }
    
    // Update current device and port chain
    if (!devicePath.isEmpty()) {
        m_currentDevice = devicePath;
        m_hotplugHandler->SetCurrentDevice(devicePath);
        m_hotplugHandler->SetCurrentDevicePortChain(portChain);
        m_currentResolution = m_currentResolution.isValid() ? m_currentResolution : QSize(0, 0);
        m_currentFramerate = m_currentFramerate > 0 ? m_currentFramerate : 0;
    }
    
    m_hotplugHandler->HandleDeviceActivation(devicePath, portChain);
}

void FFmpegBackendHandler::handleDeviceDeactivation(const QString& devicePath)
{
    if (!m_hotplugHandler) {
        qCWarning(log_ffmpeg_backend) << "Hotplug handler not initialized";
        return;
    }
    
    m_suppressErrors = true;
    av_log_set_level(AV_LOG_QUIET);
    
    m_hotplugHandler->HandleDeviceDeactivation(devicePath);
    
    // Clear local state after hotplug handler processes deactivation
    {
        QMutexLocker locker(&m_mutex);
        m_currentResolution = QSize();
        m_currentFramerate = 0;
        m_currentDevice.clear();
    }
}

void FFmpegBackendHandler::setCurrentDevicePortChain(const QString& portChain)
{
    if (m_hotplugHandler) {
        m_hotplugHandler->SetCurrentDevicePortChain(portChain);
    }
}

void FFmpegBackendHandler::setCurrentDevice(const QString& devicePath)
{
    m_currentDevice = devicePath;
    if (m_hotplugHandler) {
        m_hotplugHandler->SetCurrentDevice(devicePath);
    }
    qCDebug(log_ffmpeg_backend) << "Set current device to:" << m_currentDevice;
}

// ============================================================================
// Video Recording Implementation - Delegated to FFmpegRecorder
// ============================================================================

bool FFmpegBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
    if (!m_recorder) {
        qCWarning(log_ffmpeg_backend) << "Recorder not initialized";
        emit recordingError("Recorder not initialized");
        return false;
    }
    
    if (!m_captureRunning) {
        qCWarning(log_ffmpeg_backend) << "Cannot start recording: capture is not running";
        emit recordingError("Cannot start recording: capture is not running");
        return false;
    }
    
    // Get current capture resolution and framerate
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(1920, 1080);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 30;
    
    bool success = m_recorder->StartRecording(outputPath, format, videoBitrate, resolution, framerate);
    
    if (success) {
        m_recordingActive = true;
        emit recordingStarted(outputPath);
    } else {
        emit recordingError("Failed to initialize recording");
    }
    
    return success;
}

bool FFmpegBackendHandler::stopRecording()
{
    if (!m_recorder) {
        return false;
    }
    
    bool success = m_recorder->StopRecording();
    
    if (success) {
        m_recordingActive = false;
        emit recordingStopped();
    }
    
    return success;
}

void FFmpegBackendHandler::pauseRecording()
{
    if (m_recorder) {
        m_recorder->PauseRecording();
        emit recordingPaused();
    }
}

void FFmpegBackendHandler::resumeRecording()
{
    if (m_recorder) {
        m_recorder->ResumeRecording();
        emit recordingResumed();
    }
}

bool FFmpegBackendHandler::isRecording() const
{
    return m_recorder ? m_recorder->IsRecording() : false;
}

bool FFmpegBackendHandler::isPaused() const
{
    return m_recorder ? m_recorder->IsPaused() : false;
}

QString FFmpegBackendHandler::getCurrentRecordingPath() const
{
    return m_recorder ? m_recorder->GetCurrentRecordingPath() : QString();
}

qint64 FFmpegBackendHandler::getRecordingDuration() const
{
    return m_recorder ? m_recorder->GetRecordingDuration() : 0;
}

void FFmpegBackendHandler::setRecordingConfig(const RecordingConfig& config)
{
    if (m_recorder) {
        m_recorder->SetRecordingConfig(config);
    }
}

RecordingConfig FFmpegBackendHandler::getRecordingConfig() const
{
    return m_recorder ? m_recorder->GetRecordingConfig() : RecordingConfig();
}

// Advanced recording methods
bool FFmpegBackendHandler::isCameraReady() const
{
    AVFormatContext* formatContext = m_deviceManager ? m_deviceManager->GetFormatContext() : nullptr;
    AVCodecContext* codecContext = m_deviceManager ? m_deviceManager->GetCodecContext() : nullptr;
    return m_captureRunning && formatContext != nullptr && codecContext != nullptr;
}

bool FFmpegBackendHandler::supportsAdvancedRecording() const
{
    return m_recorder ? m_recorder->SupportsAdvancedRecording() : false;
}

bool FFmpegBackendHandler::startRecordingAdvanced(const QString& outputPath, const RecordingConfig& config)
{
    if (!m_recorder) {
        return false;
    }
    
    m_recorder->SetRecordingConfig(config);
    return startRecording(outputPath, config.format, config.video_bitrate);
}

bool FFmpegBackendHandler::forceStopRecording()
{
    if (!m_recorder) {
        return false;
    }
    
    bool success = m_recorder->ForceStopRecording();
    
    if (success) {
        m_recordingActive = false;
        emit recordingStopped();
    }
    
    return success;
}

QString FFmpegBackendHandler::getLastError() const
{
    return m_lastError;
}

bool FFmpegBackendHandler::supportsRecordingStats() const
{
    return m_recorder ? m_recorder->SupportsRecordingStats() : false;
}

qint64 FFmpegBackendHandler::getRecordingFileSize() const
{
    return m_recorder ? m_recorder->GetRecordingFileSize() : 0;
}

void FFmpegBackendHandler::takeImage(const QString& filePath)
{
    if (!m_frameProcessor || !m_recorder) {
        qCWarning(log_ffmpeg_backend) << "Frame processor or recorder not initialized";
        return;
    }
    
    QImage latestImage = m_frameProcessor->GetLatestFrame();
    m_recorder->TakeImage(filePath, latestImage);
}

void FFmpegBackendHandler::takeAreaImage(const QString& filePath, const QRect& captureArea)
{
    if (!m_frameProcessor || !m_recorder) {
        qCWarning(log_ffmpeg_backend) << "Frame processor or recorder not initialized";
        return;
    }
    
    QImage latestImage = m_frameProcessor->GetLatestFrame();
    m_recorder->TakeAreaImage(filePath, latestImage, captureArea);
}


