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



#include "loghandler.h"
#include "globalsetting.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif
LogHandler::LogHandler(QObject *parent)
    : QObject(parent)
{
}

LogHandler& LogHandler::instance()
{
    static LogHandler instance;
    return instance;
}

void LogHandler::enableLogStore()
{
    // Don't log here before handler is installed to avoid duplication
    static QSettings settings("Techxartisan", "Openterface");
    bool storeLog = settings.value("log/storeLog", false).toBool();
    
    if (storeLog)
    {
        qInstallMessageHandler(fileMessageHandler);
    }
    else
    {
        qInstallMessageHandler(customMessageHandler);      // Install custom handler for console output
    }
}

void LogHandler::fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    // qDebug() << "Write log to file now";
    static QSettings settings("Techxartisan", "Openterface");
    QString logFilePath = settings.value("log/logFilePath").toString();
    QFile outFile(logFilePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        return;
    }

    QTextStream ts(&outFile);
    
    // qDebug() << "Write log to file test";
    // Get the category name
    const char* categoryName = context.category;
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QThread *currentThread = QThread::currentThread();
    QString threadName;
    
    // Get thread name: prefer objectName if set, otherwise try to identify main thread
    if (!currentThread->objectName().isEmpty()) {
        threadName = currentThread->objectName();
    } else if (currentThread == QCoreApplication::instance()->thread()) {
        threadName = "MainThread";
    } else {
        // Fall back to thread ID for unnamed worker threads
        threadName = QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId()));
    }
    
    QString category = categoryName ? QString(categoryName) : "default";
    QString txt;
    switch (type)
    {
    case QtDebugMsg:
        txt = QString("[%1][D][%2] %3").arg(threadName).arg(category).arg(msg);
        break;
    case QtWarningMsg:
        txt = QString("[%1][W][%2] %3").arg(threadName).arg(category).arg(msg);
        break;
    case QtCriticalMsg:
        txt = QString("[%1][C][%2] %3").arg(threadName).arg(category).arg(msg);
        break;
    case QtFatalMsg:
        txt = QString("[%1][F][%2] %3").arg(threadName).arg(category).arg(msg);
        break;
    default:
        txt = QString("[%1][U][%2] %3").arg(threadName).arg(category).arg(msg);
        break;
    }

    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    ts << txt << "\n";
    ts.flush();
}


void LogHandler::customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Q_UNUSED(context)

    // Suppress specific Qt warnings that are not useful
    if (msg.contains("QWidget::paintEngine")) {
        return; // Skip logging this warning
    }

    const char* categoryName = context.category;
    QString category = categoryName ? QString(categoryName) : "opf.default.msg";
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QThread *currentThread = QThread::currentThread();
    QString threadName;
    
    // Get thread name: prefer objectName if set, otherwise try to identify main thread
    if (!currentThread->objectName().isEmpty()) {
        threadName = currentThread->objectName();
    } else if (currentThread == QCoreApplication::instance()->thread()) {
        threadName = "MainThread";
    } else {
        // Fall back to thread ID for unnamed worker threads
        threadName = QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId()));
    }
    
    QString txt = QString("[%1][%2] ").arg(timestamp).arg(threadName);
    
    switch (type) {
        case QtDebugMsg:
            txt += QString("[D][%1]: %2").arg(category, msg);
            break;
        case QtWarningMsg:
            txt += QString("[W][%1]: %2").arg(category, msg);
            break;
        case QtCriticalMsg:
            txt += QString("[C][%1]: %2").arg(category, msg);
            break;
        case QtFatalMsg:
            txt += QString("[F][%1]: %2").arg(category, msg);
            break;
        case QtInfoMsg:
            txt += QString("[I][%1]: %2").arg(category, msg);
            break;
    }

    // For Windows GUI applications, std::cout may not be available and can cause crashes
    // Use OutputDebugString instead for debug output
    // In static builds or Windows subsystem builds, stdout/stderr may not work
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(txt.utf16()));
    OutputDebugStringW(L"\n");
#else
    // Use fprintf to stderr for single output
    fprintf(stderr, "%s\n", txt.toUtf8().constData());
    fflush(stderr);
#endif
}