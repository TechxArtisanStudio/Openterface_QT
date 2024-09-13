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
