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

#include "ui/mainwindow.h"
#include "ui/loghandler.h"
#include "ui/advance/envdialog.h"
#include "ui/globalsetting.h"
#include "ui/splashscreen.h"
#include "global.h"
#include "target/KeyboardLayouts.h"
#include "ui/languagemanager.h"
#include "ui/customkey/customkeymanager.h"
#include <QMetaType>
#include <QCoreApplication>
#include <QtPlugin>
#include <QPixmap>
#include <QTime>
#include <QEventLoop>
#include <QThread>
#include <QObject>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <cstdio>

// Stdio MCP transport support (headless mode for Claude Code)
#include "server/mcp/mcpServer.h"
#include "device/DeviceManager.h"
#include "serial/SerialPortManager.h"
#include "host/cameramanager.h"
#include "video/videohid.h"


#ifdef Q_OS_WIN
#include <windows.h>
#include <crtdbg.h>
#endif

// Import static Qt plugins only when building with static plugins
#if defined(QT_STATIC) || defined(QT_STATICPLUGIN)
// Image format plugins
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QSvgPlugin)

// Platform plugins (Linux)
#ifdef Q_OS_LINUX
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
// Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)  // Commented out - not available in shared Qt build
// Note: Wayland plugin may not be properly built in static builds
// Q_IMPORT_PLUGIN(QWaylandEglPlatformIntegrationPlugin)
#endif
#endif

// Define global shutdown flag
QAtomicInteger<int> g_applicationShuttingDown(0);

// GStreamer includes
#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <glib.h>  // For GLib log handling
#endif

#include <unistd.h>
#include <unistd.h>

void writeLog(const QString &message){
    QFile logFile("startup_log.txt");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        out << "[" << timestamp << "] " << message << "\n";
        logFile.close();
    } else {
        qDebug() << "Failed to open log file:" << logFile.errorString();
    }
}

#ifdef HAVE_GSTREAMER
// Custom GLib log handler to suppress non-critical GStreamer messages
void suppressGLibMessages(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{
    Q_UNUSED(user_data)
    
    // Suppress specific known non-critical messages
    if (log_domain && (strcmp(log_domain, "GStreamer") == 0)) {
        if (strstr(message, "gst_value_set_int_range_step: assertion") ||
            strstr(message, "gst_alsa_device_new: assertion")) {
            return; // Suppress these specific messages
        }
    }
    
    // For other critical messages, still show them but with reduced verbosity
    if (log_level & G_LOG_LEVEL_CRITICAL || log_level & G_LOG_LEVEL_ERROR) {
        // Only show if it's not one of the known harmless messages
        if (!strstr(message, "gst_value_set_int_range_step") && 
            !strstr(message, "gst_alsa_device_new")) {
            fprintf(stderr, "%s\n", message);
        }
    }
}
#endif

void setupEnv(){
#ifdef Q_OS_LINUX
    // Only set QT_QPA_PLATFORM when not provided by the user or launcher script
    const QByteArray currentPlatform = qgetenv("QT_QPA_PLATFORM");
    const QByteArray launcherDetected = qgetenv("OPENTERFACE_LAUNCHER_PLATFORM");
    
    if (currentPlatform.isEmpty()) {
        // Check for available display systems
        const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
        const QByteArray x11Display = qgetenv("DISPLAY");
        
        // For static builds, be more conservative about platform selection
        #if defined(QT_STATIC) || defined(QT_STATICPLUGIN)
        qDebug() << "Static build detected - using conservative platform selection";
        if (!x11Display.isEmpty()) {
            qputenv("QT_QPA_PLATFORM", "xcb");
            qDebug() << "Static build: Set QT_QPA_PLATFORM to xcb (DISPLAY available)";
        } else {
            // Try to set DISPLAY and use XCB for static builds
            qputenv("DISPLAY", ":0");
            qputenv("QT_QPA_PLATFORM", "xcb");
            qDebug() << "Static build: No display detected, trying DISPLAY=:0 with xcb platform";
        }
        #else
        // For dynamic builds, prefer XCB for better compatibility
        // Only use Wayland if explicitly set by launcher script
        if (!launcherDetected.isEmpty()) {
            qDebug() << "Dynamic build: Using launcher script's platform detection:" << launcherDetected;
        } else if (!x11Display.isEmpty()) {
            // DISPLAY is set - use XCB
            qputenv("QT_QPA_PLATFORM", "xcb");
            qDebug() << "Dynamic build: Set QT_QPA_PLATFORM to xcb (DISPLAY available)";
        } else if (!waylandDisplay.isEmpty()) {
            // Fallback to Wayland only if X11 is not available
            qputenv("QT_QPA_PLATFORM", "wayland");
            qDebug() << "Dynamic build: Set QT_QPA_PLATFORM to wayland (WAYLAND_DISPLAY available, X11 not found)";
        } else {
            // No display found, try default settings
            qputenv("DISPLAY", ":0");
            qputenv("QT_QPA_PLATFORM", "xcb");
            qDebug() << "Dynamic build: No display detected, trying DISPLAY=:0 with xcb platform";
        }
        #endif
    } else {
        qDebug() << "QT_QPA_PLATFORM already set by launcher or user:" << currentPlatform;
    }
#endif
}

void applyMediaBackendSetting(){
#ifdef Q_OS_LINUX
    QString originalMediaBackend = qgetenv("QT_MEDIA_BACKEND");
    qDebug() << "Original QT Media Backend:" << originalMediaBackend;
    
    // Get the media backend setting from GlobalSetting
    QString mediaBackend = GlobalSetting::instance().getMediaBackend();
    
    // Handle GStreamer-specific environment settings
    if (mediaBackend == "gstreamer") {
        // Set GStreamer debug level to lowest (0 = GST_LEVEL_NONE) to suppress all messages
        qputenv("GST_DEBUG", "0");
        
        // Disable color output for cleaner logs
        qputenv("GST_DEBUG_NO_COLOR", "1");
        
        // Ensure proper GStreamer registry handling
        qputenv("GST_REGISTRY_REUSE_PLUGIN_SCANNER", "no");
        
        // Set GStreamer to handle object lifecycle more carefully
        qputenv("GST_DEBUG_DUMP_DOT_DIR", "");
        
        // Suppress non-critical GStreamer log messages from the start
        qputenv("GST_DEBUG", "0");  // Complete silence - no GStreamer debug messages
        
        // Suppress GLib critical messages that occur during device enumeration
        qputenv("G_MESSAGES_DEBUG", "");  // Disable all GLib debug messages
        
        // Prevent GStreamer from using problematic plugins that might cause object ref issues
        qputenv("GST_PLUGIN_FEATURE_RANK", "autovideosink:MAX,autoaudiosink:MAX,alsasink:NONE,pulsesink:PRIMARY");
        
        // Force proper cleanup timing
        qputenv("G_DEBUG", "gc-friendly");
        
        // Disable problematic ALSA device scanning that causes errors
        qputenv("GST_ALSA_DISABLE_PERIOD_ADJUSTMENT", "1");
        
        // Set audio policy to be more conservative
        qputenv("GST_AUDIO_DISABLE_FORMATS", "");
        
        // Force PulseAudio over ALSA to avoid device access issues
        qputenv("GST_AUDIO_SYSTEM_PULSE", "1");
        
        // Reduce audio device scanning verbosity
        qputenv("PULSE_DEBUG", "0");
        
        // Additional GStreamer environment variables for better video handling
        qputenv("GST_V4L2_USE_LIBV4L2", "1");
        
        // IMPORTANT: Do NOT override GST_PLUGIN_PATH here!
        // The launcher script has already set it correctly for the package type:
        // - RPM packages: /usr/lib/openterfaceqt/gstreamer/gstreamer-1.0
        // - AppImage packages: appDir/../lib/gstreamer-1.0
        // Overriding it here causes version conflicts on Fedora
        
        if (!qgetenv("GST_PLUGIN_PATH").isEmpty()) {
            qDebug() << "✓ Using GStreamer plugins from launcher environment:";
            qDebug() << "  GST_PLUGIN_PATH=" << qgetenv("GST_PLUGIN_PATH");
        } else {
            qDebug() << "⚠ WARNING: GST_PLUGIN_PATH not set, GStreamer may use incorrect system plugins";
        }
        
        // Ensure video output works correctly
        qputenv("GST_VIDEO_OVERLAY", "1");
        
        qDebug() << "Applied enhanced GStreamer-specific environment settings for video compatibility";
        // gstreamer not compatible with QT_MEDIA_BACKEND, so we set it to empty
    } else{
        // For other media backends, we can set a default or leave it empty
        qputenv("QT_MEDIA_BACKEND", mediaBackend.toUtf8());
        qDebug() << "Set QT_MEDIA_BACKEND to:" << mediaBackend;
    }
#endif
}

int main(int argc, char *argv[])
{
    // TEMP: Early startup logging
    QFile earlyLog("C:/openterface_startup.log");
    earlyLog.open(QIODevice::WriteOnly | QIODevice::Append);
    if (earlyLog.isOpen()) {
        QTextStream outs(&earlyLog);
        outs << "[EARLY] main() entered\n";
        outs.flush();
    }

    #ifdef Q_OS_WIN
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF);
    #endif
    qInfo() << "Start openterface...";
    
    // Parse command-line arguments early to check for --skip-env-check
    bool skipEnvironmentCheck = false;
    bool autoStartMcp = false;
    bool mcpStdioMode = false;
    int mcpSsePort = 0;  // 0 = disabled
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--skip-env-check") == 0) {
            skipEnvironmentCheck = true;
            qWarning() << "Skip environment check flag detected";
        } else if (strcmp(argv[i], "--mcp-start") == 0) {
            autoStartMcp = true;
            qWarning() << "Auto-start MCP Server flag detected";
        } else if (strcmp(argv[i], "--mcp-stdio") == 0) {
            mcpStdioMode = true;
            qWarning() << "MCP stdio transport mode detected";
        } else if (strcmp(argv[i], "--mcp-sse-port") == 0) {
            if (i + 1 >= argc) {
                qCritical() << "--mcp-sse-port requires a port number argument";
                return 1;
            }
            mcpSsePort = atoi(argv[++i]);
            if (mcpSsePort <= 0 || mcpSsePort > 65535) {
                qCritical() << "Invalid --mcp-sse-port value:" << argv[i];
                return 1;
            }
            qWarning() << "MCP SSE transport on port" << mcpSsePort;
        }
    }

    // MCP headless mode: if --mcp-stdio or --mcp-sse-port, run a minimal Qt event
    // loop with the MCP server — no MainWindow, no GUI window.
    // We use QApplication (not QCoreApplication) because KeyboardManager calls
    // QInputMethod::locale() which requires GUI initialization.
    // We use the offscreen platform so no real display is needed.
    bool mcpHeadlessMode = !autoStartMcp && (mcpStdioMode || (mcpSsePort > 0));
    if (mcpHeadlessMode) {
        // Use offscreen platform — provides QInputMethod without needing a real display
        qputenv("QT_QPA_PLATFORM", "offscreen");

        // Redirect all logging to stderr so we can see what's happening
        qputenv("QT_LOGGING_RULES", "*.debug=true");

        QApplication app(argc, argv);
        qInfo() << "Starting MCP server in stdio transport mode (offscreen)...";

        // Load keyboard layouts — required by KeyboardManager (used by MCP tools)
        qInfo() << "Loading keyboard layouts for stdio mode...";
        KeyboardLayoutManager::getInstance().loadLayouts(":/config/keyboards");
        qInfo() << "Keyboard layouts loaded";

        // Create CameraManager on the heap — must outlive McpServer so capture_screen works.
        // Parented to &app for automatic cleanup on exit.
        CameraManager* cameraManager = new CameraManager(&app);
        qInfo() << "CameraManager created for stdio mode";

        // Start VideoHid — required to initialize the video chip (MS2109/MS2130S) HID
        // interface so the HDMI input is routed to the USB capture device. Without this,
        // the capture device produces valid but black frames. GUI mode does the same in
        // MainWindowInitializer::deferredInitializeCamera().
        fprintf(stderr, "[DEBUG] Starting VideoHid...\n");
        fprintf(stderr, "[DEBUG] Current port chain: '%s'\n",
                GlobalSetting::instance().getOpenterfacePortChain().toUtf8().constData());
        VideoHid::getInstance().start();
        fprintf(stderr, "[DEBUG] VideoHid started, HID device opened: %s\n",
                VideoHid::getInstance().isOpen() ? "yes" : "no");
        fprintf(stderr, "[DEBUG] VideoHid currentHIDDevicePath: '%s'\n",
                VideoHid::getInstance().getCurrentHIDDevicePath().toUtf8().constData());
        // Check HDMI connection status
        QThread::msleep(200);  // Brief delay for HID to stabilize
        bool hdmiConnected = VideoHid::getInstance().isHdmiConnected();
        fprintf(stderr, "[DEBUG] HDMI connected: %s\n", hdmiConnected ? "yes" : "no");

        // Read input resolution from MS2109
        auto resolution = VideoHid::getInstance().getResolution();
        fprintf(stderr, "[DEBUG] MS2109 detected input resolution: %dx%d\n", resolution.first, resolution.second);

        fprintf(stderr, "[DEBUG] GPIO0 (hard switch): %s\n",
                VideoHid::getInstance().getGpio0() ? "target" : "host");
        fprintf(stderr, "[DEBUG] SPDIFOUT (soft switch): %s\n",
                VideoHid::getInstance().getSpdifout() ? "target" : "host");
        QString fw = QString::fromStdString(VideoHid::getInstance().getFirmwareVersion());
        fprintf(stderr, "[DEBUG] Firmware version: %s\n", fw.toUtf8().constData());

        // CRITICAL: Explicitly set the SPDIFOUT register in stdio mode.
        // GUI mode does this via setupEventCallbacks() -> VideoHid::setEventCallback(m_mainWindow)
        // which triggers setSpdifout() in the async firmware read block. Without this,
        // the MS2109 chip may not output valid video.
        fprintf(stderr, "[DEBUG] Explicitly setting SPDIFOUT register...\n");
        bool spdifout = VideoHid::getInstance().getSpdifout();
        fprintf(stderr, "[DEBUG] Current SPDIFOUT=%d\n", spdifout ? 1 : 0);
        VideoHid::getInstance().setSpdifout(spdifout);
        fprintf(stderr, "[DEBUG] SPDIFOUT register set\n");
        QThread::msleep(100);

        qInfo() << "VideoHid started";

        // Discover and connect to device hardware
        qInfo() << "Discovering Openterface devices...";
        QList<DeviceInfo> devices = DeviceManager::getInstance().discoverDevices();
        fprintf(stderr, "[DEBUG] Discovered %d devices\n", devices.size());
        if (!devices.isEmpty()) {
            qInfo() << "Found" << devices.size() << "device(s)";
            DeviceInfo device = devices.first();
            fprintf(stderr, "[DEBUG] First device: portChain='%s', hidDevicePath='%s', cameraDevicePath='%s'\n",
                    device.portChain.toUtf8().constData(),
                    device.hidDevicePath.toUtf8().constData(),
                    device.cameraDevicePath.toUtf8().constData());
            qInfo() << "Switching to device:" << device.getInterfaceSummary();
            auto result = DeviceManager::getInstance().switchToDeviceByPortChainWithCamera(
                device.portChain, cameraManager);
            fprintf(stderr, "[DEBUG] switchToDeviceByPortChainWithCamera result: success=%d, message='%s'\n",
                    result.success, result.statusMessage.toUtf8().constData());
            if (result.success) {
                qInfo() << "Device connected successfully:" << result.statusMessage;
            } else {
                qWarning() << "Device connection issue:" << result.statusMessage;
            }

            // Wait a bit for camera to initialize
            QThread::msleep(1000);
            fprintf(stderr, "[DEBUG] Checking if camera has frames...\n");
            QImage testFrame = cameraManager->getLatestOriginalFrame();
            fprintf(stderr, "[DEBUG] Test frame: isNull=%d, size=%dx%d\n",
                    testFrame.isNull(), testFrame.width(), testFrame.height());

            // Wait for serial port to be ready (it's initialized asynchronously)
            qInfo() << "Waiting for serial port to initialize...";
            QEventLoop waitLoop;
            QTimer timeoutTimer;
            timeoutTimer.setSingleShot(true);
            bool serialReady = false;

            QObject::connect(&SerialPortManager::getInstance(), &SerialPortManager::serialPortConnectionSuccess,
                           &waitLoop, [&waitLoop, &serialReady]() {
                               qInfo() << "Serial port is ready!";
                               serialReady = true;
                               waitLoop.quit();
                           });

            QObject::connect(&timeoutTimer, &QTimer::timeout, &waitLoop, [&waitLoop]() {
                qWarning() << "Timeout waiting for serial port";
                waitLoop.quit();
            });

            timeoutTimer.start(5000); // 5 second timeout
            waitLoop.exec();
            timeoutTimer.stop();

            if (!serialReady) {
                qWarning() << "Serial port did not become ready within timeout";
            } else {
                // Verify device is responsive by sending CMD_GET_INFO
                qInfo() << "Verifying device responsiveness...";
                QByteArray testCmd = QByteArray::fromHex("57 ab 00 01 00");
                bool deviceReady = false;

                for (int attempt = 0; attempt < 3; attempt++) {
                    SerialPortManager::getInstance().sendCommandAsync(testCmd, false);
                    QThread::msleep(200);  // Wait for response

                    // Check if we got a response (device sets ready flag)
                    if (SerialPortManager::getInstance().isPortReady()) {
                        qInfo() << "Device is responsive (attempt" << (attempt + 1) << ")";
                        deviceReady = true;
                        break;
                    }
                    qWarning() << "Device not responsive, retrying... (attempt" << (attempt + 1) << ")";
                }

                if (!deviceReady) {
                    qWarning() << "WARNING: Device did not respond to verification command";
                    qWarning() << "Commands may not work. Check device connection and firmware.";
                }
            }
        } else {
            qWarning() << "No Openterface devices found - trying direct serial port access...";

            // Fallback: try to open /dev/ttyACM0 directly for CH32V208 devices when
            // the companion device (1A86:E329) is not recognized by the device discovery.
            QString directPort = "/dev/ttyACM0";
            if (QFile::exists(directPort)) {
                qInfo() << "Found serial port at" << directPort << "- opening directly";
                bool opened = SerialPortManager::getInstance().openPort(directPort, 115200);
                if (opened) {
                    qInfo() << "Serial port opened successfully";
                    // Force ready state for serial port and command coordinator
                    // since openPort alone doesn't emit serialPortConnectionSuccess
                    Q_EMIT SerialPortManager::getInstance().serialPortConnectionSuccess(directPort);
                    // Give it a moment to process
                    QThread::msleep(500);
                } else {
                    qWarning() << "Failed to open serial port directly";
                }
            } else {
                qWarning() << "No serial port found at /dev/ttyACM0";
            }
        }

        // Wait for camera to produce its first frame (FFmpeg backend starts async)
        qInfo() << "Waiting for camera to produce first frame...";
        {
            int maxWaitMs = 5000;
            int waitedMs = 0;
            while (waitedMs < maxWaitMs && cameraManager->getLatestOriginalFrame().isNull()) {
                QThread::msleep(200);
                waitedMs += 200;
                QCoreApplication::processEvents();
            }
            if (cameraManager->getLatestOriginalFrame().isNull()) {
                qWarning() << "Camera did not produce a frame within" << maxWaitMs << "ms";
                qWarning() << "capture_screen tool may return errors until a frame is available";
            } else {
                QImage frame = cameraManager->getLatestOriginalFrame();
                qInfo() << "Camera ready! First frame:" << frame.width() << "x" << frame.height();
            }
        }

        McpServer* mcpServer = new McpServer(&app);
        mcpServer->setCameraManager(cameraManager);

        // Start stdio transport if requested
        if (mcpStdioMode) {
            if (!mcpServer->startStdio()) {
                qCritical() << "Failed to start MCP stdio transport";
                return 1;
            }
        }

        // Start SSE transport if requested
        if (mcpSsePort > 0) {
            if (!mcpServer->startSse(static_cast<quint16>(mcpSsePort))) {
                qCritical() << "Failed to start MCP SSE transport on port" << mcpSsePort;
                return 1;
            }
        }

        // Run forever; the only exit path is stdin EOF (handled by stop()) or
        // process termination.
        int result = app.exec();

        // Clean up the MCP server
        delete mcpServer;
        return result;
    }

    setupEnv();
    
    qInfo() << "Creating QApplication...";
    QApplication app(argc, argv);

    // Register custom types for QVariant
    qRegisterMetaType<QList<KeyStep>>("QList<KeyStep>");
    qRegisterMetaType<QList<int>>("QList<int>");

    // set style accroding to system palette
    QPalette systemPalette = QApplication::palette();
    app.setPalette(systemPalette);
    app.setStyle(QStyleFactory::create("Fusion"));
    
    QCoreApplication::setApplicationName("Openterface Mini-KVM");
    QCoreApplication::setOrganizationName("TechxArtisan");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    qInfo() << "Show window now";
    app.setWindowIcon(QIcon("://images/icon_32.png"));

    // Create splash screen after environment setup
    // Create splash screen with SVG or fallback image
    QPixmap pixmap(":/images/openterface-splash.svg");
    if (pixmap.isNull()) {
        // SVG failed to load, create a simple splash screen
        qWarning() << "Failed to load splash screen image, using fallback";
        pixmap = QPixmap(800, 600);
        pixmap.fill(QColor(15, 9, 9)); // Dark background from Openterface branding
    }
    
    // Create and show the splash screen as a pointer so it persists
    SplashScreen* splash = new SplashScreen(pixmap);
    splash->show();
    splash->raise();
    splash->activateWindow();
    
    qInfo() << "Splash screen shown, starting initialization";
    
    // Start the loading animation
    splash->showLoadingMessage();
    qInfo() << "Animation started";
    
    // Process events to show splash screen
    app.processEvents();
    
    // Load settings immediately (fast operation)
    qInfo() << "Loading settings...";
    GlobalSetting::instance().loadLogSettings();
    GlobalSetting::instance().loadVideoSettings();
    applyMediaBackendSetting();
    LogHandler::instance().enableLogStore();
    
    // Load keyboard layouts immediately - required for keyboard functionality
    qInfo() << "Loading keyboard layouts...";
    QString keyboardConfigPath = ":/config/keyboards";
    KeyboardLayoutManager::getInstance().loadLayouts(keyboardConfigPath);
    
    // Process events to keep UI responsive
    app.processEvents();
    
    qInfo() << "Creating main window...";
    
    // Create main window and language manager
    LanguageManager* languageManager = new LanguageManager(&app);
    languageManager->initialize("en");
    MainWindow* window = new MainWindow(languageManager);
    
    // Stop the splash animation and close it
    splash->hideLoadingMessage();
    
    // Show the main window and bring it to top
    window->show();
    window->raise();
    window->activateWindow();
    splash->finish(window);
    
    // Clean up splash screen
    splash->deleteLater();
    
    qInfo() << "Main window shown";
    
    // Defer device menu setup (device enumeration) - improves startup time
    // Hotplug monitor is already connected, so new devices will be detected
    QTimer::singleShot(5, window, [window]() {
        qInfo() << "Setting up device menu...";
        window->deferredSetupCoordinators();
        qInfo() << "Device menu setup complete";
    });
    
    // Defer camera, audio, and VideoHid initialization (improves startup time)
    // This was blocking startup for ~500ms
    QTimer::singleShot(150, window, [window]() {
        qInfo() << "Initializing camera and audio...";
        window->deferredInitializeCamera();
        qInfo() << "Camera and audio initialization started";
    });

    // Auto-start MCP Server if --mcp-start flag is present
    if (autoStartMcp) {
        // Capture port for the lambda (use 0 to indicate SSE disabled)
        int capturedSsePort = mcpSsePort;
        // Wait longer for camera initialization to complete before starting MCP server
        // Camera initialization happens in deferredInitializeCamera (150ms delay)
        // and may take additional time for device auto-selection and capture start
        QTimer::singleShot(1500, window, [window, capturedSsePort]() {
            qInfo() << "Auto-starting MCP Server (--mcp-start)...";
            
            // Wait for camera frame to be available (similar to headless mode)
            const int maxWaitMs = 5000;  // 5 seconds timeout
            const int pollIntervalMs = 200;
            int waitedMs = 0;
            
            qInfo() << "Waiting for camera frame to be available...";
            
            while (waitedMs < maxWaitMs) {
                // Check if camera has frame
                if (window->getCameraManager() && 
                    !window->getCameraManager()->getLatestOriginalFrame().isNull()) {
                    QImage frame = window->getCameraManager()->getLatestOriginalFrame();
                    qInfo() << "Camera ready! First frame:" << frame.width() << "x" << frame.height();
                    break;
                }
                
                QThread::msleep(pollIntervalMs);
                waitedMs += pollIntervalMs;
                QCoreApplication::processEvents();
            }
            
            if (waitedMs >= maxWaitMs) {
                qWarning() << "Timeout waiting for camera frame (" << maxWaitMs << "ms)";
                qWarning() << "MCP server will start, but capture_screen may return errors initially";
            }
            
            // Now initialize MCP server
            window->initMcpServer();

            if (capturedSsePort > 0) {
                // Start SSE transport on specified port
                qInfo() << "Starting MCP SSE on port" << capturedSsePort;
                bool ok = window->getMcpServer()->startSse(
                    static_cast<quint16>(capturedSsePort), QHostAddress::Any);
                if (ok) {
                    qInfo() << "MCP SSE server started successfully";
                } else {
                    qWarning() << "Failed to start MCP SSE server";
                }
            } else {
                // Default to stdio transport
                window->toggleMcpServer(true);
            }
        });
    }
    
    // Initialize GStreamer before Qt application
    #ifdef HAVE_GSTREAMER
    GError* gst_error = nullptr;
    if (!gst_init_check(&argc, &argv, &gst_error)) {
        if (gst_error) {
            qCritical() << "Failed to initialize GStreamer:" << gst_error->message;
            g_error_free(gst_error);
        } else {
            qCritical() << "Failed to initialize GStreamer: Unknown error";
        }
        return -1;
    }
    
    // Install custom GLib log handler to suppress non-critical messages
    g_log_set_default_handler(suppressGLibMessages, nullptr);
    #endif
    
    // Defer environment check to after window is shown (improves startup time)
    // Run environment check in background after a short delay
    if (!skipEnvironmentCheck && EnvironmentSetupDialog::autoEnvironmentCheck()) {
        QTimer::singleShot(500, [&app]() {
            qInfo() << "Running deferred environment check...";
            if (!EnvironmentSetupDialog::checkEnvironmentSetup()) {
                // Show environment dialog on main thread
                QMetaObject::invokeMethod(&app, []() {
                    EnvironmentSetupDialog envDialog;
                    qInfo() << "Environment setup dialog opened";
                    if (envDialog.exec() == QDialog::Rejected) {
                        qInfo() << "Driver dialog rejected - continuing anyway";
                    }
                }, Qt::QueuedConnection);
            }
        });
    }

    qInfo() << "Entering application event loop (app.exec())";
    int result = app.exec();
    
    qInfo() << "Application event loop exited with code:" << result;
    qInfo() << "Beginning final cleanup...";
    
    // Clean up GStreamer
    #ifdef HAVE_GSTREAMER
    gst_deinit();
    qDebug() << "GStreamer deinitialized";
    #endif
    
    qInfo() << "Application cleanup complete, returning" << result;
    return result;
}
