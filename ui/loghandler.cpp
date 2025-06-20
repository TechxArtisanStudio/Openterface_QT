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
    qDebug() << "Enable log store now ";
    static QSettings settings("Techxartisan", "Openterface");
    bool storeLog = settings.value("log/storeLog", false).toBool();
    qDebug() << "Store log is " << storeLog;
    if (storeLog)
    {
        qInstallMessageHandler(fileMessageHandler);
    }
    else
    {
        qInstallMessageHandler(customMessageHandler);      // Reset to default handler
    }
    qDebug() << "Enable log store done";
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
    QString threadName = currentThread->objectName().isEmpty() ? QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId())) : currentThread->objectName();
    QString category = categoryName ? QString(categoryName) : "default";
    QString txt = QString("[%1][%2] ").arg(timestamp).arg(threadName);

    switch (type)
    {
    case QtDebugMsg:
        txt = QString("[D] %1").arg(msg);
        break;
    case QtWarningMsg:
        txt = QString("[W] %1").arg(msg);
        break;
    case QtCriticalMsg:
        txt = QString("[C] %1").arg(msg);
        break;
    case QtFatalMsg:
        txt = QString("[F] %1").arg(msg);
        break;
    }

    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    ts << " [" << category << "] " << txt << " (" << context.file << ":" << context.line << ", " << context.function << ")\n";
    ts.flush();
}


void LogHandler::customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Q_UNUSED(context)

    const char* categoryName = context.category;
    QString category = categoryName ? QString(categoryName) : "opf.default.msg";
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QThread *currentThread = QThread::currentThread();
    QString threadName = currentThread->objectName().isEmpty() ? QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId())) : currentThread->objectName();
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

    // QFile outFile("log.txt");
    // outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    // QTextStream textStream(&outFile);
    // textStream << txt << endl;
    
    std::cout << txt.toStdString() << std::endl;
}