#include "cameramanager.h"
#include "host/multimediabackend.h"

// Include FFmpeg backend for all platforms (Windows now supported via DirectShow)
#include "host/backend/ffmpegbackendhandler.h"

// Include GStreamer backend for non-Windows platforms only
#ifndef Q_OS_WIN
#include "host/backend/gstreamerbackendhandler.h"
#endif

// Include Qt backend for all platforms
#include "host/backend/qtbackendhandler.h"
#include "host/backend/qtmultimediabackendhandler.h"

#ifdef Q_OS_WIN
#include "host/backend/mf/mfbackendhandler.h"
#endif

#include "ui/videopane.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QDateTime>
#include "global.h"
#include "../ui/globalsetting.h"
#include "../device/DeviceManager.h"
#include "../device/DeviceLifecycleManager.h"
#include "../device/HotplugMonitor.h"
#include "../serial/SerialPortManager.h"
#include <QGraphicsVideoItem>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <functional>
#include <QSet>
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_ui_camera, "opf.ui.camera")
OPF_LOGGING_CATEGORY(log_backend, "opf.backend")

CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_graphicsVideoOutput(nullptr), m_video_width(0), m_video_height(0)
{
    qCDebug(log_ui_camera) << "CameraManager init...";
    
    // Initialize camera device to null state
    m_currentCameraDevice = QCameraDevice();
    m_currentCameraDeviceId.clear();
    m_currentCameraPortChain.clear();
    m_currentRecordingPath.clear();

    initializeBackendHandler();
    // Setup Windows-specific hotplug monitoring
    setupWindowsHotplugMonitoring();

    // Initialize frame timeout timer - repeats every 10s to check if frames are still arriving
    m_frameTimeoutTimer = new QTimer(this);
    m_frameTimeoutTimer->setSingleShot(false);  // Repeating timer
    connect(m_frameTimeoutTimer, &QTimer::timeout, this, [this]() {
        // Check if we've received frames recently and haven't already warned
        // Also trigger if camera was attempted but never started streaming
        bool shouldWarn = !isCameraStreaming() && !m_frameTimeoutWarningShown;
        if (shouldWarn) {
            // HOTPLUG FIX: Instead of just warning, try to restart the camera.
            // After hotplug, the camera may be in a stale state (FFmpeg holding a dead
            // device handle) or the device may not have been ready when the lifecycle
            // tried to restart it. This retry mechanism attempts to recover by:
            // 1. Stopping the stale capture
            // 2. Waiting for the device to be fully released
            // 3. Re-switching to the camera device
            // 4. Restarting capture
            if (!m_currentCameraPortChain.isEmpty()
                && m_hotplugCameraRestartRetries < MAX_HOTPLUG_CAMERA_RESTART_RETRIES) {
                m_hotplugCameraRestartRetries++;
                qCWarning(log_ui_camera) << "Frame timeout: no frames received, attempting camera restart"
                                         << m_hotplugCameraRestartRetries << "/" << MAX_HOTPLUG_CAMERA_RESTART_RETRIES
                                         << "for port chain:" << m_currentCameraPortChain;

                // Stop the stale capture
                stopCamera();

                // Optimized retry delay: 500ms per retry (was 2000ms)
                int retryDelay = m_hotplugCameraRestartRetries * 500;
                QString portChain = m_currentCameraPortChain;
                QTimer::singleShot(retryDelay, this, [this, portChain]() {
                    // Clear stale state
                    m_currentCameraDevice = QCameraDevice();
                    m_currentCameraDeviceId.clear();

                    // HOTPLUG FIX (Linux): Force refresh DeviceManager cache before retrying.
                    DeviceManager::getInstance().invalidateDeviceCache();
                    // Refresh device list and try to re-switch
                    refreshAvailableCameraDevices();
                    bool success = switchToCameraDeviceByPortChain(portChain);
                    if (success) {
                        // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
                        // called it internally. Calling it again causes double-start and UI freeze.
                        qCInfo(log_ui_camera) << "Camera restart succeeded on retry"
                                              << m_hotplugCameraRestartRetries;
                        m_frameTimeoutWarningShown = false;  // Reset so we can detect future failures
                    } else {
                        qCWarning(log_ui_camera) << "Camera restart failed on retry"
                                                  << m_hotplugCameraRestartRetries;
                    }
                });
                return;  // Don't show warning yet, give the retry a chance
            }

            qCWarning(log_ui_camera) << "Frame timeout: no video frames received in last 5 seconds"
                                     << "(retries exhausted:" << m_hotplugCameraRestartRetries << "/"
                                     << MAX_HOTPLUG_CAMERA_RESTART_RETRIES << ")";
            m_frameTimeoutWarningShown = true;
            emit frameTimeout();
        }
    });

    // NOTE: Old hotplug monitor connection disabled — DeviceLifecycleManager handles this now.
    // connectToHotplugMonitor();  // Disabled to avoid clash with MainWindow camera initialization

    // Connect to DeviceLifecycleManager for centralized hotplug management (Phase 4 migration)
    {
        auto& lifecycle = DeviceLifecycleManager::getInstance();

        connect(&lifecycle, &DeviceLifecycleManager::shouldConnectCamera,
            this, [this](const QString& sessionKey, const QString& portChain) {
                // CRITICAL: Use qWarning() for visibility — log_ui_camera category may be filtered
                qWarning() << "[HOTPLUG-CAM] shouldConnectCamera received:"
                           << "session=" << sessionKey << "portChain=" << portChain
                           << "currentPortChain=" << m_currentCameraPortChain
                           << "hasActiveDevice=" << hasActiveCameraDevice()
                           << "isStreaming=" << isCameraStreaming();

                qCInfo(log_ui_camera) << "[Lifecycle] shouldConnectCamera:"
                                      << "session=" << sessionKey << "portChain=" << portChain;

                // If camera is already streaming, just notify success without re-initializing.
                // This prevents a deadlock when the old init path (deferredInitializeCamera)
                // already started the camera before the lifecycle manager's discovery phase runs.
                // The port chain may differ (old path uses serial chain, lifecycle uses companion
                // chain) but the physical camera is the same device.
                if (isCameraStreaming()) {
                    qCInfo(log_ui_camera) << "[Lifecycle] Camera already streaming on"
                                          << m_currentCameraPortChain
                                          << "— skipping re-init for" << portChain;
                    DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                        sessionKey, InterfaceType::Camera);
                    return;
                }

                // HOTPLUG FIX: If camera was already connected to the same port chain (even
                // if not currently streaming — e.g., frames not yet arriving), skip re-init.
                // Without this check, the initial discovery cascade (performInitialDiscovery)
                // triggers shouldConnectCamera even though the camera was already started by
                // the startup auto-select path. switchToCameraDeviceByPortChain returns early
                // for the same device, but startCamera() restarts FFmpeg capture → video glitch.
                // During a REAL hotplug, deactivateCameraByPortChain() clears m_currentCameraPortChain,
                // so this check won't prevent proper reconnection.
                if (!portChain.isEmpty() && !m_currentCameraPortChain.isEmpty()
                    && m_currentCameraPortChain == portChain
                    && hasActiveCameraDevice()) {
                    qWarning() << "[HOTPLUG-CAM] Camera already on same port chain"
                               << portChain << "— skipping restart (hasActiveDevice=" << hasActiveCameraDevice()
                               << "isStreaming=" << isCameraStreaming() << ")";
                    qCInfo(log_ui_camera) << "[Lifecycle] Camera already on same port chain"
                                          << portChain << "— skipping restart";
                    DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                        sessionKey, InterfaceType::Camera);
                    return;
                }

                // HOTPLUG FIX: Only refresh the device list, do NOT call onVideoInputsChanged().
                // onVideoInputsChanged() has auto-switch logic that would start the camera with
                // an empty port chain before the lifecycle-managed switch runs. This double-start
                // causes FFmpeg to stop/restart capture in quick succession, producing visual
                // glitches or a black screen after hotplug.
                refreshAvailableCameraDevices();

                // HOTPLUG FIX (修复十二): Camera restart with retry mechanism.
                // After hotplug, the camera device may not be immediately available:
                // 1. Windows DirectShow needs time to re-enumerate USB video devices
                // 2. DeviceManager may not have cameraDeviceId/cameraDevicePath populated yet
                // 3. FFmpeg may fail to open the device if it's still being initialized
                // HOTFIX (2026-09): Increased delays for USB re-enumeration stability.
                // Camera can disappear briefly during USB re-enumeration (~2s).
                constexpr int MAX_CAMERA_CONNECT_RETRIES = 4;
                std::function<void(int)> tryConnectCamera;
                tryConnectCamera = [this, sessionKey, portChain, &tryConnectCamera](int attempt) {
                    qWarning() << "[HOTPLUG-CAM] Camera connect attempt" << attempt + 1
                               << "/" << MAX_CAMERA_CONNECT_RETRIES
                               << "for portChain=" << portChain;

                    // HOTPLUG FIX (Linux): Invalidate the DeviceManager's device cache.
                    // After hotplug, the /dev/videoN device node number may change (e.g. video0 → video2).
                    // The LinuxDeviceManager uses a 500ms cache which can return stale device paths.
                    // findMatchingCameraDevice() matches by device ID — stale IDs won't match the
                    // new Qt camera devices, causing all 3 matching strategies to fail.
                    // invalidateDeviceCache() clears the platform cache without emitting signals
                    // (safe during hotplug recovery — no re-entrancy risk).
                    DeviceManager::getInstance().invalidateDeviceCache();
                    refreshAvailableCameraDevices();
                    qWarning() << "[HOTPLUG-CAM] Available camera devices:"
                               << m_availableCameraDevices.size();
                    for (const auto& dev : m_availableCameraDevices) {
                        qWarning() << "[HOTPLUG-CAM]   Camera:" << dev.description()
                                   << "ID:" << dev.id();
                    }

                    bool success = switchToCameraDeviceByPortChain(portChain);
                    if (success) {
                        // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
                        // called it internally (line 1307). Calling it again causes a double-start:
                        // "Capture already running, stopping first" → blocks main thread → UI freeze.
                        m_hotplugCameraRestartRetries = 0;
                        qWarning() << "[HOTPLUG-CAM] Camera connected successfully on attempt"
                                   << attempt + 1;
                        qCInfo(log_ui_camera) << "[Lifecycle] Camera connected for session" << sessionKey;
                        DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                            sessionKey, InterfaceType::Camera);
                    } else {
                        qWarning() << "[HOTPLUG-CAM] Camera connect failed on attempt"
                                   << attempt + 1 << "for portChain=" << portChain;
                        if (attempt + 1 < MAX_CAMERA_CONNECT_RETRIES) {
                            // HOTFIX (2026-09): Longer delays for USB stability
                            // 1500ms, 2000ms, 2500ms (was 500ms, 1000ms)
                            int retryDelay = 1500 + (attempt * 500);
                            qWarning() << "[HOTPLUG-CAM] Scheduling camera retry in"
                                       << retryDelay << "ms";
                            QTimer::singleShot(retryDelay, this, [tryConnectCamera, attempt]() {
                                tryConnectCamera(attempt + 1);
                            });
                        } else {
                            qWarning() << "[HOTPLUG-CAM] Camera connect FAILED after all retries"
                                       << "for portChain=" << portChain;
                            // Start frame timeout monitoring as last resort
                            if (m_frameTimeoutTimer && !m_frameTimeoutTimer->isActive()) {
                                m_frameTimeoutWarningShown = false;
                                m_frameTimeoutTimer->start(10000);
                            }
                            DeviceLifecycleManager::getInstance().notifyInterfaceFailed(
                                sessionKey, InterfaceType::Camera,
                                "switchToCameraDeviceByPortChain failed after retries");
                        }
                    }
                };

                // HOTPLUG FIX: Platform-specific initial delay for camera reconnect.
                // Windows DirectShow: 300ms is sufficient (friendly name is stable).
                // Linux V4L2: 1500ms needed — after USB re-enumeration the kernel must
                //   create the /dev/videoN node, udev must apply permissions, and the
                //   V4L2 driver must finish initialization. With 300ms the device is
                //   often not yet ready, causing v4l2-ctl and avformat_open_input to fail.
#ifdef Q_OS_LINUX
                constexpr int kInitialConnectDelayMs = 1500;
#else
                constexpr int kInitialConnectDelayMs = 300;
#endif
                QTimer::singleShot(kInitialConnectDelayMs, this, [tryConnectCamera]() {
                    tryConnectCamera(0);
                });
            });

        connect(&lifecycle, &DeviceLifecycleManager::shouldDisconnectCamera,
            this, [this](const QString& sessionKey) {
                qWarning() << "[HOTPLUG-CAM] shouldDisconnectCamera for session" << sessionKey
                           << "currentPortChain=" << m_currentCameraPortChain;
                qCInfo(log_ui_camera) << "[Lifecycle] shouldDisconnectCamera for session" << sessionKey;
                stopCamera();
                deactivateCameraByPortChain(m_currentCameraPortChain);
                DeviceLifecycleManager::getInstance().notifyInterfaceDisconnected(
                    sessionKey, InterfaceType::Camera);
            });

        qCInfo(log_ui_camera) << "CameraManager connected to DeviceLifecycleManager";
    }

    // HOTPLUG FIX (修复十一): Camera restart after serial watchdog recovery.
    //
    // PROBLEM: When the device is unplugged, the HotplugMonitor may NOT detect the removal
    // (Windows USB enumeration keeps stale entries). The lifecycle manager stays in "Ready"
    // state with camera marked as "Connected" — shouldConnectCamera is never emitted.
    // The serial port recovers through its OWN watchdog path (performRecovery), which does NOT
    // notify the lifecycle manager. Result: serial reconnects but camera never restarts → no video.
    //
    // FIX: Listen for serialPortConnectionSuccess — emitted whenever serial port successfully
    // opens (including watchdog recovery). If we have a saved port chain (from before device
    // removal) and camera is NOT streaming, restart the camera with retry logic.
    //
    // m_lastActiveCameraPortChain is saved in deactivateCameraByPortChain() BEFORE clearing
    // m_currentCameraPortChain. This allows the handler to know which camera to restart
    // even after the device was removed and camera was deactivated.
    connect(&SerialPortManager::getInstance(), &SerialPortManager::serialPortConnectionSuccess,
        this, [this](const QString&) {
            // Use saved port chain (survives deactivation) or current port chain
            QString portChain = m_currentCameraPortChain.isEmpty()
                ? m_lastActiveCameraPortChain
                : m_currentCameraPortChain;

            qWarning() << "[HOTPLUG-CAM][SerialRecovery] serialPortConnectionSuccess received"
                       << "currentPortChain=" << m_currentCameraPortChain
                       << "savedPortChain=" << m_lastActiveCameraPortChain
                       << "usingPortChain=" << portChain
                       << "hasActiveDevice=" << hasActiveCameraDevice()
                       << "isStreaming=" << isCameraStreaming();

            if (portChain.isEmpty()) {
                // No port chain to reconnect to — either initial startup or never had camera.
                return;
            }
            if (m_lastFrameTimestamp <= 0) {
                // No frame has ever arrived, so the initial camera start is still in flight:
                // the --device bind has already set the port chain, and isCameraStreaming() is
                // still false simply because the first frame has not landed yet. Neither guard
                // above holds, so without this the recovery fires against a camera that is
                // merely starting, tears it down a moment after it goes healthy, and then fails
                // to restart it ("No matching camera found"), leaving the camera dead.
                // Recovery restores something that worked; there is nothing to restore yet.
                qCInfo(log_ui_camera) << "[SerialRecovery] Ignoring serial connect before the"
                                      << "first frame — initial camera start is still in progress";
                return;
            }
            if (isCameraStreaming()) {
                // Camera is actively streaming — no restart needed.
                return;
            }

            qWarning() << "[HOTPLUG-CAM][SerialRecovery] Serial reconnected — camera not streaming."
                       << "Scheduling camera restart with retry for port chain:" << portChain;

            // Retry mechanism: try multiple times with increasing delay
            // (DirectShow needs time to re-enumerate after hotplug)
            // HOTFIX (2026-09): Increased delays for USB re-enumeration stability.
            // Openterface camera can disappear briefly during USB re-enumeration (~2s).
            constexpr int MAX_SERIAL_RECOVERY_RETRIES = 4;
            std::function<void(int)> tryRestartCamera;
            tryRestartCamera = [this, portChain, &tryRestartCamera](int attempt) {
                qWarning() << "[HOTPLUG-CAM][SerialRecovery] Camera restart attempt" << attempt + 1
                           << "/" << MAX_SERIAL_RECOVERY_RETRIES << "for portChain=" << portChain;

                if (isCameraStreaming()) {
                    qWarning() << "[HOTPLUG-CAM][SerialRecovery] Camera is now streaming — skipping";
                    return;
                }

                // FIX (修复十五): Check if lifecycle manager already handled the restart.
                // If m_currentCameraPortChain is set to the target port chain, the shouldConnectCamera
                // handler (修复十二) already restarted the camera. Don't double-start.
                if (m_currentCameraPortChain == portChain && hasActiveCameraDevice()) {
                    qWarning() << "[HOTPLUG-CAM][SerialRecovery] Lifecycle already handled restart —"
                               << "currentPortChain=" << m_currentCameraPortChain
                               << "hasActiveDevice=" << hasActiveCameraDevice() << "— skipping";
                    return;
                }

                // HOTPLUG FIX (Linux): Force refresh DeviceManager cache — see tryConnectCamera above.
                DeviceManager::getInstance().invalidateDeviceCache();
                refreshAvailableCameraDevices();
                bool switchSuccess = switchToCameraDeviceByPortChain(portChain);
                if (switchSuccess) {
                    // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
                    // called it internally. Calling it again causes double-start and UI freeze.
                    qWarning() << "[HOTPLUG-CAM][SerialRecovery] Camera restart succeeded on attempt"
                               << attempt + 1;
                } else {
                    qWarning() << "[HOTPLUG-CAM][SerialRecovery] Camera restart failed on attempt"
                               << attempt + 1;
                    if (attempt + 1 < MAX_SERIAL_RECOVERY_RETRIES) {
                        // HOTFIX (2026-09): Longer delays for USB stability
                        // 1500ms, 2000ms, 2500ms (was 500ms, 1000ms, 1500ms)
                        int retryDelay = 1500 + (attempt * 500);
                        qWarning() << "[HOTPLUG-CAM][SerialRecovery] Retrying in" << retryDelay << "ms";
                        QTimer::singleShot(retryDelay, this, [tryRestartCamera, attempt]() {
                            tryRestartCamera(attempt + 1);
                        });
                    } else {
                        qWarning() << "[HOTPLUG-CAM][SerialRecovery] Camera restart FAILED after all retries";
                    }
                }
            };

            // HOTPLUG FIX: Platform-specific initial delay for serial recovery camera restart.
            // Linux needs more time because V4L2 device initialization takes longer after hotplug.
#ifdef Q_OS_LINUX
            constexpr int kSerialRecoveryDelayMs = 1500;
#else
            constexpr int kSerialRecoveryDelayMs = 300;
#endif
            QTimer::singleShot(kSerialRecoveryDelayMs, this, [tryRestartCamera]() {
                tryRestartCamera(0);
            });
        });

    // Initialize available camera devices
    m_availableCameraDevices = getAvailableCameraDevices();
    qCDebug(log_ui_camera) << "Found" << m_availableCameraDevices.size() << "available camera devices";
}

CameraManager::~CameraManager() {
    // Disconnect from hotplug monitoring
    disconnectFromHotplugMonitor();
}

bool CameraManager::isWindowsPlatform()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool CameraManager::isGStreamerBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::GStreamer;
}

bool CameraManager::isFFmpegBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::FFmpeg;
}

#ifdef Q_OS_WIN
bool CameraManager::isQtBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::Qt;
}

bool CameraManager::isMediaFoundationBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::MediaFoundation;
}
#endif

QImage CameraManager::getLatestOriginalFrame()
{
#ifdef Q_OS_WIN
    // Windows: only the FFmpeg backend supports frame retrieval.
    // QtBackendHandler and MfBackendHandler do not implement getLatestOriginalFrame().
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        return ffmpeg->getLatestOriginalFrame();
    }
#else
    // Linux / other platforms: dispatch based on the active backend type.
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        return ffmpeg->getLatestOriginalFrame();
    }
    if (GStreamerBackendHandler* gst = getGStreamerBackend()) {
        return gst->getLatestOriginalFrame();
    }
#endif
    return QImage();
}

FFmpegBackendHandler* CameraManager::getFFmpegBackend() const
{
    // FFmpeg backend now supported on all platforms (Windows via DirectShow)
    if (isFFmpegBackend() && m_backendHandler) {
        try {
            // Use dynamic_cast for safer type checking
            return dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        } catch (const std::exception& e) {
            qCCritical(log_ui_camera) << "Exception during FFmpeg backend cast:" << e.what();
        }
    }
    return nullptr;
}

GStreamerBackendHandler* CameraManager::getGStreamerBackend() const
{
#ifndef Q_OS_WIN
    if (isGStreamerBackend() && m_backendHandler) {
        try {
            return qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
        } catch (const std::exception& e) {
            qCCritical(log_ui_camera) << "Exception during GStreamer backend cast:" << e.what();
        }
    }
#endif
    return nullptr;
}

MultimediaBackendHandler* CameraManager::getBackendHandler() const
{
    return m_backendHandler.get();
}

void CameraManager::initializeBackendHandler()
{
    qCDebug(log_ui_camera) << "Initializing multimedia backend handler";
    try {
        m_backendHandler = MultimediaBackendFactory::createAutoDetectedHandler(this);
        if (m_backendHandler) {
            qCDebug(log_ui_camera) << "Backend handler initialized:" << m_backendHandler->getBackendName();
            qCDebug(log_ui_camera) << "Backend handler type:" << static_cast<int>(m_backendHandler->getBackendType());
            qCDebug(log_ui_camera) << "Backend handler pointer:" << m_backendHandler.get();
            
            // Connect backend signals
            connect(m_backendHandler.get(), &MultimediaBackendHandler::backendMessage,
                    this, [](const QString& message) {
                        qCDebug(log_ui_camera) << "Backend message:" << message;
                    });
            
            connect(m_backendHandler.get(), &MultimediaBackendHandler::backendWarning,
                    this, [](const QString& warning) {
                        qCWarning(log_ui_camera) << "Backend warning:" << warning;
                    });
            
            connect(m_backendHandler.get(), &MultimediaBackendHandler::backendError,
                    this, [this](const QString& error) {
                        qCCritical(log_ui_camera) << "Backend error:" << error;
                        emit cameraError(error);
                    });
            
            // Connect fpsChanged signal from backend to CameraManager
            connect(m_backendHandler.get(), &MultimediaBackendHandler::fpsChanged,
                    this, &CameraManager::fpsChanged);

            // Connect frameReceived signal so backends that render outside
            // the Qt graphics path (e.g. GStreamer via X11 video overlay)
            // can keep the frame-alive timestamp fresh. Without this, the
            // frame-timeout watchdog falsely reports "no video signal".
            connect(m_backendHandler.get(), &MultimediaBackendHandler::frameReceived,
                    this, &CameraManager::onNewVideoFrameReceived, Qt::UniqueConnection);
            
            // Connect FFmpeg-specific signals if this is an FFmpeg backend
            if (auto ffmpegHandler = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                qCDebug(log_ui_camera) << "Setting up FFmpeg backend specific signal connections";
                
                connect(ffmpegHandler, &FFmpegBackendHandler::deviceConnectionChanged,
                        this, [this](const QString& devicePath, bool connected) {
                            qCDebug(log_ui_camera) << "FFmpeg device connection changed:" << devicePath << "connected:" << connected;
                            if (!connected) {
                                // Handle device disconnection
                                qCWarning(log_ui_camera) << "FFmpeg backend reports device disconnected:" << devicePath;
                                // Try to find and switch to an available camera device
                                handleFFmpegDeviceDisconnection(devicePath);
                            }
                        });
                
                // Connect to new enhanced hotplug signals
                connect(ffmpegHandler, &FFmpegBackendHandler::deviceActivated,
                        this, [this](const QString& devicePath) {
                            qCInfo(log_ui_camera) << "FFmpeg device activated:" << devicePath;
                            emit cameraActiveChanged(true);
                            // Reset timeout warning flag and start monitoring
                            // Only start if not already running to avoid resetting on retries
                            if (m_frameTimeoutTimer && !m_frameTimeoutTimer->isActive()) {
                                m_frameTimeoutWarningShown = false;
                                m_frameTimeoutTimer->start(10000);  // Check every 10 seconds
                                qCDebug(log_ui_camera) << "Frame timeout monitoring started (10s interval)";
                            }
                        });
                        
                connect(ffmpegHandler, &FFmpegBackendHandler::deviceDeactivated,
                        this, [this](const QString& devicePath) {
                            qCInfo(log_ui_camera) << "FFmpeg device deactivated:" << devicePath;

                            // HOTPLUG FIX (修复十三): Clear stale camera state when FFmpeg
                            // reports device deactivation (e.g., after I/O errors from unplug).
                            // Without this, m_currentCameraDevice and m_currentCameraPortChain
                            // remain set, causing the shouldConnectCamera handler's "already on
                            // same port chain" check to incorrectly skip the restart when the
                            // device is replugged.
                            qWarning() << "[HOTPLUG-CAM][Deactivate] FFmpeg device deactivated —"
                                       << "clearing camera state. currentPortChain=" << m_currentCameraPortChain
                                       << "hasActiveDevice=" << hasActiveCameraDevice();

                            // Save port chain for recovery before clearing
                            if (!m_currentCameraPortChain.isEmpty()) {
                                m_lastActiveCameraPortChain = m_currentCameraPortChain;
                            }
                            m_currentCameraDevice = QCameraDevice();
                            m_currentCameraDeviceId.clear();
                            m_currentCameraPortChain.clear();

                            emit cameraActiveChanged(false);
                            // Stop frame timeout monitoring
                            if (m_frameTimeoutTimer) {
                                m_frameTimeoutTimer->stop();
                            }
                        });
                        
                connect(ffmpegHandler, &FFmpegBackendHandler::waitingForDevice,
                        this, [this](const QString& devicePath) {
                            qCInfo(log_ui_camera) << "FFmpeg waiting for device:" << devicePath;
                            emit cameraActiveChanged(false);
                        });
                
                connect(ffmpegHandler, &FFmpegBackendHandler::captureError,
                        this, [this](const QString& error) {
                            qCWarning(log_ui_camera) << "FFmpeg capture error:" << error;

                            // HOTPLUG FIX (修复十三): Clear stale camera state on capture error.
                            // After consecutive I/O failures (device unplugged), the camera state
                            // must be cleared so the shouldConnectCamera handler won't incorrectly
                            // skip the restart when the device is replugged.
                            qWarning() << "[HOTPLUG-CAM][CaptureError] FFmpeg capture error —"
                                       << "clearing camera state. currentPortChain=" << m_currentCameraPortChain;

                            if (!m_currentCameraPortChain.isEmpty()) {
                                m_lastActiveCameraPortChain = m_currentCameraPortChain;
                            }
                            m_currentCameraDevice = QCameraDevice();
                            m_currentCameraDeviceId.clear();
                            m_currentCameraPortChain.clear();

                            emit cameraActiveChanged(false);
                            emit cameraError("FFmpeg: " + error);
                        });

                qCDebug(log_ui_camera) << "FFmpeg backend signal connections established";
            }
            
            // Qt backend setup - no longer needed for FFmpeg-only approach
#ifdef Q_OS_WIN
            qCDebug(log_ui_camera) << "Windows platform - using FFmpeg backend";
#endif
        } else {
            qCCritical(log_ui_camera) << "Failed to create backend handler - returned nullptr";
        }
    } catch (const std::exception& e) {
        qCCritical(log_ui_camera) << "Exception initializing backend handler:" << e.what();
    } catch (...) {
        qCCritical(log_ui_camera) << "Unknown exception initializing backend handler";
    }
}

void CameraManager::updateBackendHandler()
{
    qCDebug(log_ui_camera) << "Updating multimedia backend handler";
    
    // Store the current backend type for comparison
    MultimediaBackendType currentType = m_backendHandler ? m_backendHandler->getBackendType() : MultimediaBackendType::Unknown;
    MultimediaBackendType newType = MultimediaBackendFactory::detectBackendType();
    
    // Only recreate if the backend type has changed
    if (currentType != newType) {
        qCDebug(log_ui_camera) << "Backend type changed from" << MultimediaBackendFactory::backendTypeToString(currentType)
                               << "to" << MultimediaBackendFactory::backendTypeToString(newType);
        
        // Disconnect old handler signals
        if (m_backendHandler) {
            disconnect(m_backendHandler.get(), nullptr, this, nullptr);
        }
        
        // Create new handler
        initializeBackendHandler();
    } else {
        qCDebug(log_ui_camera) << "Backend type unchanged, keeping current handler";
    }
}

// Deprecated method for initializing camera with video output
// This method is kept for compatibility but should be replaced with the new methods
// that handle port chain tracking and improved device management
void CameraManager::setVideoOutput(QGraphicsVideoItem* videoOutput)
{
    if (videoOutput) {
        m_graphicsVideoOutput = videoOutput;
        
        // Connect video output to FFmpeg backend if available
        if (m_backendHandler && isFFmpegBackend()) {
            FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
            if (ffmpeg) {
                ffmpeg->setVideoOutput(videoOutput);
            }
        } else {
        }

#ifndef Q_OS_WIN
        if (m_backendHandler && isGStreamerBackend()) {
            GStreamerBackendHandler* gst = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
            if (gst) {
                gst->setVideoOutput(videoOutput);
            }
        }
#endif

#ifdef Q_OS_WIN
        if (m_backendHandler && isMediaFoundationBackend()) {
            MfBackendHandler* mf = qobject_cast<MfBackendHandler*>(m_backendHandler.get());
            if (mf) {
                mf->setVideoOutput(videoOutput);
            }
        }
#endif
    } else {
    }
}

void CameraManager::startCamera()
{
    qCDebug(log_ui_camera) << "Starting camera with multimedia backend";
    
    try {
        // FFmpeg backend only - no QCamera
        if (!m_backendHandler) {
            qCWarning(log_ui_camera) << "No backend handler available, cannot start camera";
            return;
        }
        
#ifdef Q_OS_WIN
        // On Windows builds, FFmpeg and Media Foundation backends are supported.
        // For Linux build, both FFmpeg and GStreamer backends are supported.
        if (!isFFmpegBackend() && !isMediaFoundationBackend()) {
            qCWarning(log_backend) << "Only FFmpeg and Media Foundation backends are supported on Windows";
            return;
        }
#endif
        
        // Start backend camera
        m_backendHandler->startCamera();

        // Start frame timeout monitoring - will warn if no frames arrive
        // Only start if not already running to avoid resetting on retries
        if (m_frameTimeoutTimer && !m_frameTimeoutTimer->isActive()) {
            m_frameTimeoutWarningShown = false;
            m_frameTimeoutTimer->start(10000);  // Check every 10 seconds
            qCDebug(log_ui_camera) << "Frame timeout monitoring started (10s interval)";
        }

        // FFmpeg reports real readiness asynchronously via deviceActivated.
        // Avoid optimistic success state that can produce a black screen with "active=true".
        if (!isFFmpegBackend()) {
            emit cameraActiveChanged(true);
            qCDebug(log_backend) << "Camera started successfully";
        } else {
            qCDebug(log_backend) << "FFmpeg start requested, waiting for device activation signal";
        }
        
    } catch (const std::exception& e) {
        qCritical() << "Exception starting camera:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception starting camera";
    }
}

void CameraManager::stopCamera()
{
    qCDebug(log_ui_camera) << "Stopping camera with FFmpeg backend";

    // Stop frame timeout monitoring
    if (m_frameTimeoutTimer) {
        m_frameTimeoutTimer->stop();
    }

    try {
        if (m_backendHandler) {
            qCDebug(log_ui_camera) << "Stopping FFmpeg backend camera";
            m_backendHandler->stopCamera();
            emit cameraActiveChanged(false);
            qCDebug(log_ui_camera) << "FFmpeg backend camera stopped successfully";
        } else {
            qCWarning(log_ui_camera) << "No backend handler available";
        }

    } catch (const std::exception& e) {
        qCritical() << "Exception stopping camera:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception stopping camera";
    }
}

void CameraManager::onImageCaptured(int id, const QImage& img){
    Q_UNUSED(id);
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString customFolderPath;
    if (picturesPath.isEmpty()) {
        picturesPath = QDir::currentPath();
    }
    if(filePath==""){
        customFolderPath = picturesPath + "/" + "openterfaceCaptureImg";
    }else{
        customFolderPath = filePath + "/";
        customFolderPath = customFolderPath.trimmed();
    }
    
    QDir dir(customFolderPath);
    if (!dir.exists() && filePath=="") {
        qCDebug(log_ui_camera) << "Directory do not exist";
        if (!dir.mkpath(".")) {
            qCDebug(log_ui_camera) << "Failed to create directory: " << customFolderPath;
            return;
        }
    }
    
    QString saveName = customFolderPath + "/" + timestamp + ".jpg";

    QImage coayImage = img.copy(copyRect);
    if(coayImage.save(saveName, "JPG", 90)){
        qCDebug(log_ui_camera) << "succefully save img to : " << saveName;
        emit lastImagePath(saveName);
    }else{
        qCDebug(log_ui_camera) << "fail save img to : " << saveName;
    }
    copyRect = QRect(0, 0, m_video_width, m_video_height);
}

void CameraManager::onFFmpegCaptureError(const QString& error) {
    qCWarning(log_ui_camera) << "FFmpeg capture error:" << error;

    // HOTPLUG FIX (修复十三): Clear stale camera state on capture error.
    // Defense in depth — the lambda in setupWindowsFFmpegConnections also clears,
    // but this slot is connected separately via Qt::UniqueConnection.
    if (!m_currentCameraPortChain.isEmpty()) {
        qWarning() << "[HOTPLUG-CAM][CaptureError-Slot] Clearing camera state. portChain=" << m_currentCameraPortChain;
        m_lastActiveCameraPortChain = m_currentCameraPortChain;
    }
    m_currentCameraDevice = QCameraDevice();
    m_currentCameraDeviceId.clear();
    m_currentCameraPortChain.clear();

    emit cameraActiveChanged(false);
    emit cameraError(error);
}

void CameraManager::onMediaFoundationError(const QString& error) {
    qCWarning(log_ui_camera) << "Media Foundation backend error:" << error;
    emit cameraError(error);
}

void CameraManager::takeImage(const QString& file)
{
    if (!m_backendHandler) {
        qCWarning(log_ui_camera) << "Backend handler not initialized";
        return;
    }
    
    // Support both FFmpeg and GStreamer backends
    if (isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
        if (ffmpeg) {
            QString actualFile = file;
            if (actualFile.isEmpty()) {
                // Generate path like original Qt backend
                QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QString customFolderPath;
                if (picturesPath.isEmpty()) {
                    customFolderPath = QDir::homePath() + "/Pictures";
                } else {
                    customFolderPath = picturesPath + "/openterface";
                }
                QDir dir(customFolderPath);
                if (!dir.exists() && !dir.mkpath(customFolderPath)) {
                    qCWarning(log_ui_camera) << "Failed to create directory:" << customFolderPath;
                    return;
                }
                actualFile = customFolderPath + "/" + timestamp + ".jpg";
            }
            ffmpeg->takeImage(actualFile);
            emit lastImagePath(actualFile);
        }
#ifndef Q_OS_WIN
    } else if (isGStreamerBackend()) {
        GStreamerBackendHandler* gstreamer = dynamic_cast<GStreamerBackendHandler*>(m_backendHandler.get());
        if (gstreamer) {
            QString actualFile = file;
            if (actualFile.isEmpty()) {
                // Generate path like original Qt backend
                QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QString customFolderPath;
                if (picturesPath.isEmpty()) {
                    customFolderPath = QDir::homePath() + "/Pictures";
                } else {
                    customFolderPath = picturesPath + "/openterface";
                }
                QDir dir(customFolderPath);
                if (!dir.exists() && !dir.mkpath(customFolderPath)) {
                    qCWarning(log_ui_camera) << "Failed to create directory:" << customFolderPath;
                    return;
                }
                actualFile = customFolderPath + "/" + timestamp + ".jpg";
            }
            gstreamer->takeImage(actualFile);
            emit lastImagePath(actualFile);
        }
#endif
    } else {
        qCWarning(log_ui_camera) << "Image capture not supported for current backend";
    }
}

void CameraManager::takeAreaImage(const QString& file, const QRect& captureArea)
{
    if (!m_backendHandler) {
        qCWarning(log_ui_camera) << "Backend handler not initialized";
        return;
    }
    
    // Support both FFmpeg and GStreamer backends
    if (isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
        if (ffmpeg) {
            QString actualFile = file;
            if (actualFile.isEmpty()) {
                // Generate path like original Qt backend
                QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QString customFolderPath;
                if (picturesPath.isEmpty()) {
                    customFolderPath = QDir::homePath() + "/Pictures";
                } else {
                    customFolderPath = picturesPath + "/openterface";
                }
                QDir dir(customFolderPath);
                if (!dir.exists() && !dir.mkpath(customFolderPath)) {
                    qCWarning(log_ui_camera) << "Failed to create directory:" << customFolderPath;
                    return;
                }
                actualFile = customFolderPath + "/" + timestamp + ".jpg";
            }
            ffmpeg->takeAreaImage(actualFile, captureArea);
            emit lastImagePath(actualFile);
        }
#ifndef Q_OS_WIN
    } else if (isGStreamerBackend()) {
        GStreamerBackendHandler* gstreamer = dynamic_cast<GStreamerBackendHandler*>(m_backendHandler.get());
        if (gstreamer) {
            QString actualFile = file;
            if (actualFile.isEmpty()) {
                // Generate path like original Qt backend
                QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QString customFolderPath;
                if (picturesPath.isEmpty()) {
                    customFolderPath = QDir::homePath() + "/Pictures";
                } else {
                    customFolderPath = picturesPath + "/openterface";
                }
                QDir dir(customFolderPath);
                if (!dir.exists() && !dir.mkpath(customFolderPath)) {
                    qCWarning(log_ui_camera) << "Failed to create directory:" << customFolderPath;
                    return;
                }
                actualFile = customFolderPath + "/" + timestamp + ".jpg";
            }
            gstreamer->takeAreaImage(actualFile, captureArea);
            emit lastImagePath(actualFile);
        }
#endif
    } else {
        qCWarning(log_ui_camera) << "Area image capture not supported for current backend";
    }
}

void CameraManager::startRecording()
{
    qCInfo(log_ui_camera) << "=== START RECORDING (FFmpeg Backend) ===";
    
    // Check if recording is already in progress
    if (isRecording()) {
        qCWarning(log_ui_camera) << "Recording already in progress";
        return;
    }
    
    // Check FFmpeg backend availability
    if (!m_backendHandler || !isFFmpegBackend()) {
        qCWarning(log_ui_camera) << "FFmpeg backend not available for recording";
        emit recordingError("FFmpeg backend not available");
        return;
    }
    
    FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
    if (!ffmpeg) {
        qCWarning(log_ui_camera) << "Failed to get FFmpeg backend handler";
        emit recordingError("FFmpeg backend not initialized");
        return;
    }
    
    // Generate output path with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picturesPath.isEmpty()) {
        picturesPath = QDir::currentPath();
    }
    QString customFolderPath = picturesPath + "/openterfaceRecordings";
    QString outputPath = customFolderPath + "/recording_" + timestamp + ".mp4";
    
    // Ensure output directory exists
    QFileInfo fileInfo(outputPath);
    QDir outputDir = fileInfo.dir();
    if (!outputDir.exists()) {
        if (!outputDir.mkpath(".")) {
            qCWarning(log_ui_camera) << "Failed to create output directory:" << outputDir.absolutePath();
            emit recordingError("Failed to create output directory");
            return;
        }
    }
    
    // Get recording settings
    QString format = GlobalSetting::instance().getRecordingOutputFormat();
    int bitrate = GlobalSetting::instance().getRecordingVideoBitrate();
    
    qCInfo(log_ui_camera) << "Starting recording to:" << outputPath 
                          << "Format:" << format << "Bitrate:" << bitrate;
    
    // Start FFmpeg recording
    bool success = ffmpeg->startRecording(outputPath, format, bitrate);
    
    if (success) {
        m_currentRecordingPath = outputPath;
        qCInfo(log_ui_camera) << "=== RECORDING STARTED SUCCESSFULLY ===";
        emit recordingStarted();
    } else {
        qCWarning(log_ui_camera) << "=== RECORDING START FAILED ===";
        emit recordingError("Failed to start FFmpeg recording");
    }
}

void CameraManager::stopRecording()
{
    qCInfo(log_ui_camera) << "=== STOP RECORDING PROCESS INITIATED ===";
    
    // Check if we're actually recording before attempting to stop
    if (!isRecording()) {
        qCWarning(log_ui_camera) << "No active recording to stop";
        qCDebug(log_ui_camera) << "=== STOP RECORDING ABORTED - NOT RECORDING ===";
        emit recordingStopped(); // Emit signal to ensure UI stays in sync
        return;
    }
    
    QString recordingPath = m_currentRecordingPath;
    qCDebug(log_ui_camera) << "Stopping recording:" << recordingPath;
    qCDebug(log_ui_camera) << "Backend type: " << (m_backendHandler ? 
                                                static_cast<int>(m_backendHandler->getBackendType()) : -1);

    // Linux/macOS: Stop ONLY the active backend (FFmpeg or GStreamer)
    if (!m_backendHandler) {
        qCWarning(log_ui_camera) << "No multimedia backend handler available on non-Windows platform";
        emit recordingError("No multimedia backend available to stop recording.");
    } else {
        bool stopSuccess = false;

        // Linux-specific recording stop code
        switch (m_backendHandler->getBackendType()) {
            case MultimediaBackendType::FFmpeg: {
                if (FFmpegBackendHandler* ffmpeg = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                    // Stop the actual recording (void return type)
                    ffmpeg->stopRecording();
                    stopSuccess = true;
                    
                    qCInfo(log_ui_camera) << "Stopped recording via FFmpegBackendHandler";
                } else {
                    qCWarning(log_ui_camera) << "Backend type is FFmpeg but cast failed";
                }
                break;
            }
#ifndef Q_OS_WIN
            case MultimediaBackendType::GStreamer: {
                if (GStreamerBackendHandler* gst = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get())) {
                    // Stop the actual recording (void return type)
                    gst->stopRecording();
                    stopSuccess = true;
                    
                    qCInfo(log_ui_camera) << "Stopped recording via GStreamerBackendHandler";
                } else {
                    qCWarning(log_ui_camera) << "Backend type is GStreamer but cast failed";
                }
                break;
            }
#endif
            default:
                qCWarning(log_ui_camera) << "Unsupported backend for Linux recording stop:" << static_cast<int>(m_backendHandler->getBackendType());
                emit recordingError("Unsupported backend for stopping recording on Linux");
                break;
        }

        // Windows-specific recording stop code using QtBackendHandler
        // This code block is never executed on Windows due to the outer #ifdef
        // It's here for completeness in case someone moves the #else/#endif structure
        if (QtBackendHandler* qtHandler = qobject_cast<QtBackendHandler*>(m_backendHandler.get())) {
            stopSuccess = qtHandler->stopRecording();
            if (stopSuccess) {
                qCInfo(log_ui_camera) << "Successfully stopped recording via QtBackendHandler";
            } else {
                qCWarning(log_ui_camera) << "QtBackendHandler failed to stop recording gracefully";
            }
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to QtBackendHandler for recording stop";
        }
        
        // Log the final result of the stop operation
        qCInfo(log_ui_camera) << "Recording stop result: " << (stopSuccess ? "Successful" : "Failed");
    }

    // Check if the file exists after recording is stopped
    if (!recordingPath.isEmpty()) {
        QFileInfo fileInfo(recordingPath);
        QTimer::singleShot(2000, this, [this, recordingPath, fileInfo]() {
            if (fileInfo.exists()) {
                qCInfo(log_ui_camera) << "Recording saved successfully to:" << recordingPath
                                     << "Size:" << fileInfo.size() << "bytes";
                
                if (fileInfo.size() < 1024) {  // If file is smaller than 1KB
                    qCWarning(log_ui_camera) << "Recording file is suspiciously small, may be corrupted";
                    
                    // Try to check if the file is actually a valid video
                    QFile checkFile(recordingPath);
                    if (checkFile.open(QIODevice::ReadOnly)) {
                        QByteArray header = checkFile.read(16); // Read first 16 bytes
                        checkFile.close();
                        
                        // Basic check for some common video formats
                        if (header.isEmpty() || 
                            !(header.startsWith("\x00\x00\x00") || // MP4
                              header.startsWith("RIFF") ||         // AVI
                              header.startsWith("\x1A\x45\xDF\xA3"))) { // MKV
                            
                            qCWarning(log_ui_camera) << "Recording file doesn't appear to have a valid header";
                            emit recordingError("Recording failed - output file appears to be invalid");
                            return;
                        }
                    }
                    
                    emit recordingError("Recording file may be corrupted (very small size)");
                } else {
                    // Show a success notification or open the folder
                    qCInfo(log_ui_camera) << "Recording completed successfully";
                    emit recordingStopped();
                }
            } else {
                qCWarning(log_ui_camera) << "Recording file does not exist after stopping:" << recordingPath;
                emit recordingError("Failed to save recording file");
            }
        });
    } else {
        // No path was set, but still emit the signal to update the UI
        emit recordingStopped();
    }
    
    // Clear current recording path
    m_currentRecordingPath.clear();
}

void CameraManager::pauseRecording()
{
    qCDebug(log_ui_camera) << "Pause recording (FFmpeg backend)";
    
    if (!m_backendHandler || !isFFmpegBackend()) {
        qCWarning(log_ui_camera) << "FFmpeg backend not available for pause";
        return;
    }
    
    FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
    if (ffmpeg) {
        ffmpeg->pauseRecording();
        qCDebug(log_ui_camera) << "Paused recording via FFmpeg backend";
    }
}

void CameraManager::resumeRecording()
{
    qCDebug(log_ui_camera) << "Resume recording (FFmpeg backend)";
    
    if (!m_backendHandler || !isFFmpegBackend()) {
        qCWarning(log_ui_camera) << "FFmpeg backend not available for resume";
        return;
    }
    
    FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
    if (ffmpeg) {
        ffmpeg->resumeRecording();
        qCDebug(log_ui_camera) << "Resumed recording via FFmpeg backend";
    }
}

bool CameraManager::isRecording() const
{
    // Check if we have an active recording path
    if (!m_currentRecordingPath.isEmpty()) {
        qCDebug(log_ui_camera) << "Recording path is set:" << m_currentRecordingPath;
        return true;
    }
    
    // Check FFmpeg backend
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = const_cast<CameraManager*>(this)->getFFmpegBackend();
        if (ffmpeg && ffmpeg->isRecording()) {
            qCDebug(log_ui_camera) << "FFmpeg backend reports recording active";
            return true;
        }
    }
    
    qCDebug(log_ui_camera) << "Final recording status: NOT ACTIVE";
    return false;
}

bool CameraManager::isPaused() const
{
    // Check FFmpeg backend
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = const_cast<CameraManager*>(this)->getFFmpegBackend();
        if (ffmpeg && ffmpeg->isRecording() && ffmpeg->isPaused()) {
            qCDebug(log_ui_camera) << "FFmpeg backend pause status: PAUSED";
            return true;
        }
    }
    
    qCDebug(log_ui_camera) << "Pause status: NOT PAUSED";
    return false;
}

// Helper methods removed - QCamera-dependent
// REMOVED: generateRecordingFilePath(), configureMediaRecorderForRecording(), 
// REMOVED: setupConnections(), configureResolutionAndFormat(),
// REMOVED: setCameraFormat(), getCameraFormat(), getCameraFormats()

// Camera device management and switching functionality

QList<QCameraDevice> CameraManager::getAvailableCameraDevices() const
{
    QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    
    // Deduplicate camera devices based on device ID
    // Windows sometimes lists the same camera twice with different names
    QMap<QByteArray, QCameraDevice> uniqueDevices;
    
    for (const QCameraDevice& device : devices) {
        QByteArray deviceId = device.id();
        QString deviceDescription = device.description();
        
        // Skip "USB2.0 HD UVC WebCam" - it's a duplicate of the Openterface device
        if (deviceDescription == "USB2.0 HD UVC WebCam") {
            qCDebug(log_ui_camera) << "Filtering out USB2.0 HD UVC WebCam device (duplicate)";
            continue;
        }
        
        if (uniqueDevices.contains(deviceId)) {
            qCDebug(log_ui_camera) << "Duplicate camera device detected:"
                                   << "'" << device.description() << "'"
                                   << "vs"
                                   << "'" << uniqueDevices[deviceId].description() << "'"
                                   << "with same ID:" << deviceId;
            qCDebug(log_ui_camera) << "Keeping first detected device:" << uniqueDevices[deviceId].description();
            // Keep the first device detected (no preference for specific names)
        } else {
            uniqueDevices[deviceId] = device;
        }
    }
    
    QList<QCameraDevice> deduplicatedDevices = uniqueDevices.values();
    
    if (deduplicatedDevices.size() < devices.size()) {
        qCDebug(log_ui_camera) << "Filtered/deduplicated" << devices.size() << "camera devices down to" << deduplicatedDevices.size();
    }
    
    return deduplicatedDevices;
}

QCameraDevice CameraManager::getCurrentCameraDevice() const
{
    return m_currentCameraDevice;
}

// REMOVED: Old single-parameter switchToCameraDevice() method
// Now using switchToCameraDevice(const QCameraDevice&, const QString& portChain) only

bool CameraManager::switchToCameraDevice(const QCameraDevice &cameraDevice, const QString& portChain)
{
    if (!isCameraDeviceValid(cameraDevice)) {
        qCWarning(log_ui_camera) << "Cannot switch to invalid camera device:" << cameraDevice.description();
        return false;
    }
    
    qCDebug(log_ui_camera) << "Switching to camera device:" << cameraDevice.description() << "with port chain:" << portChain;
    
    // Check if switching to the same device with the same port chain
    QString targetDevicePath = convertCameraDeviceToPath(cameraDevice);
    bool isSameDevice = (m_currentCameraDevice.isNull() == false) && 
                       (QString::fromUtf8(m_currentCameraDevice.id()) == QString::fromUtf8(cameraDevice.id()));
    bool isSamePortChain = (m_currentCameraPortChain == portChain);
    
    if (isSameDevice && isSamePortChain) {
        qCDebug(log_ui_camera) << "Switching to same device with same port chain, doing nothing";
        return true;
    }
    
    // Update current device tracking
    m_currentCameraDevice = cameraDevice;
    m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
    m_currentCameraPortChain = portChain;
    
    if (isSameDevice) {
        qCDebug(log_ui_camera) << "Switching to same device, updating port chain only";
        // Just update the port chain in the backend
        if (FFmpegBackendHandler* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
            ffmpegHandler->setCurrentDevicePortChain(portChain);
        }
    #ifndef Q_OS_WIN
    if (GStreamerBackendHandler* gstHandler = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get())) {
            gstHandler->setCurrentDevicePortChain(portChain);
    }
    #endif
        emit cameraDeviceSwitchComplete(cameraDevice.description());
        return true;
    }
    
    // Stop current camera if running
    bool wasRunning = false;
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        if (ffmpegHandler) {
            wasRunning = ffmpegHandler->isDirectCaptureRunning();
        }
    }
    
    stopCamera();
    
    // Add delay to allow device to be properly released (Windows needs this)
    if (wasRunning) {
        QThread::msleep(500); // Wait for device to be fully released
        qCDebug(log_ui_camera) << "Waited 500ms for device to be released";
    }
    
    // Configure backend with new device
    if (m_backendHandler) {
        m_backendHandler->configureCameraDevice();
        
        // Pass port chain to backend for hotplug tracking
        if (FFmpegBackendHandler* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
            ffmpegHandler->setCurrentDevicePortChain(portChain);
            // Set the current device path for FFmpeg backend
            QString devicePath = convertCameraDeviceToPath(cameraDevice);
            ffmpegHandler->setCurrentDevice(devicePath);
            qCDebug(log_ui_camera) << "Set device path in FFmpeg backend:" << devicePath;
        }
        #ifndef Q_OS_WIN
        if (GStreamerBackendHandler* gstHandler = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get())) {
            gstHandler->setCurrentDevicePortChain(portChain);
            // Set the current device path for GStreamer backend
            QString devicePath = convertCameraDeviceToPath(cameraDevice);
            gstHandler->setCurrentDevice(devicePath);
            // Ensure the GStreamer backend has sensible defaults for resolution and framerate
            // if none were explicitly set by the caller. Use 1280x720@30 as a safe default.
            QSize defaultResolution(1920, 1080);
            int defaultFramerate = 30;
            gstHandler->setResolutionAndFramerate(defaultResolution, defaultFramerate);
            qCDebug(log_ui_camera) << "GStreamer default resolution/framerate set to" << defaultResolution << defaultFramerate;
            qCDebug(log_ui_camera) << "Set device path in GStreamer backend:" << devicePath;
        }
        #endif

#ifdef Q_OS_WIN
        if (MfBackendHandler* mfHandler = qobject_cast<MfBackendHandler*>(m_backendHandler.get())) {
            // For MF, use the cameraDevicePath from DeviceInfo (symbolic link)
            // rather than the Qt QCamera-based convertCameraDeviceToPath.
            DeviceInfo mfDevInfo = DeviceManager::getInstance().getCurrentSelectedDevice();
            if (!mfDevInfo.cameraDevicePath.isEmpty()) {
                mfHandler->setDevicePath(mfDevInfo.cameraDevicePath);
                qCDebug(log_ui_camera) << "Set camera symbolic link in MF backend:" << mfDevInfo.cameraDevicePath;
            } else {
                qCWarning(log_ui_camera) << "No camera symbolic link available for MF backend";
            }
        }
#endif

        // Start camera with new device
        startCamera();
        
        emit cameraDeviceSwitchComplete(cameraDevice.description());
        return true;
    }
    
    qCWarning(log_ui_camera) << "No backend handler available for device switch";
    return false;
}

bool CameraManager::isCameraDeviceValid(const QCameraDevice& device) const
{
    return !device.isNull() && !device.id().isEmpty();
}

// REMOVED: switchToCameraDeviceById() - QCamera-dependent method

QString CameraManager::getCurrentCameraDeviceId() const
{
    if (m_currentCameraDeviceId.isEmpty()) {
        qCDebug(log_ui_camera) << "Current camera device ID is empty";
        return QString();
    }
    
    qCDebug(log_ui_camera) << "Current camera device ID:" << m_currentCameraDeviceId;
    return m_currentCameraDeviceId;
}

QString CameraManager::getCurrentCameraDeviceDescription() const
{
    if (m_currentCameraDevice.isNull()) {
        qCDebug(log_ui_camera) << "Current camera device is null, returning empty string";
        return QString();
    }
    
    QString description = m_currentCameraDevice.description();
    qCDebug(log_ui_camera) << "Current camera device description:" << description;
    return description;
}

void CameraManager::refreshAvailableCameraDevices()
{
    QList<QCameraDevice> previousDevices = m_availableCameraDevices;
    m_availableCameraDevices = getAvailableCameraDevices();
    
    qCDebug(log_ui_camera) << "Refreshed camera devices, now have" << m_availableCameraDevices.size() << "devices";
    
    // Emit signal if device count changed
    if (previousDevices.size() != m_availableCameraDevices.size()) {
        emit availableCameraDevicesChanged(m_availableCameraDevices.size());
    }
}

// REMOVED: findBestAvailableCamera(), getAllCameraDescriptions(), switchToCameraDeviceById() - QCamera-dependent methods

// Removed duplicate broken getCurrentCameraDeviceDescription - keeping only correct version below

// REMOVED: isCameraDeviceValid() - QCamera-dependent method

// REMOVED: isCameraDeviceAvailable() - QCamera-dependent method

// REMOVED: getAvailableCameraDeviceDescriptions() - QCamera-dependent method

// REMOVED: getAvailableCameraDeviceIds() - QCamera-dependent method

// REMOVED: findBestAvailableCamera() - QCamera-dependent method

// REMOVED: getAllCameraDescriptions() - QCamera-dependent method

// Removed duplicate refreshAvailableCameraDevices - keeping only version above

// Note: Automatic device coordination methods have been disabled
// These methods previously handled automatic camera switching when devices changed

QString CameraManager::extractShortIdentifier(const QString& fullId) const
{
    // Extract patterns like "7&1FF4451E&2&0000" from full device IDs (Windows)
    // or video device numbers from /dev/video paths (Linux)
    
    // First, check for Linux V4L device pattern: /dev/video<number>
    QRegularExpression linuxRegex(R"(/dev/video(\d+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch linuxMatch = linuxRegex.match(fullId);
    
    if (linuxMatch.hasMatch()) {
        QString shortId = linuxMatch.captured(1);
        qCDebug(log_ui_camera) << "Extracted Linux V4L short identifier:" << shortId << "from:" << fullId;
        return shortId;
    }
    
    // Look for Windows patterns with format: digit&hexdigits&digit&hexdigits
    // Examples: "7&1FF4451E&2&0000", "6&2ABC123F&1&0001", etc.
    QRegularExpression windowsRegex(R"((\d+&[A-F0-9]+&\d+&[A-F0-9]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch windowsMatch = windowsRegex.match(fullId);
    
    if (windowsMatch.hasMatch()) {
        QString shortId = windowsMatch.captured(1);
        qCDebug(log_ui_camera) << "Extracted Windows short identifier:" << shortId << "from:" << fullId;
        return shortId;
    }
    
    qCDebug(log_ui_camera) << "No short identifier pattern found in:" << fullId;
    return fullId;
    // return QString();
}

QString CameraManager::convertCameraDeviceToPath(const QCameraDevice& device) const
{
    QString deviceId = QString::fromUtf8(device.id());
    QString deviceDescription = device.description();
    
#ifdef Q_OS_WIN
    // Windows: DirectShow uses the friendly device name directly
    // Just use "video=<device_description>" format
    QString dshowDeviceName = QString("video=%1").arg(deviceDescription);
    qCDebug(log_ui_camera) << "DirectShow device:" << dshowDeviceName;
    return dshowDeviceName;
#else
    // Linux/macOS: Use V4L2 device path (usually /dev/video0, /dev/video1, etc.)
    // If the device ID is already a /dev/video path, use it directly
    if (deviceId.startsWith("/dev/video")) {
        qCDebug(log_ui_camera) << "Using V4L2 device path:" << deviceId;
        return deviceId;
    }
    
    // Try to extract video device number and construct path
    QRegularExpression re("(\\d+)");
    QRegularExpressionMatch match = re.match(deviceId);
    if (match.hasMatch()) {
        QString videoPath = "/dev/video" + match.captured(1);
        qCDebug(log_ui_camera) << "Constructed V4L2 device path:" << videoPath << "from ID:" << deviceId;
        return videoPath;
    }
    
    // Fallback: assume /dev/video0 if we can't parse the ID
    qCWarning(log_ui_camera) << "Could not parse device ID:" << deviceId << "- defaulting to /dev/video0";
    return "/dev/video0";
#endif
}

QCameraDevice CameraManager::findQtOpenterfaceDevice(const QList<QCameraDevice>& devices) const
{
    QList<QCameraDevice> devList = devices;
    if (devList.isEmpty()) {
        devList = getAvailableCameraDevices();
    }

    for (const QCameraDevice& device : devList) {
        if (device.description().contains("Openterface", Qt::CaseInsensitive) ||
            device.description().contains("MACROSILICON", Qt::CaseInsensitive) ||
            device.description().contains("345F", Qt::CaseInsensitive) ||
            device.description() == "Openterface") {
            return device;
        }
    }

    return QCameraDevice();
}

QString CameraManager::determineDirectCaptureDevicePath(QString &outPortChain, bool &ok) const
{
    ok = false;
    outPortChain.clear();
    QString devicePath;

    DeviceManager& deviceManager = DeviceManager::getInstance();
    DeviceInfo selectedDevice = deviceManager.getCurrentSelectedDevice();

    QList<QCameraDevice> devices = getAvailableCameraDevices();

#ifdef Q_OS_WIN
    // Windows: prefer DeviceManager selected device, then search Qt devices for Openterface
    if (selectedDevice.isValid()) {
        outPortChain = selectedDevice.portChain;
        QCameraDevice found = findQtOpenterfaceDevice(devices);
        if (!found.isNull()) {
            devicePath = convertCameraDeviceToPath(found);
        }
    }

    if (devicePath.isEmpty()) {
        // fallback: use any available camera via Qt detection
        if (!devices.isEmpty()) {
            devicePath = convertCameraDeviceToPath(devices.first());
        }
    }
#else
    // Linux/macOS: prefer DeviceManager cameraDevicePath, otherwise detect via Qt
    if (selectedDevice.isValid() && !selectedDevice.cameraDevicePath.isEmpty()) {
        devicePath = selectedDevice.cameraDevicePath;
        outPortChain = selectedDevice.portChain;
    } else {
        QCameraDevice found = findQtOpenterfaceDevice(devices);
        if (!found.isNull()) {
            devicePath = convertCameraDeviceToPath(found);
        }

        if (devicePath.isEmpty() && !devices.isEmpty()) {
            devicePath = convertCameraDeviceToPath(devices.first());
        }

        if (devicePath.isEmpty()) {
            devicePath = QStringLiteral("/dev/video0");
        }
    }
#endif

    if (!devicePath.isEmpty()) {
        ok = true;
    }
    return devicePath;
}

// REMOVED: displayAllCameraDeviceIds() - QCamera-dependent method

// REMOVED: handleCameraTimeout() - QCamera-dependent method

QCameraDevice CameraManager::findMatchingCameraDevice(const QString& portChain) const
{
    if (portChain.isEmpty()) {
        qCDebug(log_ui_camera) << "Empty port chain provided";
        return QCameraDevice();
    }

    qWarning() << "[HOTPLUG-CAM] findMatchingCameraDevice for portChain:" << portChain;

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);

    if (devices.isEmpty()) {
        qWarning() << "[HOTPLUG-CAM] No devices found in DeviceManager for port chain:" << portChain;
        return QCameraDevice();
    }

    qWarning() << "[HOTPLUG-CAM] Found" << devices.size() << "device(s) in DeviceManager for port chain:" << portChain;

    // Look for a device that has camera information
    DeviceInfo selectedDevice;
    for (const DeviceInfo& device : devices) {
        if (!device.cameraDeviceId.isEmpty() || !device.cameraDevicePath.isEmpty()) {
            selectedDevice = device;
            qCDebug(log_ui_camera) << "Found device with camera info:" 
                     << "cameraDeviceId:" << device.cameraDeviceId
                     << "cameraDevicePath:" << device.cameraDevicePath;
            break;
        }
    }

    if (!selectedDevice.isValid() || (selectedDevice.cameraDeviceId.isEmpty() && selectedDevice.cameraDevicePath.isEmpty())) {
        qWarning() << "[HOTPLUG-CAM] No device with camera info for port chain:" << portChain
                    << "cameraDeviceId empty:" << selectedDevice.cameraDeviceId.isEmpty()
                    << "cameraDevicePath empty:" << selectedDevice.cameraDevicePath.isEmpty()
                    << "device valid:" << selectedDevice.isValid();
        qCInfo(log_ui_camera) << "Device info may not be populated yet - camera switch will fail, needs retry";
        return QCameraDevice();
    }

    // Extract short identifier from target camera ID for better matching
    QString targetShortId;
    if (!selectedDevice.cameraDeviceId.isEmpty()) {
        targetShortId = extractShortIdentifier(selectedDevice.cameraDeviceId);
        qCDebug(log_ui_camera) << "Extracted target short identifier:" << targetShortId;
    }

    QList<QCameraDevice> availableCameras = getAvailableCameraDevices();

    for (const QCameraDevice& camera : availableCameras) {
        QString cameraId = QString::fromUtf8(camera.id());
        // if cameraId is a number, append /dev/video as prefix
        if (cameraId.toInt() != 0 || cameraId == "0") {
            cameraId = "/dev/video" + cameraId;
        }

        QString cameraDescription = camera.description();

        qCDebug(log_ui_camera) << "Checking camera device:" << cameraDescription 
                 << "ID:" << cameraId;

        // Try multiple matching strategies
        // Strategy 1: Short identifier match (preferred method)
        if (!targetShortId.isEmpty() && cameraId.contains(targetShortId, Qt::CaseInsensitive)) {
            qCDebug(log_ui_camera) << "Matched camera by short identifier:" << targetShortId;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
        // Strategy 2: Direct ID match
        if (!selectedDevice.cameraDeviceId.isEmpty() && cameraId == selectedDevice.cameraDeviceId) {
            qCDebug(log_ui_camera) << "Matched camera by exact ID:" << selectedDevice.cameraDeviceId;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
        // Strategy 3: Path match (if applicable)
        if (!selectedDevice.cameraDevicePath.isEmpty() && cameraId.contains(selectedDevice.cameraDevicePath, Qt::CaseInsensitive)) {
            qCDebug(log_ui_camera) << "Matched camera by path:" << selectedDevice.cameraDevicePath;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
    }

    qWarning() << "[HOTPLUG-CAM] Could not find matching Qt camera device for port chain:" << portChain
               << "checked" << availableCameras.size() << "camera(s)";
    for (const QCameraDevice& cam : availableCameras) {
        qWarning() << "[HOTPLUG-CAM]   Qt camera:" << cam.description() << "ID:" << cam.id();
    }
    return QCameraDevice();
}

QCameraDevice CameraManager::findCameraByDeviceInfo(const DeviceInfo& deviceInfo) const
{
    if (!deviceInfo.hasCameraDevice()) {
        qCDebug(log_ui_camera) << "Device has no camera component";
        return QCameraDevice();
    }
    
    qCDebug(log_ui_camera) << "Finding Qt camera device for DeviceInfo:";
    qCDebug(log_ui_camera) << "  Camera device ID:" << deviceInfo.cameraDeviceId;
    qCDebug(log_ui_camera) << "  Camera device path:" << deviceInfo.cameraDevicePath;
    
    QList<QCameraDevice> availableCameras = getAvailableCameraDevices();
    
    for (const QCameraDevice& camera : availableCameras) {
        QString cameraId = QString::fromUtf8(camera.id());
        QString cameraDescription = camera.description();
        
        qCDebug(log_ui_camera) << "  Checking camera:" << cameraDescription << "ID:" << cameraId;
        
        // Strategy 1: Match by device ID
        if (!deviceInfo.cameraDeviceId.isEmpty()) {
            // Try exact match
            if (cameraId.compare(deviceInfo.cameraDeviceId, Qt::CaseInsensitive) == 0) {
                qCDebug(log_ui_camera) << "  ✓ Matched by exact device ID";
                return camera;
            }
            
            // Try partial match (device ID contains camera ID or vice versa)
            if (deviceInfo.cameraDeviceId.contains(cameraId, Qt::CaseInsensitive) ||
                cameraId.contains(deviceInfo.cameraDeviceId, Qt::CaseInsensitive)) {
                qCDebug(log_ui_camera) << "  ✓ Matched by partial device ID";
                return camera;
            }
        }
        
        // Strategy 2: Match by device path
        if (!deviceInfo.cameraDevicePath.isEmpty()) {
            if (cameraId.contains(deviceInfo.cameraDevicePath, Qt::CaseInsensitive) ||
                deviceInfo.cameraDevicePath.contains(cameraId, Qt::CaseInsensitive)) {
                qCDebug(log_ui_camera) << "  ✓ Matched by device path";
                return camera;
            }
        }
        
        // Strategy 3: Match by hardware identifiers (for Openterface devices)
        if (cameraDescription.contains("345F", Qt::CaseInsensitive) ||
            cameraId.contains("345F", Qt::CaseInsensitive) ||
            cameraDescription.contains("Openterface", Qt::CaseInsensitive)) {
            qCDebug(log_ui_camera) << "  ✓ Matched by Openterface hardware identifier";
            return camera;
        }
    }
    
    qCDebug(log_ui_camera) << "  ✗ No matching Qt camera device found";
    return QCameraDevice();
}

bool CameraManager::initializeCameraWithVideoOutput(QGraphicsVideoItem* videoOutput)
{
    
    if (!videoOutput) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null graphics video output";
        return false;
    }
    
    // Set the video output first if it's different from current
    if (m_graphicsVideoOutput != videoOutput) {
        setVideoOutput(videoOutput);
    }
    
    // Check if we already have an active camera device
    if (hasActiveCameraDevice()) {
        return true;
    }
    
    bool switchSuccess = false;
    
    // Windows: Use enhanced approach with better device detection
    if (isWindowsPlatform()) {
        
        // First, try to find camera using device manager information
        DeviceManager& deviceManager = DeviceManager::getInstance();
        QList<DeviceInfo> devices = deviceManager.getCurrentDevices();
        
        QCameraDevice openterfaceDevice;
        QString targetPortChain;
        
        // Look for devices with camera components
        for (const DeviceInfo& device : devices) {
            if (device.hasCameraDevice()) {
                
                // Try to find this camera in Qt's camera list
                QCameraDevice matchedCamera = findCameraByDeviceInfo(device);
                if (!matchedCamera.isNull()) {
                    openterfaceDevice = matchedCamera;
                    targetPortChain = device.portChain;
                    break;
                }
            }
        }
        
        // Fallback: Look for any camera with "Openterface" in the description
        if (openterfaceDevice.isNull()) {
            QList<QCameraDevice> allDevices = getAvailableCameraDevices();
            
            QCameraDevice found = findQtOpenterfaceDevice(allDevices);
            if (!found.isNull()) {
                openterfaceDevice = found;
            }
        }

        if (!openterfaceDevice.isNull()) {
            switchSuccess = switchToCameraDevice(openterfaceDevice, targetPortChain);
            // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
            // called it internally. Calling it again causes double-start and UI freeze.
        } else {
            qCWarning(log_ui_camera) << "Windows: No Openterface camera device found";
            
            // Additional debugging: list all available cameras
            QList<QCameraDevice> allDevices = getAvailableCameraDevices();
            for (const QCameraDevice& device : allDevices) {
            }
        }
        
        return switchSuccess && !m_currentCameraDevice.isNull();
    }
    
    // Non-Windows: Use existing complex backend logic
    // First priority: Check for port chain in global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    if (!portChain.isEmpty()) {
        
        QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
        
        if (!matchedCamera.isNull()) {
            switchSuccess = switchToCameraDevice(matchedCamera, portChain);
            if (switchSuccess) {
            } else {
                qCWarning(log_ui_camera) << "Failed to switch to matched camera device:" << matchedCamera.description();
            }
        } else {
            qCDebug(log_ui_camera) << "No matching camera device found for port chain:" << portChain;
        }
    } else {
    }
    
    // Fallback: Traditional camera selection logic (without port chain tracking)
    if (!switchSuccess) {
        // Enforce camera device description to be "Openterface"
        QList<QCameraDevice> devices = getAvailableCameraDevices();
        QCameraDevice openterfaceDevice = findQtOpenterfaceDevice(devices);

        if (!openterfaceDevice.isNull()) {
            switchSuccess = switchToCameraDevice(openterfaceDevice, QString());  // No port chain available for fallback
            if (switchSuccess) {
            }
        } else {
            qCWarning(log_ui_camera) << "No camera device with description 'Openterface' found";
        }
    }

    // Start camera if switch was successful
    if (switchSuccess) {
        startCamera();
    }

    // If we still don't have a camera device, return false
    if (m_currentCameraDevice.isNull()) {
        qCWarning(log_ui_camera) << "No camera device available for initialization";
        return false;
    }

    return switchSuccess;
}

bool CameraManager::initializeCameraWithVideoOutput(VideoPane* videoPane, bool startCapture)
{
    
    if (!videoPane) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null VideoPane";
        return false;
    }

    // Wire up frame timestamp tracking (for test framework and streaming detection)
    connect(videoPane, &VideoPane::newVideoFrameReceived,
            this, &CameraManager::onNewVideoFrameReceived, Qt::UniqueConnection);
    
    // Check if we're using FFmpeg backend for direct capture
    if (isFFmpegBackend() && m_backendHandler) {
        
        // Cast to FFmpegBackendHandler to access direct capture methods
        auto* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        if (ffmpegHandler) {
                // Enable direct FFmpeg mode in VideoPane and set the video output
                videoPane->enableDirectFFmpegMode(true);
                ffmpegHandler->setVideoOutput(videoPane);

                // Capture errors from FFmpeg backend
                connect(ffmpegHandler, &FFmpegBackendHandler::captureError,
                        this, &CameraManager::onFFmpegCaptureError, Qt::UniqueConnection);

                // Connect camera active changed to VideoPane (UniqueConnection)
                connect(this, &CameraManager::cameraActiveChanged, videoPane, &VideoPane::onCameraActiveChanged, Qt::UniqueConnection);

                // Get device path and configuration via helper
                QString devicePath;
                QSize resolution(0, 0); // Auto-detect maximum resolution  
                int framerate = 0; // Default to auto-detect
                
                // Try to get user-configured framerate from GlobalVar (set via videopage)
                int configuredFps = GlobalVar::instance().getCaptureFps();
                if (configuredFps > 0) {
                    framerate = configuredFps;
                    qCDebug(log_ui_camera) << "Using user-configured framerate:" << framerate << "fps";
                } else {
                    qCDebug(log_ui_camera) << "Using auto-detect framerate";
                }
                
                QString detectedPortChain;
                bool deviceOk = false;
                devicePath = determineDirectCaptureDevicePath(detectedPortChain, deviceOk);

                if (!deviceOk || devicePath.isEmpty()) {
                    qCWarning(log_ui_camera) << "Could not determine device path for FFmpeg direct capture";
                    return false;
                }
            
#ifdef Q_OS_WIN
            // Windows: Use DirectShow device name from Qt camera device
            DeviceManager& deviceManager = DeviceManager::getInstance();
            DeviceInfo selectedDevice = deviceManager.getCurrentSelectedDevice();
            
            if (selectedDevice.isValid()) {
                // Try to get camera device from available devices
                QList<QCameraDevice> devices = getAvailableCameraDevices();
                QCameraDevice found = findQtOpenterfaceDevice(devices);
                if (!found.isNull()) {
                    // Convert Qt camera device to DirectShow format
                    devicePath = convertCameraDeviceToPath(found);
                }
            }
            
            if (devicePath.isEmpty()) {
                qCWarning(log_ui_camera) << "No Openterface device found, searching for any available camera";
                QList<QCameraDevice> devices = getAvailableCameraDevices();
                if (!devices.isEmpty()) {
                    devicePath = convertCameraDeviceToPath(devices.first());
                    qCDebug(log_ui_camera) << "Using first available camera:" << devicePath;
                } else {
                    qCCritical(log_ui_camera) << "No camera devices available";
                    return false;
                }
            }
#else
            // Linux/macOS: Use V4L2 device path
            DeviceManager& deviceManager = DeviceManager::getInstance();
            DeviceInfo selectedDevice = deviceManager.getCurrentSelectedDevice();
            
            if (selectedDevice.isValid() && !selectedDevice.cameraDevicePath.isEmpty()) {
                devicePath = selectedDevice.cameraDevicePath;
            } else {
                qCWarning(log_ui_camera) << "No valid camera device path found in selected device, trying Qt camera detection";
                
                // Fallback: Try to detect Openterface device path from Qt cameras
                QList<QCameraDevice> devices = getAvailableCameraDevices();
                QCameraDevice found = findQtOpenterfaceDevice(devices);
                if (!found.isNull()) {
                    // Convert Qt device ID to V4L2 device path
                    devicePath = convertCameraDeviceToPath(found);
                }
                
                if (devicePath.isEmpty()) {
                    devicePath = "/dev/video0"; // Final fallback
                    qCWarning(log_ui_camera) << "Using default device path:" << devicePath;
                }
            }
#endif
            
            // Only start capture if requested (otherwise just set up the pipeline)
            if (startCapture) {
                bool captureStarted = ffmpegHandler->startDirectCapture(devicePath, resolution, framerate);
                
                if (captureStarted) {
                    // Prefer known port chain from DeviceManager if available
                    m_currentCameraPortChain = detectedPortChain.isEmpty() ? devicePath : detectedPortChain;
                    
                    // Emit camera active signal to trigger UI updates (e.g., switch to VideoPane)
                    emit cameraActiveChanged(true);
                    
                    return true;
                } else {
                    qCWarning(log_ui_camera) << "Failed to start FFmpeg direct capture";
                    // Fall back to standard Qt camera approach
                }
            } else {
                m_currentCameraPortChain = detectedPortChain.isEmpty() ? devicePath : detectedPortChain;
                return true;
            }
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to FFmpegBackendHandler";
        }
    } else {
    }
    
    // Check if we're using GStreamer backend for direct pipeline capture (Linux only)
#ifndef Q_OS_WIN
    if (isGStreamerBackend() && m_backendHandler) {
        auto* gstHandler = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
        if (gstHandler) {
            // Enable direct GStreamer mode in VideoPane
            videoPane->enableDirectGStreamerMode(true);

            // Set VideoPane as GStreamer video output for direct rendering
            gstHandler->setVideoOutput(videoPane);

            // No captureError or deviceActivated signals to connect for GStreamer handler here

            // Determine device path and port chain via helper
            QString devicePath;
            QString detectedPortChain;
            bool deviceOk = false;
            devicePath = determineDirectCaptureDevicePath(detectedPortChain, deviceOk);
            if (!deviceOk || devicePath.isEmpty()) {
                qCWarning(log_ui_camera) << "Could not determine device path for GStreamer direct capture";
                return false;
            }

            // Only start capture if requested
            if (startCapture) {
                // Set device and port chain into the handler and start
                gstHandler->setCurrentDevicePortChain(detectedPortChain);
                gstHandler->setCurrentDevice(devicePath);
                // Ensure the GStreamer backend has sensible defaults for resolution and framerate
                // if not otherwise configured. Use 1280x720@30 as a safe default.
                QSize defaultResolution(1280, 720);
                int defaultFramerate = 30;
                gstHandler->setResolutionAndFramerate(defaultResolution, defaultFramerate);
                qCDebug(log_ui_camera) << "GStreamer default resolution/framerate set to" << defaultResolution << defaultFramerate;
                gstHandler->startCamera();

                // Track the current camera port chain locally as we do for FFmpeg
                m_currentCameraPortChain = detectedPortChain.isEmpty() ? devicePath : detectedPortChain;
                emit cameraActiveChanged(true);
                qCDebug(log_ui_camera) << "GStreamer direct capture attempted for device:" << devicePath;
                return true;
            } else {
                return true;
            }
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to GStreamerBackendHandler";
        }
    }
#endif

#ifdef Q_OS_WIN
    // Check if we're using Media Foundation backend for direct capture
    if (isMediaFoundationBackend() && m_backendHandler) {
        auto* mfHandler = qobject_cast<MfBackendHandler*>(m_backendHandler.get());
        if (mfHandler) {
            // Enable direct FFmpeg mode in VideoPane for rendering
            videoPane->enableDirectFFmpegMode(true);

            // Set VideoPane as Media Foundation video output for direct rendering
            mfHandler->setVideoOutput(videoPane);

            // Connect error signals
            connect(mfHandler, &MfBackendHandler::backendError,
                    this, &CameraManager::onMediaFoundationError, Qt::UniqueConnection);

            // Connect camera active changed to VideoPane
            connect(this, &CameraManager::cameraActiveChanged, videoPane, &VideoPane::onCameraActiveChanged, Qt::UniqueConnection);

            // Configure resolution and framerate
            QSize resolution(GlobalVar::instance().getCaptureWidth(), GlobalVar::instance().getCaptureHeight());
            int framerate = GlobalVar::instance().getCaptureFps();
            if (resolution.width() <= 0) resolution = QSize(1920, 1080);
            if (framerate <= 0) framerate = 30;

            mfHandler->setResolution(resolution);
            mfHandler->setFramerate(framerate);

            // Pass the camera device symbolic link to the MF handler.
            // The Windows device enumerator resolves this to the \\?\usb#... form
            // that MF's MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK expects.
            // Without this, devicePath_ stays empty and startCamera() blindly
            // auto-selects the first device from MFEnumDeviceSources (often wrong).
            DeviceInfo mfSelectedDevice = DeviceManager::getInstance().getCurrentSelectedDevice();
            if (!mfSelectedDevice.cameraDevicePath.isEmpty()) {
                mfHandler->setDevicePath(mfSelectedDevice.cameraDevicePath);
                qCDebug(log_ui_camera) << "MF: using camera symbolic link:"
                                       << mfSelectedDevice.cameraDevicePath;
            } else {
                qCWarning(log_ui_camera) << "MF: no camera device path in selected device,"
                                            " falling back to auto-select first MF device";
            }

            if (startCapture) {
                mfHandler->startCamera();

                emit cameraActiveChanged(true);
                return true;
            } else {
                return true;
            }
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to MfBackendHandler";
        }
    }
#endif

    // Fall back to standard Qt camera approach with QGraphicsVideoItem
    videoPane->enableDirectFFmpegMode(false);
    return initializeCameraWithVideoOutput(videoPane->getVideoItem());
}

bool CameraManager::hasActiveCameraDevice() const
{
    // Check if we have a valid device tracked
    return !m_currentCameraDevice.isNull() && !m_currentCameraDeviceId.isEmpty();
}

bool CameraManager::isCameraStreaming() const
{
    if (!hasActiveCameraDevice()) return false;
    if (m_lastFrameTimestamp <= 0) return false;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - m_lastFrameTimestamp) < 5000;  // Frames received within last 5s
}

void CameraManager::onNewVideoFrameReceived()
{
    m_lastFrameTimestamp = QDateTime::currentMSecsSinceEpoch();
}

QString CameraManager::getCurrentCameraPortChain() const
{
    return m_currentCameraPortChain;
}

bool CameraManager::deactivateCameraByPortChain(const QString& portChain)
{
    if (portChain.isEmpty()) {
        qCDebug(log_ui_camera) << "Cannot deactivate camera with empty port chain";
        return false;
    }
    
    // Check if we have an active camera and if its port chain matches
    if (m_currentCameraPortChain.isEmpty()) {
        qCDebug(log_ui_camera) << "No current camera port chain tracked, cannot compare for deactivation";
        return false;
    }
    
    if (m_currentCameraPortChain != portChain) {
        qCDebug(log_ui_camera) << "Current camera port chain" << m_currentCameraPortChain 
                 << "does not match unplugged device port chain" << portChain;
        return false;
    }
    
    qCInfo(log_ui_camera) << "Deactivating camera for unplugged device at port chain:" << portChain;
    qWarning() << "[HOTPLUG-CAM] deactivateCameraByPortChain:" << portChain
               << "currentChain=" << m_currentCameraPortChain;
    
    try {
        // HOTPLUG FIX: Do NOT call stopCamera() here — the caller (shouldDisconnectCamera
        // handler) already calls stopCamera() before calling this function. Calling it twice
        // is redundant and previously added ~7s of blocking on the main thread (via
        // StopCaptureThread waiting for a capture thread stuck on a dead USB device).

        // Clear current device tracking
        // HOTPLUG FIX (修复十一): Save port chain BEFORE clearing — the serial recovery handler
        // needs this to know which camera to restart after hotplug recovery.
        m_lastActiveCameraPortChain = m_currentCameraPortChain;
        m_currentCameraDevice = QCameraDevice();
        m_currentCameraDeviceId.clear();
        m_currentCameraPortChain.clear();
        m_hotplugCameraRestartRetries = 0;  // HOTPLUG FIX: Reset retry counter for next reconnect
        m_frameTimeoutWarningShown = false;  // Reset timeout warning for new session

        // HOTPLUG FIX: Defer video output reset and backend cleanup to the next event loop
        // iteration. Previously this used QThread::msleep(50) × 3 = 150ms of blocking on the
        // main thread. With the async stopDirectCapture() fix, the entire disconnect path is
        // now non-blocking, preventing UI freeze during hotplug.
        QTimer::singleShot(0, this, [this]() {
            // Clear the video output to show blank instead of frozen frame
            if (m_graphicsVideoOutput && m_backendHandler) {
                qCDebug(log_ui_camera) << "Clearing video output (deferred)";
                FFmpegBackendHandler* ffmpeg = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get());
                if (ffmpeg) {
                    ffmpeg->setVideoOutput(static_cast<QGraphicsVideoItem*>(nullptr));
                    ffmpeg->setVideoOutput(m_graphicsVideoOutput);
                }
                #ifndef Q_OS_WIN
                GStreamerBackendHandler* gst = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
                if (gst) {
                    gst->setVideoOutput(static_cast<QGraphicsVideoItem*>(nullptr));
                    gst->setVideoOutput(m_graphicsVideoOutput);
                }
                #endif

        #ifdef Q_OS_WIN
                MfBackendHandler* mf = qobject_cast<MfBackendHandler*>(m_backendHandler.get());
                if (mf) {
                    mf->setVideoOutput(static_cast<QGraphicsVideoItem*>(nullptr));
                    mf->setVideoOutput(m_graphicsVideoOutput);
                }
        #endif
            }
            // Clear device info inside backend handlers
            if (m_backendHandler) {
                if (FFmpegBackendHandler* ffmpeg = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                    ffmpeg->setCurrentDevice(QString());
                    ffmpeg->setCurrentDevicePortChain(QString());
                }
    #ifndef Q_OS_WIN
                if (GStreamerBackendHandler* gst = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get())) {
                    gst->setCurrentDevice(QString());
                    gst->setCurrentDevicePortChain(QString());
                }
    #endif
            }
        });
        
        qCInfo(log_ui_camera) << "Camera successfully deactivated for unplugged device";
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in deactivateCameraByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in deactivateCameraByPortChain";
        return false;
    }
}

bool CameraManager::tryAutoSwitchToNewDevice(const QString& portChain)
{
    qCDebug(log_ui_camera) << "========================================";
    qCDebug(log_ui_camera) << "tryAutoSwitchToNewDevice called";
    qCDebug(log_ui_camera) << "  Target port chain:" << portChain;
    qCDebug(log_ui_camera) << "========================================";

    // Check if we currently have an active camera device
    if (hasActiveCameraDevice()) {
        qCWarning(log_ui_camera) << "!!! Active camera device detected, skipping auto-switch to preserve user selection";
        qCWarning(log_ui_camera) << "!!! Current device:" << m_currentCameraDevice.description();
        qCWarning(log_ui_camera) << "!!! Current port chain:" << m_currentCameraPortChain;
        cancelAutoSwitchRetry();
        return false;
    }

    qCDebug(log_ui_camera) << "✓ No active camera device found, attempting to switch to new device";

    // Cancel any pending retry since we're trying now
    cancelAutoSwitchRetry();

    // IMPORTANT: Refresh available camera devices to ensure the list is up-to-date
    qCDebug(log_ui_camera) << "Refreshing available camera devices before auto-switch";
    refreshAvailableCameraDevices();
    qCDebug(log_ui_camera) << "  Available cameras after refresh:" << m_availableCameraDevices.size();

    // Try to find a matching camera device for the port chain
    qCDebug(log_ui_camera) << "Attempting to find matching camera device for port chain:" << portChain;
    QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);

    if (matchedCamera.isNull()) {
        qCWarning(log_ui_camera) << "✗ No matching camera device found for port chain:" << portChain;
        // Start exponential backoff retry if we haven't exceeded max retries
        if (m_autoSwitchRetry.retryCount < m_autoSwitchRetry.maxRetries) {
            qCInfo(log_ui_camera) << "Starting exponential backoff retry mechanism";
            startAutoSwitchRetry(portChain);
            return false; // Retry in progress
        }
        qCWarning(log_ui_camera) << "  Max retries exceeded, giving up";
        return false;
    }

    qCDebug(log_ui_camera) << "✓ Found matching camera device:" << matchedCamera.description() << "for port chain:" << portChain;

    // Ensure video output is connected before switching
    if (m_graphicsVideoOutput) {
        qCDebug(log_ui_camera) << "Video output available for camera switch";
    } else {
        qCWarning(log_ui_camera) << "!!! No graphics video output available";
    }

    // Switch to the new camera device
    qCDebug(log_ui_camera) << "Calling switchToCameraDevice...";
    bool switchSuccess = switchToCameraDevice(matchedCamera, portChain);

    if (switchSuccess) {
        qCInfo(log_ui_camera) << "✓ Successfully auto-switched to new camera device:" << matchedCamera.description() << "at port chain:" << portChain;
        cancelAutoSwitchRetry();

        // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
        // called it internally. Calling it again causes double-start and UI freeze.
        if (m_graphicsVideoOutput) {
            qCDebug(log_ui_camera) << "Camera started by switchToCameraDevice, video output available";
        } else {
            qCWarning(log_ui_camera) << "!!! Cannot start camera - no video output available";
        }

        emit newDeviceAutoConnected(matchedCamera, portChain);
    } else {
        qCWarning(log_ui_camera) << "✗ Failed to auto-switch to new camera device:" << matchedCamera.description();
        // Retry if switch failed but device was found
        if (m_autoSwitchRetry.retryCount < m_autoSwitchRetry.maxRetries) {
            startAutoSwitchRetry(portChain);
        }
    }

    qCDebug(log_ui_camera) << "========================================";
    return switchSuccess;
}

bool CameraManager::switchToCameraDeviceByPortChain(const QString &portChain)
{
    if (portChain.isEmpty()) {
        qWarning() << "[HOTPLUG-CAM] Cannot switch to camera with empty port chain";
        return false;
    }

    qWarning() << "[HOTPLUG-CAM] switchToCameraDeviceByPortChain:" << portChain;

    try {
        QCameraDevice targetCamera = findMatchingCameraDevice(portChain);

        if (targetCamera.isNull()) {
            qWarning() << "[HOTPLUG-CAM] No matching camera found for port chain:" << portChain;
            return false;
        }
        
        qCDebug(log_ui_camera) << "Found matching camera device:" << targetCamera.description() << "for port chain:" << portChain;
        
        bool switchSuccess = switchToCameraDevice(targetCamera, portChain);
        if (switchSuccess) {
            qCDebug(log_ui_camera) << "Successfully switched to camera device:" << targetCamera.description() << "with port chain:" << portChain;
        } else {
            qCWarning(log_ui_camera) << "Failed to switch to camera device:" << targetCamera.description();
        }
        
        return switchSuccess;
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in switchToCameraDeviceByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in switchToCameraDeviceByPortChain";
        return false;
    }
}

void CameraManager::refreshVideoOutput()
{
    
    try {
        // Force re-establishment of video output connection to ensure new camera feed is displayed
        if (m_graphicsVideoOutput) {
            // Temporarily disconnect and reconnect to force refresh
    // REMOVED: m_captureSession.setVideoOutput(nullptr);
            QThread::msleep(10); // Brief pause
    // REMOVED: m_captureSession.setVideoOutput(m_graphicsVideoOutput);
            
            // Verify reconnection
    // REMOVED: if (m_captureSession.videoOutput() == m_graphicsVideoOutput) {
            } else {
                qCWarning(log_ui_camera) << "Graphics video output refresh failed";
            }
    
    } catch (const std::exception& e) {
        qCritical() << "Exception refreshing video output:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception refreshing video output";
    }
}

void CameraManager::setupWindowsHotplugMonitoring()
{
    qCDebug(log_ui_camera) << "Setting up Windows hotplug monitoring";
    
    // For Windows, we rely on the DeviceManager's hotplug monitor instead of QMediaDevices
    // since QMediaDevices::videoInputsChanged is not reliably available as a signal
    // The DeviceManager hotplug monitor will handle device detection and call our handlers
    
    qCDebug(log_ui_camera) << "Windows hotplug monitoring enabled (using DeviceManager)";
}

void CameraManager::onVideoInputsChanged()
{
    qCDebug(log_ui_camera) << "Video inputs changed - refreshing camera device list";
    
    QList<QCameraDevice> previousDevices = m_availableCameraDevices;
    refreshAvailableCameraDevices();
    
    // Check for disconnected devices
    for (const QCameraDevice& prevDevice : previousDevices) {
        bool stillExists = false;
        for (const QCameraDevice& currentDevice : m_availableCameraDevices) {
            if (QString::fromUtf8(prevDevice.id()) == QString::fromUtf8(currentDevice.id())) {
                stillExists = true;
                break;
            }
        }
        
        if (!stillExists) {
            qCDebug(log_ui_camera) << "Camera device disconnected:" << prevDevice.description();
            
            // Check if this was our current device
            if (!m_currentCameraDevice.isNull() && 
                QString::fromUtf8(m_currentCameraDevice.id()) == QString::fromUtf8(prevDevice.id())) {
                qCInfo(log_ui_camera) << "Current camera device disconnected, stopping camera";
                stopCamera();
                
                // Reset current device tracking
                m_currentCameraDevice = QCameraDevice();
                m_currentCameraDeviceId.clear();
                m_currentCameraPortChain.clear();
                
                QString prevDeviceId = QString::fromUtf8(prevDevice.id());
                emit cameraDeviceDisconnected(prevDeviceId, QString());  // No port chain available
            }
        }
    }
    
    // Check for newly connected devices
    for (const QCameraDevice& currentDevice : m_availableCameraDevices) {
        bool isNew = true;
        for (const QCameraDevice& prevDevice : previousDevices) {
            if (QString::fromUtf8(currentDevice.id()) == QString::fromUtf8(prevDevice.id())) {
                isNew = false;
                break;
            }
        }
        
        if (isNew) {
            qCDebug(log_ui_camera) << "New camera device detected:" << currentDevice.description();
            QString deviceId = QString::fromUtf8(currentDevice.id());
            emit cameraDeviceConnected(deviceId, QString());  // No port chain available
            
            // Auto-switch to new Openterface device if no current device is active
            if (currentDevice.description().contains("Openterface", Qt::CaseInsensitive) && 
                !hasActiveCameraDevice()) {
                qCInfo(log_ui_camera) << "Auto-switching to new Openterface camera device:" << currentDevice.description();
                
                bool switchSuccess = switchToCameraDevice(currentDevice, QString());  // No port chain
                // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
                // called it internally. Calling it again causes double-start and UI freeze.
                if (switchSuccess && m_graphicsVideoOutput) {
                    qCInfo(log_ui_camera) << "✓ Successfully auto-switched to new Openterface camera device";
                } else {
                    qCWarning(log_ui_camera) << "Failed to auto-switch to new Openterface camera device";
                }
            }
        }
    }
}

void CameraManager::connectToHotplugMonitor()
{
    qCDebug(log_ui_camera) << "Connecting CameraManager to hotplug monitor";
    
    // For FFmpeg backend, hotplug is handled directly by the backend to avoid conflicts
    if (isFFmpegBackend()) {
        qCDebug(log_ui_camera) << "FFmpeg backend handles hotplug directly, skipping CameraManager hotplug connections";
        return;
    }
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!hotplugMonitor) {
        qCWarning(log_ui_camera) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_ui_camera) << "========================================";
                qCDebug(log_ui_camera) << "CameraManager: DEVICE UNPLUGGED EVENT";
                qCDebug(log_ui_camera) << "  Device port chain:" << device.portChain;
                qCDebug(log_ui_camera) << "========================================";
                
                // Check if device has camera info from DeviceManager
                bool hasCameraInfoFromDeviceManager = device.hasCameraDevice();
                qCDebug(log_ui_camera) << "Device camera info check:";
                qCDebug(log_ui_camera) << "  Has camera from DeviceManager:" << hasCameraInfoFromDeviceManager;
                
                // CRITICAL FIX: Even if DeviceManager doesn't have camera info,
                // we should still deactivate if:
                // 1. We have an active Openterface camera
                // 2. The port chain matches (or is empty since DeviceManager might not track it properly)
                bool shouldDeactivate = false;
                
                if (hasCameraInfoFromDeviceManager) {
                    qCDebug(log_ui_camera) << "Device has camera component - checking if it matches current camera";
                    qCDebug(log_ui_camera) << "  Current camera port chain:" << m_currentCameraPortChain;
                    qCDebug(log_ui_camera) << "  Unplugged device port chain:" << device.portChain;
                    
                    // Check if the unplugged device matches the current camera device port chain
                    if (!m_currentCameraPortChain.isEmpty() && m_currentCameraPortChain == device.portChain) {
                        shouldDeactivate = true;
                        qCInfo(log_ui_camera) << ">>> Port chains MATCH - Will deactivate camera";
                    }
                } else {
                    // Workaround: If DeviceManager has no camera info, but we have an active camera,
                    // deactivate it anyway when ANY device at the expected port is unplugged
                    qCDebug(log_ui_camera) << "DeviceManager has no camera info for unplugged device";
                    
                    if (hasActiveCameraDevice()) {
                        qCDebug(log_ui_camera) << "We have an active camera - checking if we should deactivate it";
                        
                        // Check if current device is Openterface
                        QString currentDesc = m_currentCameraDevice.description();
                        if (currentDesc.contains("Openterface", Qt::CaseInsensitive)) {
                            qCDebug(log_ui_camera) << "Current camera is Openterface:" << currentDesc;
                            
                            // If port chain matches OR is empty (not tracked), deactivate
                            if (m_currentCameraPortChain.isEmpty() || m_currentCameraPortChain == device.portChain) {
                                shouldDeactivate = true;
                                qCWarning(log_ui_camera) << ">>> Deactivating Openterface camera (fallback - DeviceManager has no camera info)";
                            }
                        }
                    }
                }
                
                // Perform deactivation if needed
                if (shouldDeactivate) {
                    qCInfo(log_ui_camera) << "Deactivating camera for unplugged device at port:" << device.portChain;
                    bool deactivated = deactivateCameraByPortChain(device.portChain);
                    if (deactivated) {
                        qCInfo(log_ui_camera) << "✓ Camera deactivated for unplugged device at port:" << device.portChain;
                    } else {
                        qCWarning(log_ui_camera) << "✗ Camera deactivation FAILED for port:" << device.portChain;
                    }
                } else {
                    qCDebug(log_ui_camera) << "Camera deactivation skipped - no match found";
                    if (m_currentCameraPortChain.isEmpty()) {
                        qCDebug(log_ui_camera) << "  Reason: No current camera port chain tracked";
                    } else {
                        qCDebug(log_ui_camera) << "  Reason: Port chain or device type mismatch";
                    }
                }
                
                // For Windows: Also manually check for Qt camera device changes
                if (isWindowsPlatform()) {
                    onVideoInputsChanged();
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_ui_camera) << "========================================";
                qCDebug(log_ui_camera) << "CameraManager: NEW DEVICE PLUGGED IN EVENT";
                qCDebug(log_ui_camera) << "  Device port chain:" << device.portChain;
                qCDebug(log_ui_camera) << "========================================";

                // Quick check: if device has no camera component and camera is
                // already active, bail out immediately (no expensive enumeration).
                bool hasCameraInfoFromDeviceManager = device.hasCameraDevice();
                if (!hasCameraInfoFromDeviceManager && hasActiveCameraDevice()) {
                    qCDebug(log_ui_camera) << "No camera component and camera already active, skipping";
                    return;
                }

                // FIX: Debounce rapid hotplug events. When a device is quickly
                // unplugged and re-plugged, multiple newDevicePluggedIn signals
                // can fire in succession. Without debouncing, each event queues a
                // separate deferred handler that can interfere with the previous
                // one, leaving the video stream in a broken state.
                //
                // Instead of QTimer::singleShot(0) which fires immediately, we
                // use a 300ms debounce timer. Each new plug event resets the
                // timer, so only the LAST event in a rapid burst actually triggers
                // the expensive camera enumeration + auto-switch.
                if (m_hotplugDebounceTimer) {
                    m_hotplugDebounceTimer->stop();
                    qCDebug(log_ui_camera) << "Cancelling previous pending hotplug debounce timer";
                } else {
                    m_hotplugDebounceTimer = new QTimer(this);
                    m_hotplugDebounceTimer->setSingleShot(true);
                }

                // Capture device info for the debounced handler
                QTimer* timer = m_hotplugDebounceTimer;
                timer->start(300);

                // Disconnect any previous connection, then connect once
                disconnect(timer, &QTimer::timeout, nullptr, nullptr);
                connect(timer, &QTimer::timeout, this, [this, device, hasCameraInfoFromDeviceManager]() {
                    qCDebug(log_ui_camera) << "Hotplug debounce timer fired for port:" << device.portChain;

                    // FIX: Verify the device is still present before doing expensive
                    // camera operations. During rapid hotplug, a deferred handler from
                    // a previous plug event can still be queued when the device is
                    // already unplugged. By checking the current device snapshot, we
                    // avoid trying to switch to a device that's no longer connected.
                    DeviceManager& deviceManager = DeviceManager::getInstance();
                    HotplugMonitor* monitor = deviceManager.getHotplugMonitor();
                    if (monitor) {
                        QList<DeviceInfo> currentDevices = monitor->getLastSnapshot();
                        bool stillPresent = false;
                        for (const auto& d : currentDevices) {
                            if (d.portChain == device.portChain) {
                                stillPresent = true;
                                break;
                            }
                        }
                        if (!stillPresent) {
                            qCWarning(log_ui_camera) << "Device at port" << device.portChain
                                                     << "no longer present, skipping camera auto-switch";
                            return;
                        }
                    }

                    // If there's already an active camera, skip
                    if (hasActiveCameraDevice()) {
                        qCDebug(log_ui_camera) << "Camera already active, skipping auto-switch";
                        return;
                    }

                    // For Windows: Refresh Qt camera device list
                    if (isWindowsPlatform()) {
                        qCDebug(log_ui_camera) << "Windows: Refreshing video inputs before checking for camera device";
                        onVideoInputsChanged();
                    }

                    qCDebug(log_ui_camera) << "Device camera info check:";
                    qCDebug(log_ui_camera) << "  Has camera from DeviceManager:" << hasCameraInfoFromDeviceManager;
                    qCDebug(log_ui_camera) << "  Camera device ID:" << device.cameraDeviceId;
                    qCDebug(log_ui_camera) << "  Camera device path:" << device.cameraDevicePath;

                    // WORKAROUND: Even if DeviceManager didn't populate camera info,
                    // check if QMediaDevices has an Openterface camera available
                    bool hasOpenterfaceCameraInQt = false;
                    if (!hasCameraInfoFromDeviceManager) {
                        qCDebug(log_ui_camera) << "DeviceManager has no camera info - checking QMediaDevices for Openterface camera";
                        QCameraDevice found = findQtOpenterfaceDevice(m_availableCameraDevices);
                        if (!found.isNull()) {
                            hasOpenterfaceCameraInQt = true;
                            qCDebug(log_ui_camera) << "  ✓ Found Openterface camera in Qt:" << found.description();
                        }
                    }

                    if (!hasCameraInfoFromDeviceManager && !hasOpenterfaceCameraInQt) {
                        qCDebug(log_ui_camera) << "Device at port" << device.portChain << "has no camera component, skipping camera auto-switch";
                        return;
                    }

                    if (hasOpenterfaceCameraInQt) {
                        qCDebug(log_ui_camera) << "Using Qt-detected Openterface camera (DeviceManager camera info not available)";
                    } else {
                        qCDebug(log_ui_camera) << "Device has camera component:";
                        qCDebug(log_ui_camera) << "  Camera device ID:" << device.cameraDeviceId;
                        qCDebug(log_ui_camera) << "  Camera device path:" << device.cameraDevicePath;
                    }

                    qCDebug(log_ui_camera) << "Current camera state check:";
                    qCDebug(log_ui_camera) << "  m_currentCameraDevice.isNull():" << m_currentCameraDevice.isNull();
                    qCDebug(log_ui_camera) << "  m_currentCameraPortChain:" << m_currentCameraPortChain;
                    qCDebug(log_ui_camera) << "  hasActiveCameraDevice():" << hasActiveCameraDevice();

                    qCDebug(log_ui_camera) << "No active camera device found, attempting to switch to new device";

                    // If using the Qt-detected camera workaround, auto-switch directly
                    if (hasOpenterfaceCameraInQt && !hasCameraInfoFromDeviceManager) {
                        qCDebug(log_ui_camera) << "Using fallback: switching to Qt-detected Openterface camera";
                        QCameraDevice found = findQtOpenterfaceDevice(m_availableCameraDevices);
                        if (!found.isNull()) {
                            qCDebug(log_ui_camera) << "Switching to Openterface camera:" << found.description();
                            bool switchSuccess = switchToCameraDevice(found, device.portChain);
                            // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
                            // called it internally. Calling it again causes double-start and UI freeze.
                            if (switchSuccess && m_graphicsVideoOutput) {
                                qCInfo(log_ui_camera) << "✓ Camera auto-switched to Openterface device (fallback method)";
                            } else {
                                qCWarning(log_ui_camera) << "✗ Camera auto-switch FAILED (fallback method)";
                            }
                            qCDebug(log_ui_camera) << "========================================";
                            return;
                        }
                    }

                    // Try to auto-switch to the new camera device
                    bool switchSuccess = tryAutoSwitchToNewDevice(device.portChain);
                    if (switchSuccess) {
                        qCInfo(log_ui_camera) << "✓ Camera auto-switched to new device at port:" << device.portChain;
                    } else {
                        qCWarning(log_ui_camera) << "✗ Camera auto-switch FAILED for port:" << device.portChain;
                    }
                    qCDebug(log_ui_camera) << "========================================";
                });
            });
            
    qCDebug(log_ui_camera) << "CameraManager successfully connected to hotplug monitor";
}

void CameraManager::disconnectFromHotplugMonitor()
{
    qCDebug(log_ui_camera) << "Disconnecting CameraManager from hotplug monitor";

    // Stop and clean up the hotplug debounce timer
    if (m_hotplugDebounceTimer) {
        m_hotplugDebounceTimer->stop();
        delete m_hotplugDebounceTimer;
        m_hotplugDebounceTimer = nullptr;
    }

    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (hotplugMonitor) {
        disconnect(hotplugMonitor, nullptr, this, nullptr);
        qCDebug(log_ui_camera) << "CameraManager disconnected from hotplug monitor";
    }
}

void CameraManager::handleFFmpegDeviceDisconnection(const QString& devicePath)
{   
    qCDebug(log_ui_camera) << "Handling FFmpeg device disconnection for:" << devicePath;
    
    // Check if the disconnected device is our current device
    QString currentDeviceId = getCurrentCameraDeviceId();
    if (!currentDeviceId.isEmpty() && 
        (currentDeviceId == devicePath || currentDeviceId.contains(devicePath))) {
        
        qCWarning(log_ui_camera) << "Current FFmpeg device disconnected, attempting recovery";
        
        // Try to find an alternative available camera device
        QList<QCameraDevice> availableDevices = getAvailableCameraDevices();
        QCameraDevice replacementDevice;
        
        for (const QCameraDevice& device : availableDevices) {
            // Skip the disconnected device
            QString deviceId = QString::fromUtf8(device.id());
            if (deviceId == devicePath || deviceId.contains(devicePath)) {
                continue;
            }
            
            // Check if this device is available
#ifndef Q_OS_WIN
            if (auto ffmpegHandler = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                // Convert device ID to device path
                QString testDevicePath;
                if (!deviceId.startsWith("/dev/video")) {
                    bool isNumber = false;
                    int deviceNumber = deviceId.toInt(&isNumber);
                    if (isNumber) {
                        testDevicePath = QString("/dev/video%1").arg(deviceNumber);
                    } else {
                        // Skip devices with unparseable IDs
                        qCDebug(log_ui_camera) << "Skipping device with unparseable ID:" << deviceId;
                        continue;
                    }
                } else {
                    testDevicePath = deviceId;
                }
                
                if (ffmpegHandler->checkCameraAvailable(testDevicePath)) {
                    replacementDevice = device;
                    qCDebug(log_ui_camera) << "Found replacement device:" << device.description();
                    break;
                }
            }
#endif
        }
        
        if (!replacementDevice.isNull()) {
            qCDebug(log_ui_camera) << "Attempting to switch to replacement device";

            // NOTE: Do NOT call stopCamera() here — switchToCameraDevice() already
            // calls it internally (line 1256), and then calls startCamera() (line 1307).
            // Calling stop/start here causes double-start and UI freeze.

            // Switch to the new device
            if (switchToCameraDevice(replacementDevice, QString())) {
                qCInfo(log_ui_camera) << "Successfully switched to replacement device:" << replacementDevice.description();
                // NOTE: Do NOT call startCamera() here — switchToCameraDevice() already
                // called it internally. Calling it again causes double-start and UI freeze.
            } else {
                qCWarning(log_ui_camera) << "Failed to switch to replacement device";
                emit cameraError("Camera device disconnected and no suitable replacement found");
            }
        } else {
            qCWarning(log_ui_camera) << "No suitable replacement device found for disconnected FFmpeg device";
            emit cameraError("Camera device disconnected: " + devicePath);
        }
    } else {
        qCDebug(log_ui_camera) << "Disconnected device is not our current device, ignoring";
    }
}

// ===== Auto-switch retry mechanism (exponential backoff) =====

void CameraManager::startAutoSwitchRetry(const QString& portChain)
{
    cancelAutoSwitchRetry();

    m_autoSwitchRetry.retryCount = 0;
    m_autoSwitchRetry.targetDevicePath = portChain;
    m_autoSwitchRetry.isActive = true;

    if (!m_autoSwitchRetry.retryTimer) {
        m_autoSwitchRetry.retryTimer = new QTimer(this);
        m_autoSwitchRetry.retryTimer->setSingleShot(true);
        connect(m_autoSwitchRetry.retryTimer, &QTimer::timeout,
                this, &CameraManager::executeAutoSwitchRetry);
    }

    int interval = m_autoSwitchRetry.getNextInterval();
    qCInfo(log_ui_camera) << "Starting auto-switch retry - max retries:" << m_autoSwitchRetry.maxRetries
                          << "first retry in:" << interval << "ms";
    m_autoSwitchRetry.retryTimer->start(interval);
}

void CameraManager::cancelAutoSwitchRetry()
{
    if (m_autoSwitchRetry.retryTimer && m_autoSwitchRetry.retryTimer->isActive()) {
        m_autoSwitchRetry.retryTimer->stop();
    }
    m_autoSwitchRetry.isActive = false;
    m_autoSwitchRetry.retryCount = 0;
}

void CameraManager::executeAutoSwitchRetry()
{
    if (!m_autoSwitchRetry.isActive) return;

    m_autoSwitchRetry.retryCount++;
    qCInfo(log_ui_camera) << "Auto-switch retry attempt" << m_autoSwitchRetry.retryCount
                          << "/" << m_autoSwitchRetry.maxRetries;

    // Refresh devices before retry
    refreshAvailableCameraDevices();

    // Try to switch
    bool success = tryAutoSwitchToNewDevice(m_autoSwitchRetry.targetDevicePath);

    if (success) {
        qCInfo(log_ui_camera) << "Auto-switch succeeded on retry" << m_autoSwitchRetry.retryCount;
        cancelAutoSwitchRetry();
        return;
    }

    // Not found - schedule next retry
    if (m_autoSwitchRetry.retryCount < m_autoSwitchRetry.maxRetries) {
        int nextInterval = m_autoSwitchRetry.getNextInterval();
        qCDebug(log_ui_camera) << "Scheduling next retry in" << nextInterval << "ms";
        m_autoSwitchRetry.retryTimer->start(nextInterval);
    } else {
        qCWarning(log_ui_camera) << "Auto-switch FAILED after" << m_autoSwitchRetry.maxRetries << "retries for" << m_autoSwitchRetry.targetDevicePath;
        m_autoSwitchRetry.isActive = false;
        m_autoSwitchRetry.retryCount = 0;
    }
}
