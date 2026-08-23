#include "ChatPersistence.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

ChatPersistence::ChatPersistence(QObject *parent)
    : QObject(parent)
{
}

ChatPersistence &ChatPersistence::instance()
{
    static ChatPersistence inst;
    return inst;
}

QString ChatPersistence::historyFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath("chat_history.json");
}

void ChatPersistence::saveHistory(
    const QList<ChatMessage> &messages,
    const ChatExecutionPlan &plan,
    bool hasPlan,
    const QList<ChatTaskTraceEntry> &plannerTraces)
{
    QJsonObject root;

    QJsonArray msgsArr;
    for (const auto &msg : messages) {
        // Status-hint messages (step indicators like "Step 2/10 — examining
        // screen...") are ephemeral display-only entries; don't persist them.
        if (msg.isStatusHint) continue;
        msgsArr.append(msg.toJson());
    }
    root["messages"] = msgsArr;

    if (hasPlan) {
        root["currentPlan"] = plan.toJson();
    }

    QJsonArray tracesArr;
    for (const auto &t : plannerTraces) tracesArr.append(t.toJson());
    root["plannerTraces"] = tracesArr;

    QJsonDocument doc(root);
    QString path = historyFilePath();

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qCDebug(log_ai_chat) << "Chat history saved to" << path
                             << "messages=" << messages.size();
    } else {
        qCWarning(log_ai_chat) << "Failed to save chat history to" << path;
    }
}

bool ChatPersistence::loadHistory(
    QList<ChatMessage> &messages,
    ChatExecutionPlan &plan,
    bool &hasPlan,
    QList<ChatTaskTraceEntry> &plannerTraces)
{
    QString path = historyFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(log_ai_chat) << "No chat history file found at" << path;
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError) {
        qCWarning(log_ai_chat) << "Failed to parse chat history:" << err.errorString();
        return false;
    }

    QJsonObject root = doc.object();

    messages.clear();
    QJsonArray msgsArr = root["messages"].toArray();
    for (const auto &v : msgsArr) {
        messages.append(ChatMessage::fromJson(v.toObject()));
    }

    hasPlan = root.contains("currentPlan");
    if (hasPlan) {
        plan = ChatExecutionPlan::fromJson(root["currentPlan"].toObject());
    }

    plannerTraces.clear();
    QJsonArray tracesArr = root["plannerTraces"].toArray();
    for (const auto &v : tracesArr) {
        plannerTraces.append(ChatTaskTraceEntry::fromJson(v.toObject()));
    }

    qCDebug(log_ai_chat) << "Chat history loaded from" << path
                         << "messages=" << messages.size()
                         << "hasPlan=" << hasPlan;
    return true;
}
