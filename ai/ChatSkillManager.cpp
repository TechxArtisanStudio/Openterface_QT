#include "ChatSkillManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QLoggingCategory>
#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

ChatSkillManager::ChatSkillManager(QObject *parent)
    : QObject(parent)
{
    seedAndLoad();
}

ChatSkillManager &ChatSkillManager::instance()
{
    static ChatSkillManager inst;
    return inst;
}

QString ChatSkillManager::skillsFolder()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath("Skills");
}

void ChatSkillManager::reload()
{
    seedAndLoad();
}

ChatSkill ChatSkillManager::skillById(const QString &id) const
{
    for (const auto &skill : m_skills) {
        if (skill.id == id) return skill;
    }
    return ChatSkill();
}

void ChatSkillManager::seedAndLoad()
{
    QString folder = skillsFolder();
    QDir dir;

    // Create the folder if it doesn't exist yet
    if (!dir.exists(folder)) {
        dir.mkpath(folder);
    }

    // Write each built-in skill as a JSON file if it isn't there already
    QList<ChatSkill> builtins = builtInSkills();
    for (const auto &skill : builtins) {
        QString filePath = QDir(folder).filePath(skill.id + ".json");
        if (!QFile::exists(filePath)) {
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QJsonDocument doc(skill.toJson());
                file.write(doc.toJson(QJsonDocument::Indented));
                file.close();
            }
        }
    }

    // Load all JSON files from the folder
    QList<ChatSkill> loaded = loadFromFolder(folder);

    // Merge: built-in order first, then extra user skills alphabetically
    QList<ChatSkill> merged;
    QSet<QString> seenIDs;

    for (const auto &builtinSkill : builtins) {
        // Prefer the on-disk version so users can edit built-ins
        bool found = false;
        for (const auto &liveSkill : loaded) {
            if (liveSkill.id == builtinSkill.id) {
                merged.append(liveSkill);
                found = true;
                break;
            }
        }
        if (!found) {
            merged.append(builtinSkill);
        }
        seenIDs.insert(builtinSkill.id);
    }

    for (const auto &skill : loaded) {
        if (!seenIDs.contains(skill.id)) {
            merged.append(skill);
            seenIDs.insert(skill.id);
        }
    }

    m_skills = merged;
    emit skillsChanged();
}

QList<ChatSkill> ChatSkillManager::loadFromFolder(const QString &folderPath) const
{
    QList<ChatSkill> result;
    QDir dir(folderPath);

    QStringList filters;
    filters << "*.json";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

    for (const QString &filename : files) {
        QString filePath = dir.filePath(filename);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) continue;

        QByteArray data = file.readAll();
        file.close();

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(log_ai_chat) << "Failed to parse skill file:" << filePath;
            continue;
        }

        ChatSkill skill = ChatSkill::fromJson(doc.object());
        if (!skill.id.isEmpty()) {
            result.append(skill);
        }
    }

    return result;
}

QList<ChatSkill> ChatSkillManager::builtInSkills() const
{
    QList<ChatSkill> skills;

    ChatSkill checkMessages;
    checkMessages.id = "check-messages";
    checkMessages.name = "Check Messages";
    checkMessages.icon = "mail-message";
    checkMessages.prompt = QStringLiteral(
        "Look at this screenshot of the target machine and identify any unread or pending messages. "
        "For every messaging app, notification badge, or chat window visible, report:\n\n"
        "- App / Service name\n"
        "- Sender name (exactly as shown)\n"
        "- Number of unread messages (as shown by a badge or counter)\n"
        "- Brief preview of the message text if readable\n\n"
        "List each sender on its own line. "
        "If no messaging apps or unread messages are visible, say so clearly."
    );
    checkMessages.captureScreen = true;
    checkMessages.userLabel = "Check messages on target screen";
    skills.append(checkMessages);

    return skills;
}
