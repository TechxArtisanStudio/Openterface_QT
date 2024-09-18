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
    static QSettings settings("Techxartisan", "Openterface");
    bool storeLog = settings.value("log/storeLog", false).toBool();
    if (storeLog)
    {
        qInstallMessageHandler(fileMessageHandler);
    }
    else
    {
        qInstallMessageHandler(0);      // Reset to default handler
    }
}

void LogHandler::fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    static QSettings settings("Techxartisan", "Openterface");
    QString logFilePath = settings.value("log/logFilePath").toString();
    QFile outFile(logFilePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Append))
    {
        return;
    }

    QTextStream ts(&outFile);
    QString txt;

    // Get the category name
    const char* categoryName = context.category;
    QString category = categoryName ? QString(categoryName) : "default";

    switch (type)
    {
    case QtDebugMsg:
        txt = QString("Debug: %1").arg(msg);
        break;
    case QtWarningMsg:
        txt = QString("Warning: %1").arg(msg);
        break;
    case QtCriticalMsg:
        txt = QString("Critical: %1").arg(msg);
        break;
    case QtFatalMsg:
        txt = QString("Fatal: %1").arg(msg);
        break;
    }

    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    ts << " [" << category << "] " << txt << " (" << context.file << ":" << context.line << ", " << context.function << ")\n";
    ts.flush();
}
