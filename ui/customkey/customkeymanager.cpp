#include "customkeymanager.h"
#include "../globalsetting.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMetaType>

Q_DECLARE_METATYPE(QList<int>)

Q_LOGGING_CATEGORY(log_custom_keys, "opf.ui.customkeys")

// Static key name to Qt key code mapping
static QMap<QString, int>& getKeyMap() {
    static QMap<QString, int> map;
    if (map.isEmpty()) {
        // Letters
        for (char c = 'A'; c <= 'Z'; c++) {
            map[QString("Key_%1").arg(c)] = Qt::Key_A + (c - 'A');
        }
        // Numbers
        for (char c = '0'; c <= '9'; c++) {
            map[QString("Key_%1").arg(c)] = Qt::Key_0 + (c - '0');
        }
        // Function keys
        for (int i = 1; i <= 24; i++) {
            map[QString("Key_F%1").arg(i)] = Qt::Key_F1 + (i - 1);
        }
        // Common special keys
        map["Key_Space"] = Qt::Key_Space;
        map["Key_Tab"] = Qt::Key_Tab;
        map["Key_Return"] = Qt::Key_Return;
        map["Key_Enter"] = Qt::Key_Enter;
        map["Key_Backspace"] = Qt::Key_Backspace;
        map["Key_Escape"] = Qt::Key_Escape;
        map["Key_Delete"] = Qt::Key_Delete;
        map["Key_Insert"] = Qt::Key_Insert;
        map["Key_Home"] = Qt::Key_Home;
        map["Key_End"] = Qt::Key_End;
        map["Key_PageUp"] = Qt::Key_PageUp;
        map["Key_PageDown"] = Qt::Key_PageDown;
        map["Key_Up"] = Qt::Key_Up;
        map["Key_Down"] = Qt::Key_Down;
        map["Key_Left"] = Qt::Key_Left;
        map["Key_Right"] = Qt::Key_Right;
        map["Key_Print"] = Qt::Key_Print;
        map["Key_ScrollLock"] = Qt::Key_ScrollLock;
        map["Key_Pause"] = Qt::Key_Pause;
        map["Key_CapsLock"] = Qt::Key_CapsLock;
        map["Key_NumLock"] = Qt::Key_NumLock;
        map["Key_Meta"] = Qt::Key_Meta;
        map["Key_Menu"] = Qt::Key_Menu;
        map["Key_Help"] = Qt::Key_Help;
        map["Key_Clear"] = Qt::Key_Clear;
        // Navigation cluster
        map["Key_Back"] = Qt::Key_Back;
        map["Key_Forward"] = Qt::Key_Forward;
        map["Key_Stop"] = Qt::Key_Stop;
        map["Key_Refresh"] = Qt::Key_Refresh;
        // Multimedia keys
        map["Key_VolumeUp"] = Qt::Key_VolumeUp;
        map["Key_VolumeDown"] = Qt::Key_VolumeDown;
        map["Key_VolumeMute"] = Qt::Key_VolumeMute;
        map["Key_MediaPlay"] = Qt::Key_MediaPlay;
        map["Key_MediaStop"] = Qt::Key_MediaStop;
        map["Key_MediaNext"] = Qt::Key_MediaNext;
        map["Key_MediaPrevious"] = Qt::Key_MediaPrevious;
        // Modifier keys (stored as key codes)
        map["Key_Control"] = Qt::Key_Control;
        map["Key_Shift"] = Qt::Key_Shift;
        map["Key_Alt"] = Qt::Key_Alt;
    }
    return map;
}

// Reverse map: Qt key code to key name
static QMap<int, QString>& getReverseKeyMap() {
    static QMap<int, QString> map;
    if (map.isEmpty()) {
        auto& forward = getKeyMap();
        for (auto it = forward.begin(); it != forward.end(); ++it) {
            map[it.value()] = it.key();
        }
    }
    return map;
}

int CustomKeyManager::keyNameToCode(const QString& name) {
    return getKeyMap().value(name, Qt::Key_unknown);
}

QString CustomKeyManager::codeToKeyName(int code) {
    if (code == 0) return QString();
    return getReverseKeyMap().value(code, QString("0x%1").arg(code, 0, 16));
}

int CustomKeyManager::modifierNameToFlag(const QString& name) {
    if (name == "Ctrl") return Qt::ControlModifier;
    if (name == "Shift") return Qt::ShiftModifier;
    if (name == "Alt") return Qt::AltModifier;
    if (name == "Win" || name == "Meta") return Qt::MetaModifier;
    return 0;
}

bool CustomKeyManager::isModifierKey(int keyCode) {
    return keyCode == Qt::Key_Control || keyCode == Qt::Key_Shift
        || keyCode == Qt::Key_Alt || keyCode == Qt::Key_Meta;
}

QStringList CustomKeyManager::modifierFlagToNames(int modifiers) {
    QStringList names;
    if (modifiers & Qt::ControlModifier) names << "Ctrl";
    if (modifiers & Qt::ShiftModifier) names << "Shift";
    if (modifiers & Qt::AltModifier) names << "Alt";
    if (modifiers & Qt::MetaModifier) names << "Win";
    return names;
}

CustomKeyManager::CustomKeyManager(QObject *parent) : QObject(parent) {
}

CustomKeyManager& CustomKeyManager::getInstance() {
    static CustomKeyManager instance;
    return instance;
}

QString CustomKeyManager::getCustomKeysDir() const {
    QString appDataPath;
#ifdef Q_OS_WIN
    appDataPath = qgetenv("APPDATA");
    if (appDataPath.isEmpty()) appDataPath = QDir::homePath() + "/AppData/Roaming";
#elif defined(Q_OS_MAC)
    appDataPath = QDir::homePath() + "/Library/Application Support";
#else
    appDataPath = QDir::homePath() + "/.config";
#endif
    QString dir = appDataPath + "/Openterface/customkeys";
    QDir d;
    if (!d.exists(dir)) d.mkpath(dir);
    return dir;
}

void CustomKeyManager::initialize() {
    // 1. Try loading last imported JSON file if it still exists
    QString lastImportPath = GlobalSetting::instance().getLastCustomKeyImportPath();
    if (!lastImportPath.isEmpty() && QFile::exists(lastImportPath)) {
        if (loadFromFile(lastImportPath)) {
            qCDebug(log_custom_keys) << "Loaded custom keys from last imported file:" << lastImportPath;
            m_currentPresetName = QFileInfo(lastImportPath).fileName();
            return;
        }
    }

    // 2. Try loading user's saved config (current.json - auto-saved on every change)
    QString userFile = getCustomKeysDir() + "/current.json";
    if (QFile::exists(userFile)) {
        if (loadFromFile(userFile)) {
            qCDebug(log_custom_keys) << "Loaded user custom keys from" << userFile;
            m_currentPresetName = "current";
            return;
        }
    }

    // 3. Fall back to default embedded resource
    QString defaultResource = ":/config/customkeys/default.json";
    if (loadFromFile(defaultResource)) {
        qCDebug(log_custom_keys) << "Loaded default keys from resource";
        m_currentPresetName = "Default";
        return;
    }

    // Last resort: empty
    qCWarning(log_custom_keys) << "Failed to load any custom keys configuration";
    m_keys.clear();
}

bool CustomKeyManager::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(log_custom_keys) << "Could not open file:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        qCWarning(log_custom_keys) << "JSON parse error:" << parseError.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    m_currentPresetName = root["name"].toString("Unknown");
    m_keys = parseJsonKeys(root["keys"].toArray());
    return !m_keys.isEmpty();
}

QList<CustomKeyInfo> CustomKeyManager::getKeys() const {
    return m_keys;
}

void CustomKeyManager::setKeys(const QList<CustomKeyInfo>& keys) {
    m_keys = keys;
    // Auto-save to current.json
    QString userFile = getCustomKeysDir() + "/current.json";
    QFile file(userFile);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonObject root;
        root["version"] = 2;
        root["name"] = m_currentPresetName;
        root["keys"] = toJsonKeys(keys);
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        qCDebug(log_custom_keys) << "Saved custom keys to" << userFile;
    }
}

QList<CustomKeyInfo> CustomKeyManager::parseJsonKeys(const QJsonArray& keysArray) const {
    QList<CustomKeyInfo> keys;
    for (const QJsonValue& val : keysArray) {
        QJsonObject obj = val.toObject();
        CustomKeyInfo info;
        info.displayName = obj["displayName"].toString("");
        info.isSeparator = obj["isSeparator"].toBool(false);
        info.specialCombo = obj["specialCombo"].toString("");

        // Parse keyCodes array (new format)
        QJsonArray keyCodesArr = obj["keyCodes"].toArray();
        for (const QJsonValue& kcVal : keyCodesArr) {
            info.keyCodes << kcVal.toInt(0);
        }

        // Fallback: parse old steps format for backward compatibility
        if (info.keyCodes.isEmpty()) {
            QJsonArray stepsArr = obj["steps"].toArray();
            for (const QJsonValue& stepVal : stepsArr) {
                QJsonObject stepObj = stepVal.toObject();

                // Parse modifiers
                int modifiers = 0;
                QJsonArray modArr = stepObj["modifiers"].toArray();
                for (const QJsonValue& modVal : modArr) {
                    modifiers |= modifierNameToFlag(modVal.toString());
                }

                // Add modifier keys to keyCodes
                if (modifiers & Qt::ControlModifier) info.keyCodes << Qt::Key_Control;
                if (modifiers & Qt::ShiftModifier) info.keyCodes << Qt::Key_Shift;
                if (modifiers & Qt::AltModifier) info.keyCodes << Qt::Key_Alt;
                if (modifiers & Qt::MetaModifier) info.keyCodes << Qt::Key_Meta;

                // Parse key code from keyName
                QString keyName = stepObj["keyName"].toString("");
                if (!keyName.isEmpty()) {
                    info.keyCodes << keyNameToCode(keyName);
                } else if (stepObj.contains("keyCode")) {
                    info.keyCodes << stepObj["keyCode"].toInt(0);
                }
            }
        }

        keys << info;
    }
    return keys;
}

QJsonArray CustomKeyManager::toJsonKeys(const QList<CustomKeyInfo>& keys) const {
    QJsonArray arr;
    for (const CustomKeyInfo& info : keys) {
        if (info.isSeparator) {
            QJsonObject obj;
            obj["displayName"] = "";
            obj["isSeparator"] = true;
            obj["keyCodes"] = QJsonArray();
            arr << obj;
            continue;
        }

        QJsonObject obj;
        obj["displayName"] = info.displayName;
        obj["isSeparator"] = false;

        if (!info.specialCombo.isEmpty()) {
            obj["specialCombo"] = info.specialCombo;
        }

        QJsonArray keyCodesArr;
        for (int keyCode : info.keyCodes) {
            keyCodesArr << keyCode;
        }
        obj["keyCodes"] = keyCodesArr;
        arr << obj;
    }
    return arr;
}

bool CustomKeyManager::importFromJson(const QString& filePath) {
    return loadFromFile(filePath);
}

bool CustomKeyManager::exportToJson(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QJsonObject root;
    root["version"] = 2;
    root["name"] = m_currentPresetName;
    root["keys"] = toJsonKeys(m_keys);
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QStringList CustomKeyManager::getPresets() const {
    QString dir = getCustomKeysDir();
    QDir d(dir);
    if (!d.exists()) return QStringList();

    QStringList files = d.entryList(QStringList() << "*.json", QDir::Files);
    // Also include "Default" for the embedded resource
    files.prepend("Default");
    files.removeDuplicates();
    return files;
}

bool CustomKeyManager::loadPreset(const QString& presetName) {
    if (presetName == "Default") {
        return loadFromFile(":/config/customkeys/default.json");
    }
    QString filePath = getCustomKeysDir() + "/" + presetName;
    return loadFromFile(filePath);
}

bool CustomKeyManager::savePreset(const QString& presetName) {
    QString fileName = presetName;
    if (!fileName.endsWith(".json")) fileName += ".json";
    QString filePath = getCustomKeysDir() + "/" + fileName;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QJsonObject root;
    root["version"] = 2;
    root["name"] = presetName;
    root["keys"] = toJsonKeys(m_keys);
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool CustomKeyManager::deletePreset(const QString& presetName) {
    QString fileName = presetName;
    if (!fileName.endsWith(".json")) fileName += ".json";
    QString filePath = getCustomKeysDir() + "/" + fileName;
    return QFile::remove(filePath);
}

QString CustomKeyManager::getCurrentPresetName() const {
    return m_currentPresetName;
}
