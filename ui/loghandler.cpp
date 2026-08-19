#include "loghandler.h"
#include "globalsetting.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Static member initialization
bool LogHandler::s_fileLoggingEnabled = false;
QFile LogHandler::s_logFile;
QMutex LogHandler::s_mutex;
bool LogHandler::s_installed = false;

LogHandler::LogHandler(QObject *parent)
    : QObject(parent)
{
}

LogHandler& LogHandler::instance()
{
    static LogHandler instance;
    return instance;
}

void LogHandler::install()
{
    QMutexLocker locker(&s_mutex);
    if (s_installed) return;

    // Read settings to determine file logging state
    QSettings settings("Techxartisan", "Openterface");
    s_fileLoggingEnabled = settings.value("log/storeLog", false).toBool();

    if (s_fileLoggingEnabled) {
        QString path = getLogFilePath();
        s_logFile.setFileName(path);
        if (!s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            s_fileLoggingEnabled = false;
        }
    }

    qInstallMessageHandler(combinedHandler);
    s_installed = true;
}

void LogHandler::setFileLoggingEnabled(bool enabled, const QString& path)
{
    QMutexLocker locker(&s_mutex);

    // Close existing file if open
    if (s_logFile.isOpen()) {
        s_logFile.flush();
        s_logFile.close();
    }

    s_fileLoggingEnabled = enabled;

    if (enabled) {
        QString filePath = path.isEmpty() ? getLogFilePath() : path;
        s_logFile.setFileName(filePath);
        if (!s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            s_fileLoggingEnabled = false;
        }
    }
}

QString LogHandler::getLogFilePath() const
{
    QSettings settings("Techxartisan", "Openterface");
    QString userPath = settings.value("log/logFilePath").toString();

    if (!userPath.isEmpty()) {
        return userPath;
    }

    // Default: QStandardPaths::AppDataLocation
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty()) {
        dataDir = QCoreApplication::applicationDirPath();
    }
    QDir().mkpath(dataDir);
    return dataDir + "/openterface.log";
}

void LogHandler::shutdown()
{
    QMutexLocker locker(&s_mutex);
    if (s_logFile.isOpen()) {
        s_logFile.flush();
        s_logFile.close();
    }
    s_fileLoggingEnabled = false;
}

void LogHandler::enableLogStore()
{
    // Backward-compatible entry point: re-read settings and update file logging
    QSettings settings("Techxartisan", "Openterface");
    bool storeLog = settings.value("log/storeLog", false).toBool();
    QString logFilePath = settings.value("log/logFilePath").toString();
    setFileLoggingEnabled(storeLog, logFilePath);

    // Ensure handler is installed
    if (!s_installed) {
        qInstallMessageHandler(combinedHandler);
        s_installed = true;
    }
}

QString LogHandler::formatMessage(QtMsgType type,
                                   const QMessageLogContext &context,
                                   const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");

    QThread *currentThread = QThread::currentThread();
    QString threadName;
    if (!currentThread->objectName().isEmpty()) {
        threadName = currentThread->objectName();
    } else if (currentThread == QCoreApplication::instance()->thread()) {
        threadName = "MainThread";
    } else {
        threadName = QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId()));
    }

    const char* categoryName = context.category;
    QString category = categoryName ? QString(categoryName) : "default";

    QString level;
    switch (type) {
        case QtDebugMsg:    level = "D"; break;
        case QtInfoMsg:     level = "I"; break;
        case QtWarningMsg:  level = "W"; break;
        case QtCriticalMsg: level = "C"; break;
        case QtFatalMsg:    level = "F"; break;
        default:            level = "U"; break;
    }

    return QString("[%1][%2][%3][%4] %5")
        .arg(timestamp, level, threadName, category, msg);
}

void LogHandler::combinedHandler(QtMsgType type,
                                  const QMessageLogContext &context,
                                  const QString &msg)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);

    QString formatted = formatMessage(type, context, msg);

    // 1. Console output (always)
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(formatted.utf16()));
    OutputDebugStringW(L"\n");
#else
    fprintf(stderr, "%s\n", formatted.toUtf8().constData());
    fflush(stderr);
#endif

    // 2. File output (if enabled)
    if (s_fileLoggingEnabled && s_logFile.isOpen()) {
        QTextStream stream(&s_logFile);
        stream << formatted << "\n";
        stream.flush();
    }

    // 3. Fatal → abort
    if (type == QtFatalMsg) {
        std::abort();
    }
}
