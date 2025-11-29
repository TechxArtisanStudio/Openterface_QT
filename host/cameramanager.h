#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QCameraDevice>  // Keep for device enumeration only
#include <QGraphicsVideoItem>
#include <QDir>
#include <QStandardPaths>
#include <QRect>
#include <QList>
#include <QSize>
#include <QVideoFrameFormat>
#include "host/multimediabackend.h"
#include "../device/DeviceInfo.h"

// Forward declarations
class GStreamerBackendHandler;
class FFmpegBackendHandler;
class QtBackendHandler;
class VideoPane;

// Struct to represent a video format key, used for comparing and sorting video formats
// It includes resolution, frame rate range, and pixel format
struct VideoFormatKey {
    QSize resolution;
    int minFrameRate;
    int maxFrameRate;
    QVideoFrameFormat::PixelFormat pixelFormat;

    bool operator<(const VideoFormatKey &other) const {
        if (resolution.width() != other.resolution.width())
            return resolution.width() < other.resolution.width();
        if (resolution.height() != other.resolution.height())
            return resolution.height() < other.resolution.height();
        if (minFrameRate != other.minFrameRate)
            return minFrameRate < other.minFrameRate;
        if (maxFrameRate != other.maxFrameRate)
            return maxFrameRate < other.maxFrameRate;
        return pixelFormat < other.pixelFormat;
    }
};


class CameraManager : public QObject
{
    Q_OBJECT

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager();

    // Camera control - FFmpeg backend only
    void startCamera();
    void stopCamera();
    
    // Image capture - uses FFmpeg backend
    void takeImage(const QString& file);
    void takeAreaImage(const QString& file, const QRect& captureArea);
    
    // Recording - uses FFmpeg backend
    void startRecording();
    void stopRecording();
    void pauseRecording();
    void resumeRecording();
    bool isRecording() const;
    bool isPaused() const;
    
    // Backend handler access
    FFmpegBackendHandler* getFFmpegBackend() const;
    GStreamerBackendHandler* getGStreamerBackend() const;
    MultimediaBackendHandler* getBackendHandler() const;
    
    // Video output management
    void setVideoOutput(QGraphicsVideoItem* videoOutput);
    
    // Platform detection
    static bool isWindowsPlatform();

    // Camera initialization with video output
    bool initializeCameraWithVideoOutput(QGraphicsVideoItem* videoOutput);
    bool initializeCameraWithVideoOutput(VideoPane* videoPane, bool startCapture = true);
    
    // Check if there's an active camera device
    bool hasActiveCameraDevice() const;
    
    // Auto-switch to new device when hotplug event occurs (only if no active device)
    bool tryAutoSwitchToNewDevice(const QString& portChain);
    
    // Get the port chain of the currently active camera device (if any)
    QString getCurrentCameraPortChain() const;
    
    // Deactivate camera if it matches the specified port chain
    bool deactivateCameraByPortChain(const QString& portChain);
    
    // Helper methods to detect current multimedia backend
    bool isGStreamerBackend() const;
    bool isFFmpegBackend() const;
    bool isQtBackend() const;
    
    // Camera device management and switching
    QList<QCameraDevice> getAvailableCameraDevices() const;
    QCameraDevice getCurrentCameraDevice() const;
    bool switchToCameraDevice(const QCameraDevice &cameraDevice, const QString& portChain);
    bool switchToCameraDeviceByPortChain(const QString &portChain);
    bool isCameraDeviceValid(const QCameraDevice& device) const;
    QString getCurrentCameraDeviceId() const;
    QString getCurrentCameraDeviceDescription() const;
    
    // Manual device refresh for Qt 6 compatibility
    void refreshAvailableCameraDevices();
    
    // Video output refresh and hotplug support
    void refreshVideoOutput();
    
signals:
    void cameraActiveChanged(bool active);
    void recordingStarted();
    void recordingStopped();
    void recordingError(const QString &errorString);
    void cameraError(const QString &errorString);
    void resolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps, float pixelClk);
    void imageCaptured(int id, const QImage& img);
    void lastImagePath(const QString& imagePath);
    void cameraDeviceChanged(const QString& newDeviceId, const QString& oldDeviceId);
    void cameraDeviceSwitched(const QString& fromDeviceId, const QString& toDeviceId);
    void cameraDeviceConnected(const QString& deviceId, const QString& portChain);
    void cameraDeviceDisconnected(const QString& deviceId, const QString& portChain);
    void cameraDeviceSwitching(const QString& fromDevice, const QString& toDevice);
    void cameraDeviceSwitchComplete(const QString& device);
    void availableCameraDevicesChanged(int deviceCount);
    void newDeviceAutoConnected(const QCameraDevice& device, const QString& portChain);
    void fpsChanged(double fps);
    
public slots:
    // Note: Automatic device coordination slots have been removed
    // Camera devices are now managed manually through the UI only
    
private slots:
    void onImageCaptured(int id, const QImage& img);

private:
    // Backend handler management
    void initializeBackendHandler();
    void updateBackendHandler();
    
    // Helper method to convert Qt camera device to platform-specific device path
    QString convertCameraDeviceToPath(const QCameraDevice& device) const;

    // Declaration for findMatchingCameraDevice
    QCameraDevice findMatchingCameraDevice(const QString& portChain) const;
    QCameraDevice findCameraByDeviceInfo(const DeviceInfo& deviceInfo) const;
    
    // Helper method for device ID matching
    QString extractShortIdentifier(const QString& fullId) const;

    // Helper method to find the first Qt camera device that looks like an Openterface device
    QCameraDevice findQtOpenterfaceDevice(const QList<QCameraDevice>& devices) const;

    // Helper to determine device path for direct capture (FFmpeg/GStreamer).
    // Returns an empty string on failure. On success, outPortChain will be set
    // to the port chain if available and ok will be set to true.
    QString determineDirectCaptureDevicePath(QString &outPortChain, bool &ok) const;
    
    // FFmpeg backend specific methods
    void handleFFmpegDeviceDisconnection(const QString& devicePath);
    
    // Hotplug monitoring integration
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    void setupWindowsHotplugMonitoring();
    
    // Member variables - FFmpeg backend only
    std::unique_ptr<MultimediaBackendHandler> m_backendHandler;
    QGraphicsVideoItem* m_graphicsVideoOutput;
    int m_video_width;
    int m_video_height;
    QString filePath;

    QRect copyRect;
    
    // Camera device management member variables
    QCameraDevice m_currentCameraDevice;
    QString m_currentCameraDeviceId;
    QString m_currentCameraPortChain;  // Track the port chain of current camera device
    QList<QCameraDevice> m_availableCameraDevices;
    
    // Recording management
    QString m_currentRecordingPath;  // Path to the current recording file
    
private slots:
    void onVideoInputsChanged();
};

#endif // CAMERAMANAGER_H
