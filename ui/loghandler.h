#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>


class LogHandler : public QObject
{
    Q_OBJECT

public:
    explicit LogHandler(QObject *parent = nullptr); 

    static LogHandler& instance();

    void enableLogStore();
    
    static void fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

};


#endif // LOGHANDLER_H
