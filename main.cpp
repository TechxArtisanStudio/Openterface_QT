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
#include "global.h"
#include "target/KeyboardLayouts.h"
#include "ui/languagemanager.h"
#include <QCoreApplication>
#include <QtPlugin>

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
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
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


void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context)


    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QThread *currentThread = QThread::currentThread();
    QString threadName = currentThread->objectName().isEmpty() ? QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId())) : currentThread->objectName();
    QString txt = QString("[%1][%2] ").arg(timestamp).arg(threadName);
    
    switch (type) {
        case QtDebugMsg:
            txt += QString("{Debug}: %1").arg(msg);
            break;
        case QtWarningMsg:
            txt += QString("{Warning}: %1").arg(msg);
            break;
        case QtCriticalMsg:
            txt += QString("{Critical}: %1").arg(msg);
            break;
        case QtFatalMsg:
            txt += QString("{Fatal}: %1").arg(msg);
            break;
        case QtInfoMsg:
            txt += QString("{Info}: %1").arg(msg);
            break;
    }

    // QFile outFile("log.txt");
    // outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    // QTextStream textStream(&outFile);
    // textStream << txt << endl;
    
    std::cout << txt.toStdString() << std::endl;
}

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

void setupEnv(){
#ifdef Q_OS_LINUX
    // Only set QT_QPA_PLATFORM when not provided by the user
    const QByteArray currentPlatform = qgetenv("QT_QPA_PLATFORM");
    
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
        // For dynamic builds, prefer XCB if DISPLAY is available
        if (!x11Display.isEmpty()) {
            qputenv("QT_QPA_PLATFORM", "xcb");
            qDebug() << "Dynamic build: Set QT_QPA_PLATFORM to xcb (DISPLAY available)";
        } else if (!waylandDisplay.isEmpty()) {
            qputenv("QT_QPA_PLATFORM", "wayland");
            qDebug() << "Dynamic build: Set QT_QPA_PLATFORM to wayland (WAYLAND_DISPLAY available)";
        } else {
            qputenv("DISPLAY", ":0");
            qputenv("QT_QPA_PLATFORM", "xcb");
            qDebug() << "Dynamic build: No display detected, trying DISPLAY=:0 with xcb platform";
        }
        #endif
    } else {
        qDebug() << "Current QT_QPA_PLATFORM:" << currentPlatform;
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
        qputenv("GST_PLUGIN_PATH", "/usr/lib/gstreamer-1.0");
        qputenv("GST_PLUGIN_SYSTEM_PATH", "/usr/lib/gstreamer-1.0");
        
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
    qDebug() << "Start openterface...";
    
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
    qDebug() << "GStreamer initialized successfully";
    
    // Install custom GLib log handler to suppress non-critical messages
    g_log_set_default_handler(suppressGLibMessages, nullptr);
    #endif
    setupEnv();
    qDebug() << "Creating QApplication...";
    QApplication app(argc, argv);

    // set style accroding to system palette
    QPalette systemPalette = QApplication::palette();
    app.setPalette(systemPalette);
    app.setStyle(QStyleFactory::create("Fusion"));
    
    qInstallMessageHandler(customMessageHandler);

    QCoreApplication::setApplicationName("Openterface Mini-KVM");
    QCoreApplication::setOrganizationName("TechxArtisan");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    qDebug() << "Show window now";
    app.setWindowIcon(QIcon("://images/icon_32.png"));
    
    // Check if the environment is properly set up
    if (EnvironmentSetupDialog::autoEnvironmentCheck() && !EnvironmentSetupDialog::checkEnvironmentSetup()) {
        EnvironmentSetupDialog envDialog;
        qDebug() << "Environment setup dialog opened";
        if (envDialog.exec() == QDialog::Rejected) {
            qDebug() << "Driver dialog rejected";
            QApplication::quit(); // Quit the application if the dialog is rejected
            return 0;
        }
    } 
    
    // load the settings
    qDebug() << "Loading settings";
    GlobalSetting::instance().loadLogSettings();
    GlobalSetting::instance().loadVideoSettings();
    // Apply media backend setting after settings are loaded
    applyMediaBackendSetting();
    // onVideoSettingsChanged(GlobalVar::instance().getCaptureWidth(), GlobalVar::instance().getCaptureHeight());
    LogHandler::instance().enableLogStore();

    // Load keyboard layouts from resource file
    QString keyboardConfigPath = ":/config/keyboards";
    KeyboardLayoutManager::getInstance().loadLayouts(keyboardConfigPath);
    
    
    // writeLog("Environment setup completed");
    LanguageManager languageManager(&app);
    languageManager.initialize("en");
    // writeLog("languageManager initialized");
    MainWindow window(&languageManager);
    // writeLog("Application started");
    window.show();

    int result = app.exec();
    
    // Clean up GStreamer
    #ifdef HAVE_GSTREAMER
    gst_deinit();
    qDebug() << "GStreamer deinitialized";
    #endif
    
    
    return result;
};
