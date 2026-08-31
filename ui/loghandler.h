#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QThread>
#include <QDebug>
#include <QCoreApplication>

class LogHandler : public QObject
{
    Q_OBJECT

public:
    explicit LogHandler(QObject *parent = nullptr);
    static LogHandler& instance();

    /// Install the combined message handler (call once at startup)
    void install();

    /// Enable or disable file logging at runtime
    void setFileLoggingEnabled(bool enabled, const QString& path = QString());

    /// Get the effective log file path
    QString getLogFilePath() const;

    /// Flush and close log file (call before app exit)
    void shutdown();

    // Keep for backward compatibility during transition
    void enableLogStore();

    static void combinedHandler(QtMsgType type,
                                const QMessageLogContext &context,
                                const QString &msg);

private:
    static QString formatMessage(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &msg);

    static bool s_fileLoggingEnabled;
    static QFile s_logFile;
    static QMutex s_mutex;
    static bool s_installed;
};

#endif // LOGHANDLER_H
