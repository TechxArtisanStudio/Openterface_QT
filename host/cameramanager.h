#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QGraphicsVideoItem>  // Add this for graphics-based video display
#include <QDir>
#include <QStandardPaths>
#include <QRect>
#include <QList>
#include <QSize>
#include <QVideoFrameFormat>
#include "host/multimediabackend.h"

// Forward declarations
class GStreamerBackendHandler;
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

    // void setCamera(const QCameraDevice &cameraDevice, QGraphicsVideoItem* videoOutput);
    void setCamera(const QCameraDevice &cameraDevice, QGraphicsVideoItem* videoOutput);
    void setCameraDevice(const QCameraDevice &cameraDevice);
    void startCamera();
    void stopCamera();
    void takeImage(const QString& file);
    void takeAreaImage(const QString& file, const QRect& captureArea);
    void startRecording();
    void stopRecording();
    QCamera* getCamera() const { return m_camera.get(); }
    void setVideoOutput(QGraphicsVideoItem* videoOutput);
    void setCameraFormat(const QCameraFormat &format);
    QCameraFormat getCameraFormat() const;
    QList<QCameraFormat> getCameraFormats() const;
    void queryResolutions();
    void configureResolutionAndFormat();
    std::map<VideoFormatKey, QCameraFormat> getVideoFormatMap();

    // Platform detection - Windows uses direct QCamera approach
    static bool isWindowsPlatform();

    // Camera initialization with video output
    bool initializeCameraWithVideoOutput(QGraphicsVideoItem* videoOutput);
    bool initializeCameraWithVideoOutput(VideoPane* videoPane);
    
    // Check if there's an active camera device
    bool hasActiveCameraDevice() const;
    
    // Auto-switch to new device when hotplug event occurs (only if no active device)
    bool tryAutoSwitchToNewDevice(const QString& portChain);
    
    // Get the port chain of the currently active camera device (if any)
    QString getCurrentCameraPortChain() const;
    
    // Deactivate camera if it matches the specified port chain
    bool deactivateCameraByPortChain(const QString& portChain);

    // Updated method to return supported pixel formats
    QList<QVideoFrameFormat> getSupportedPixelFormats() const;
    QCameraFormat getVideoFormat(const QSize &resolution, int desiredFrameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
    
    // Frame rate handling methods using backend handler
    QList<int> getSupportedFrameRates(const QCameraFormat& format) const;
    bool isFrameRateSupported(const QCameraFormat& format, int frameRate) const;
    int getOptimalFrameRate(int desiredFrameRate) const;
    QList<int> getAllSupportedFrameRates() const;
    void validateCameraFormat(const QCameraFormat& format) const;
    // Helper methods to detect current multimedia backend
    bool isGStreamerBackend() const;
    bool isFFmpegBackend() const;
    
    // Camera device management and switching
    QList<QCameraDevice> getAvailableCameraDevices() const;
    QCameraDevice getCurrentCameraDevice() const;
    bool switchToCameraDevice(const QCameraDevice &cameraDevice);
    bool switchToCameraDevice(const QCameraDevice &cameraDevice, const QString& portChain);
    bool switchToCameraDeviceById(const QString& deviceId);
    QString getCurrentCameraDeviceId() const;
    QString getCurrentCameraDeviceDescription() const;
    
    // Auto-detection methods for available cameras
    QCameraDevice findBestAvailableCamera() const;
    QStringList getAllCameraDescriptions() const;
    
    // Manual device refresh for Qt 6 compatibility
    void refreshAvailableCameraDevices();
    
    // Camera device validation and status
    bool isCameraDeviceValid(const QCameraDevice &cameraDevice) const;
    bool isCameraDeviceAvailable(const QString& deviceId) const;
    QStringList getAvailableCameraDeviceDescriptions() const;
    QStringList getAvailableCameraDeviceIds() const;
    void displayAllCameraDeviceIds() const;
    
    // Video output refresh and hotplug support
    void refreshVideoOutput();
    
    bool switchToCameraDeviceByPortChain(const QString &portChain);
    
signals:
    void cameraActiveChanged(bool active);
    void cameraSettingsApplied();
    void recordingStarted();
    void recordingStopped();
    void cameraError(const QString &errorString);
    void resolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps, float pixelClk);
    void imageCaptured(int id, const QImage& img);
    void lastImagePath(const QString& imagePath);
    void cameraDeviceChanged(const QCameraDevice& newDevice, const QCameraDevice& oldDevice);
    void cameraDeviceSwitched(const QString& fromDeviceId, const QString& toDeviceId);
    void cameraDeviceConnected(const QCameraDevice& device);
    void cameraDeviceDisconnected(const QCameraDevice& device);
    void cameraDeviceSwitching(const QString& fromDevice, const QString& toDevice);
    void cameraDeviceSwitchComplete(const QString& device);
    void availableCameraDevicesChanged(int deviceCount);
    void newDeviceAutoConnected(const QCameraDevice& device, const QString& portChain);
    
public slots:
    // Note: Automatic device coordination slots have been removed
    // Camera devices are now managed manually through the UI only
    
private slots:
    void onImageCaptured(int id, const QImage& img);
    void handleCameraTimeout();

private:
    std::unique_ptr<QCamera> m_camera;
    QMediaCaptureSession m_captureSession;
    std::unique_ptr<QImageCapture> m_imageCapture;
    std::unique_ptr<QMediaRecorder> m_mediaRecorder;
    std::unique_ptr<MultimediaBackendHandler> m_backendHandler;
    QGraphicsVideoItem* m_graphicsVideoOutput;
    int m_video_width;
    int m_video_height;
    QString filePath;
    void setupConnections();

    QRect copyRect;
    std::map<VideoFormatKey, QCameraFormat> videoFormatMap;

    // Camera device management member variables
    QCameraDevice m_currentCameraDevice;
    QString m_currentCameraDeviceId;
    QString m_currentCameraPortChain;  // Track the port chain of current camera device
    QList<QCameraDevice> m_availableCameraDevices;
    
    // Helper method for device ID matching
    QString extractShortIdentifier(const QString& fullId) const;

    // Declaration for findMatchingCameraDevice
    QCameraDevice findMatchingCameraDevice(const QString& portChain) const;

    // Backend handler management
    void initializeBackendHandler();
    void updateBackendHandler();
    
    // FFmpeg backend specific methods
    void handleFFmpegDeviceDisconnection(const QString& devicePath);
    
    // Hotplug monitoring integration
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    void setupWindowsHotplugMonitoring();
    
private slots:
    void onVideoInputsChanged();
};

#endif // CAMERAMANAGER_H
