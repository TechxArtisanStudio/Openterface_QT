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


#include <iostream>
#include <QApplication>
#include <QIcon>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QLoggingCategory>
#include <QStyleFactory>
#include <QDir>
#include <QFile>
#include <QTextStream>


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

void setupEnv(){
#ifdef Q_OS_LINUX
    // Check if QT_QPA_PLATFORM is not set, and set it to "xcb" if it's empty
    if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
        qputenv("QT_QPA_PLATFORM", "xcb");
        qDebug() << "Set QT_QPA_PLATFORM to xcb";
    } else {
        qDebug() << "Current QT_QPA_PLATFORM:" << qgetenv("QT_QPA_PLATFORM");
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
        // Set GStreamer debug level to reduce verbose output but catch critical errors
        qputenv("GST_DEBUG", "1,qt6media:3");
        
        // Disable color output for cleaner logs
        qputenv("GST_DEBUG_NO_COLOR", "1");
        
        // Ensure proper GStreamer registry handling
        qputenv("GST_REGISTRY_REUSE_PLUGIN_SCANNER", "no");
        
        // Set GStreamer to handle object lifecycle more carefully
        qputenv("GST_DEBUG_DUMP_DOT_DIR", "");
        
        // Prevent GStreamer from using problematic plugins that might cause object ref issues
        qputenv("GST_PLUGIN_FEATURE_RANK", "qt6videosink:MAX,qt6audiosink:MAX");
        
        // Force proper cleanup timing
        qputenv("G_DEBUG", "gc-friendly");
        
        qDebug() << "Applied GStreamer-specific environment settings for object safety";
    }
    
    qputenv("QT_MEDIA_BACKEND", mediaBackend.toUtf8());
    QString newMediaBackend = qgetenv("QT_MEDIA_BACKEND");
    qDebug() << "Current QT Media Backend set to:" << newMediaBackend;
#endif
}

int main(int argc, char *argv[])
{
    qDebug() << "Start openterface...";
    setupEnv();
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

    return app.exec();
};
