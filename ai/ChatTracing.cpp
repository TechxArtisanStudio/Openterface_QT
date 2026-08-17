#include "ChatTracing.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

ChatTracing::ChatTracing(QObject *parent)
    : QObject(parent)
{
}

ChatTracing &ChatTracing::instance()
{
    static ChatTracing inst;
    return inst;
}

QString ChatTracing::traceFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath("ai_trace.log");
}

void ChatTracing::appendToFile(const QString &section)
{
    QString path = traceFilePath();
    QFile file(path);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << "\n" << section << "\n";
        stream << QString("=").repeated(60) << "\n";
        file.close();
    }
}

void ChatTracing::appendAITrace(const QString &title, const QString &body)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString section = QString("[%1] %2\n%3")
        .arg(timestamp, title, body);
    appendToFile(section);
    qCDebug(log_ai_chat) << "AI Trace:" << title;
}

void ChatTracing::appendTaskStepTrace(const QUuid &taskID, const QString &title,
                                       const QString &body, const QString &imageFilePath)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString section = QString("[%1] TASK_STEP task=%2 title=%3\n%4")
        .arg(timestamp, taskID.toString(), title, body);
    if (!imageFilePath.isEmpty()) {
        section += QString("\nImage: %1").arg(imageFilePath);
    }
    appendToFile(section);
}

void ChatTracing::appendPlannerTrace(const QString &title, const QString &body,
                                      const QString &imageFilePath)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString section = QString("[%1] PLANNER: %2\n%3")
        .arg(timestamp, title, body);
    if (!imageFilePath.isEmpty()) {
        section += QString("\nImage: %1").arg(imageFilePath);
    }
    appendToFile(section);
}

QString ChatTracing::readTraceLog() const
{
    QFile file(traceFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return "(no trace log found)";
    }
    QString content = file.readAll();
    file.close();
    return content;
}

QString ChatTracing::readableTraceParts(const QList<ChatApiMessage> &messages) const
{
    QStringList parts;
    for (int i = 0; i < messages.size(); ++i) {
        const auto &msg = messages[i];
        QString role = chatRoleToString(msg.role);
        if (msg.contentParts.isEmpty()) {
            parts << QString("[%1] %2: %3")
                .arg(i).arg(role, msg.simpleText.left(200));
        } else {
            QString desc;
            for (const auto &part : msg.contentParts) {
                if (part.type == ChatApiContentPart::Text) {
                    desc += QString("text: %1 | ").arg(part.text.left(100));
                } else {
                    desc += QString("image: %1 chars | ").arg(part.imageUrl.length());
                }
            }
            parts << QString("[%1] %2: %3")
                .arg(i).arg(role, desc.left(200));
        }
    }
    return parts.join("\n");
}
