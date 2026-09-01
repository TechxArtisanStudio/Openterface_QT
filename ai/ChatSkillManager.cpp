#include "ChatSkillManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QLoggingCategory>
#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

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

    // IDs of skills that were shipped as built-ins in earlier versions but
    // have since been removed. Skip them when loading so users who ran a
    // previous version don't keep seeing a button that no longer belongs.
    static const QSet<QString> deprecatedIDs = { "check-messages" };

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
        if (skill.id.isEmpty()) continue;
        if (deprecatedIDs.contains(skill.id)) {
            // Clean up the stale file so we don't re-parse it next launch
            QFile::remove(filePath);
            continue;
        }
        result.append(skill);
    }

    return result;
}

QList<ChatSkill> ChatSkillManager::builtInSkills() const
{
    // Built-in quick-action skills. Each becomes a button in the chat skill bar.
    // Prompts instruct the agent to act on the TARGET machine (the KVM-connected
    // remote computer), not the host running Openterface.
    //
    // captureScreen = true so the model gets a screenshot of the target before
    // acting — it needs to know whether a terminal is already open, etc.

    auto makeSkill = [](const QString &id, const QString &name,
                        const QString &prompt, bool capture = true) {
        ChatSkill s;
        s.id = id;
        s.name = name;
        s.prompt = prompt;
        s.captureScreen = capture;
        return s;
    };

    return {
        makeSkill("check-ip", "Check IP",
            "Check the IP address configuration on the target machine. "
            "Open a terminal if needed, run 'ip addr' (or 'ifconfig'), and report "
            "all network interfaces and their IP addresses."),

        makeSkill("enable-ssh", "Enable SSH",
            "Enable and start the SSH server on the target machine. "
            "If on a Debian/Ubuntu-based system, run: sudo apt install -y openssh-server && sudo systemctl enable --now ssh. "
            "If on a RHEL/Fedora-based system, run: sudo dnf install -y openssh-server && sudo systemctl enable --now sshd. "
            "Then verify it's running with 'systemctl status ssh' (or 'sshd') and report the result."),

        makeSkill("check-disk", "Check Disk",
            "Check disk space usage on the target machine. "
            "Open a terminal if needed, run 'df -h', and report the disk usage for all mounted filesystems."),

        makeSkill("check-memory", "Check Memory",
            "Check memory usage on the target machine. "
            "Open a terminal if needed, run 'free -h', and report total, used, and available memory."),

        makeSkill("check-system", "System Info",
            "Show system information on the target machine. "
            "Open a terminal if needed, run 'uname -a' and 'cat /etc/os-release', and report the OS name, version, kernel, and architecture."),

        makeSkill("check-network", "Network Ports",
            "Check listening network ports and connections on the target machine. "
            "Open a terminal if needed, run 'ss -tuln', and report all listening ports and their associated services."),

        makeSkill("disable-screen-lock", "Disable Screen Lock",
            "Disable screen lock and screen blanking on the target machine to keep it always on. "
            "Open a terminal if needed and run the following commands:\n"
            "1. Disable screen blanking: 'xset s off' and 'xset -dpms'\n"
            "2. Disable screen lock (GNOME): 'gsettings set org.gnome.desktop.session idle-delay 0'\n"
            "3. Disable auto-suspend: 'gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-ac-type 'nothing''\n"
            "For other desktop environments, use 'xset s off -dpms' and configure the DE's power settings accordingly. "
            "Report the commands executed and their results."),
    };
}
