#include "KeyboardLayouts.h"
#include <QObject>
#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeySequence>

Q_LOGGING_CATEGORY(log_keyboard_layouts, "opf.host.layouts")

const KeyboardLayoutConfig QWERTY_US("US QWERTY", false);
const KeyboardLayoutConfig QWERTY_UK("UK QWERTY", false);
const KeyboardLayoutConfig AZERTY_FR("French AZERTY", false);
const KeyboardLayoutConfig AZERTY_BE("Belgian AZERTY", false);
const KeyboardLayoutConfig QWERTZ_DE("German QWERTZ", false);
const KeyboardLayoutConfig QWERTY_DK("Danish QWERTY", false);
const KeyboardLayoutConfig QWERTY_SE("Swedish QWERTY", false);
const KeyboardLayoutConfig JAPANESE("Japanese", false);
const KeyboardLayoutConfig QWERTY_ES("Spanish QWERTY", false);

// Initialize all layouts
struct LayoutInitializer {
    LayoutInitializer() {
        // The initialization functions are removed as per the instructions
    }
} layoutInitializer;

QMap<QString, int> KeyboardLayoutConfig::keyNameToQt;

// Static method implementation
KeyboardLayoutConfig KeyboardLayoutConfig::fromJsonFile(const QString& filePath) {
    KeyboardLayoutConfig config;
    
    qCDebug(log_keyboard_layouts) << "Loading keyboard layout from file:" << filePath;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open keyboard layout file:" << filePath;
        return config;
    }

    QByteArray data = file.readAll();
    qCDebug(log_keyboard_layouts) << "File contents:" << data;
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qWarning() << "Failed to parse JSON from file:" << filePath;
        return config;
    }

    QJsonObject json = doc.object();
    
    // Load basic properties
    config.name = json["name"].toString();
    config.isRightToLeft = json["right_to_left"].toBool(false);
    qCDebug(log_keyboard_layouts) << "Loading layout:" << config.name;

    // Create a mapping from key names to Qt key codes
    if (keyNameToQt.isEmpty()) {
        initializeKeyNameToQt(keyNameToQt);
    }

    // Load key map
    QJsonObject keyMap = json["key_map"].toObject();
    qCDebug(log_keyboard_layouts) << "Loading" << keyMap.size() << "key mappings";
    
    for (auto it = keyMap.begin(); it != keyMap.end(); ++it) {
        QString keyName = it.key();
        QString valueStr = it.value().toString();
        
        // Convert key name to Qt key code
        if (keyName.startsWith("Key_")) {
            keyName = keyName.mid(4); // Remove "Key_" prefix
        }
        
        // Get the Qt key code
        int qtKey = keyNameToQt.value(keyName, Qt::Key_unknown);
        qCDebug(log_keyboard_layouts) << "Converting key name:" << keyName 
                                    << "to Qt key code: 0x" << QString::number(qtKey, 16)
                                    << "(" << qtKey << ")"
                                    << "(original name:" << it.key() << ")";
        
        // Convert hex string to integer
        bool ok;
        uint8_t scanCode = valueStr.mid(2).toInt(&ok, 16);
        if (ok && qtKey != Qt::Key_unknown) {
            // Check if this Qt key already has a mapping
            if (config.keyMap.contains(qtKey)) {
                qCWarning(log_keyboard_layouts) << "WARNING: Overwriting existing mapping for Qt key 0x" 
                                                << QString::number(qtKey, 16)
                                                << "old HID: 0x" << QString::number(config.keyMap[qtKey], 16)
                                                << "new HID: 0x" << QString::number(scanCode, 16)
                                                << "key name:" << keyName;
            }
            config.keyMap[qtKey] = scanCode;
            qCDebug(log_keyboard_layouts) << "Successfully mapped key" << keyName 
                                        << "(Qt:0x" << QString::number(qtKey, 16) << ")"
                                        << "to scancode 0x" << QString::number(scanCode, 16);
        } else{
            qCWarning(log_keyboard_layouts) << "Failed to map key" << keyName 
                                          << "value:" << valueStr 
                                          << "ok:" << ok 
                                          << "qtKey:" << qtKey;
        }
    }

    // Load char mapping
    QJsonObject charMap = json["char_mapping"].toObject();
    qCDebug(log_keyboard_layouts) << "Loading" << charMap.size() << "character mappings";
    
    for (auto it = charMap.begin(); it != charMap.end(); ++it) {
        QString charStr = it.key(); 
        QChar character = charStr[0];
        QString keyName = it.value().toString(); 
        
        // Special debug for | character
        if (character.unicode() == 0x7C) {
            qCWarning(log_keyboard_layouts) << "***** LOADING '|' CHARACTER *****";
            qCWarning(log_keyboard_layouts) << "  charStr:" << charStr;
            qCWarning(log_keyboard_layouts) << "  character:" << character;
            qCWarning(log_keyboard_layouts) << "  character.unicode():" << QString::number(character.unicode(), 16);
            qCWarning(log_keyboard_layouts) << "  keyName:" << keyName;
        }
        
        qCDebug(log_keyboard_layouts) << "Processing char:" << charStr 
                                      << "(U+" << QString::number(character.unicode(), 16) << ")"
                                      << "mapped to" << keyName;

        // Remove "Key_" prefix and get Qt key from keyNameToQt
        if (keyName.startsWith("Key_")) {
            keyName = keyName.mid(4); 
        }
        int qtKey = keyNameToQt.value(keyName, Qt::Key_unknown);
        if (qtKey == Qt::Key_unknown) {
            qCWarning(log_keyboard_layouts) << "Unknown key name in char_mapping:" << keyName << "for char:" << charStr;
            continue;
        }

        // Check if this unicode value already exists in charMapping
        if (config.charMapping.contains(character.unicode())) {
            qCWarning(log_keyboard_layouts) << "WARNING: Overwriting charMapping for character" 
                                           << charStr << "(U+" << QString::number(character.unicode(), 16) << ")"
                                           << "old Qt key: 0x" << QString::number(config.charMapping[character.unicode()], 16)
                                           << "new Qt key: 0x" << QString::number(qtKey, 16);
        }

        config.charMapping[character.unicode()] = qtKey;
        qCDebug(log_keyboard_layouts) << "Mapped char" << charStr 
                                     << "(U+" << QString::number(character.unicode(), 16) << ")"
                                     << "to QtKey 0x" << QString::number(qtKey, 16);
        
        // Special debug for | character
        if (character.unicode() == 0x7C) {
            qCWarning(log_keyboard_layouts) << "***** '|' CHARACTER MAPPED *****";
            qCWarning(log_keyboard_layouts) << "  charMapping[0x7C] = Qt key 0x" << QString::number(qtKey, 16);
            qCWarning(log_keyboard_layouts) << "  Now looking up HID for Qt key 0x" << QString::number(qtKey, 16);
            uint8_t hid = config.keyMap.value(qtKey, 0);
            qCWarning(log_keyboard_layouts) << "  Result: HID 0x" << QString::number(hid, 16);
        }
    }

    QJsonObject unicodeMap = json["unicode_map"].toObject();
    qCDebug(log_keyboard_layouts) << "Loading" << unicodeMap.size() << "unicode mappings";
    
    for (auto it = unicodeMap.begin(); it != unicodeMap.end(); ++it) {
        QString unicodeStr = it.key();
        QString valueStr = it.value().toString();

        uint32_t unicodeValue;
        if (unicodeStr.startsWith("U+")) {
            bool ok;
            unicodeValue = unicodeStr.mid(2).toUInt(&ok, 16);
            if (!ok) {
                qCWarning(log_keyboard_layouts) << "Invalid Unicode key:" << unicodeStr;
                continue;
            }
        } else {
            bool ok;
            unicodeValue = unicodeStr.toUInt(&ok);
            if (!ok) {
                qCWarning(log_keyboard_layouts) << "Invalid Unicode key:" << unicodeStr;
                continue;
            }
        }

        bool ok;
        uint8_t scanCode = valueStr.mid(2).toInt(&ok, 16);
        if (ok) {
            config.unicodeMap[unicodeValue] = scanCode;
            qCDebug(log_keyboard_layouts) << "Mapped Unicode U+" << QString::number(unicodeValue, 16) 
                                        << "to scancode 0x" << QString::number(scanCode, 16);
        } else {
            qCWarning(log_keyboard_layouts) << "Failed to parse scancode for Unicode" 
                                          << unicodeStr << ":" << valueStr;
        }
    }

    // Load shift keys
    QJsonArray shiftKeys = json["need_shift_keys"].toArray();

    // DEBUG: Dump the entire keyMap to see what got loaded
    qCWarning(log_keyboard_layouts) << "===== DUMPING KEYMAP FOR LAYOUT:" << config.name << "=====";
    for (auto it = config.keyMap.begin(); it != config.keyMap.end(); ++it) {
        qCWarning(log_keyboard_layouts) << "  Qt key 0x" << QString::number(it.key(), 16)
                                       << "-> HID 0x" << QString::number(it.value(), 16);
    }
    qCWarning(log_keyboard_layouts) << "===== END KEYMAP DUMP =====";
    for (const QJsonValue& value : shiftKeys) {
        QString keyStr = value.toString();
        if (keyStr.length() == 1) {
            config.needShiftKeys.append(keyStr[0].unicode()); 
        } else {
            // Handle hex values for special characters
            config.needShiftKeys.append(keyStr.toInt(nullptr, 16));
        }
    }

    if (json.contains("need_altgr_keys")) {
        QJsonArray altGrKeys = json["need_altgr_keys"].toArray();
        for (const QJsonValue& value : altGrKeys) {
            QString keyStr = value.toString();
            if (keyStr.length() == 1) {
                config.needAltGrKeys.append(keyStr[0].unicode());
                qCDebug(log_keyboard_layouts) << "Added AltGr key:" << keyStr;
            } else {
                config.needAltGrKeys.append(keyStr.toInt(nullptr, 16));
                qCDebug(log_keyboard_layouts) << "Added AltGr key (hex):" << keyStr;
            }
        }
    }
    
    return config;
}

// Singleton implementation
KeyboardLayoutManager& KeyboardLayoutManager::getInstance() {
    static KeyboardLayoutManager instance;
    return instance;
}

void KeyboardLayoutManager::loadLayouts(const QString& configDir) {
    layouts.clear();
    qCDebug(log_keyboard_layouts) << "Loading keyboard layouts from directory:" << configDir;

    // Try loading from filesystem first
    QDir dir(configDir);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.json";
        QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
        
        qCDebug(log_keyboard_layouts) << "Found" << files.size() << "layout files in filesystem.";
        
        for (const QFileInfo& file : files) {
            qCDebug(log_keyboard_layouts) << "Processing file from filesystem:" << file.fileName();
            KeyboardLayoutConfig config = KeyboardLayoutConfig::fromJsonFile(file.absoluteFilePath());
            if (!config.name.isEmpty()) {
                layouts[config.name] = config;
                qCDebug(log_keyboard_layouts) << "Loaded layout from filesystem:" << config.name;
            }
        }
    }

    // Then try loading from resources
    QDir resourceDir(":/config/keyboards");
    if (resourceDir.exists()) {
        QStringList filters;
        filters << "*.json";
        QFileInfoList files = resourceDir.entryInfoList(filters, QDir::Files);
        
        qCDebug(log_keyboard_layouts) << "Found" << files.size() << "layout files in resources.";
        
        for (const QFileInfo& file : files) {
            qCDebug(log_keyboard_layouts) << "Processing file from resources:" << file.fileName();
            KeyboardLayoutConfig config = KeyboardLayoutConfig::fromJsonFile(file.absoluteFilePath());
            if (!config.name.isEmpty()) {
                layouts[config.name] = config;
                qCDebug(log_keyboard_layouts) << "Loaded layout from resources:" << config.name;
            }
        }
    }

    qCDebug(log_keyboard_layouts) << "Finished loading layouts. Total layouts loaded:" << layouts.size();
    if (layouts.isEmpty()) {
        qWarning() << "No keyboard layouts were loaded! Make sure the JSON files exist in either" 
                  << configDir << "or in the resources.";
    }
}

KeyboardLayoutConfig KeyboardLayoutManager::getLayout(const QString& name) const {
    return layouts.value(name);
}

QStringList KeyboardLayoutManager::getAvailableLayouts() const {
    return layouts.keys();
}

// === Custom Layout Support Implementation ===

QString KeyboardLayoutManager::getCustomLayoutsDir() {
    QString appDataPath;
    
#ifdef Q_OS_WIN
    appDataPath = QDir::fromNativeSeparators(qgetenv("APPDATA"));
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::homePath() + "/AppData/Roaming";
    }
#elif defined(Q_OS_MAC)
    appDataPath = QDir::homePath() + "/Library/Application Support";
#else // Linux
    appDataPath = QDir::homePath() + "/.config";
#endif
    
    QString customDir = appDataPath + "/Openterface/keyboards/custom";
    QDir dir;
    if (!dir.exists(customDir)) {
        dir.mkpath(customDir);
        qCDebug(log_keyboard_layouts) << "Created custom layouts directory:" << customDir;
    }
    
    return customDir;
}

bool KeyboardLayoutManager::createCustomLayout(const QString& baseName, const QString& customName) {
    if (!layouts.contains(baseName)) {
        qWarning() << "Base layout not found:" << baseName;
        return false;
    }
    
    KeyboardLayoutConfig customConfig = layouts[baseName];
    customConfig.name = customName;
    
    return saveCustomLayout(customConfig, customName);
}

bool KeyboardLayoutManager::saveCustomLayout(const KeyboardLayoutConfig& config, const QString& name) {
    QString customDir = getCustomLayoutsDir();
    QString fileName = name;
    fileName.replace(" ", "_");
    fileName = fileName.toLower();
    QString filePath = customDir + "/" + fileName + ".json";
    
    QString jsonStr = exportLayoutToJson(config);
    if (jsonStr.isEmpty()) {
        qWarning() << "Failed to export layout to JSON";
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }
    
    file.write(jsonStr.toUtf8());
    file.close();
    
    // Add to loaded layouts
    layouts[config.name] = config;
    
    qCDebug(log_keyboard_layouts) << "Saved custom layout:" << config.name << "to" << filePath;
    return true;
}

KeyboardLayoutConfig KeyboardLayoutManager::mergeCorrections(
    const KeyboardLayoutConfig& base,
    const QMap<int, uint8_t>& keyMapCorrections,
    const QMap<uint8_t, int>& charMapCorrections)
{
    KeyboardLayoutConfig merged = base;
    
    // Apply key map corrections
    for (auto it = keyMapCorrections.begin(); it != keyMapCorrections.end(); ++it) {
        merged.keyMap[it.key()] = it.value();
        qCDebug(log_keyboard_layouts) << "Corrected key mapping: Qt key" << it.key() 
                                     << "-> HID" << QString::number(it.value(), 16);
    }
    
    // Apply char map corrections
    for (auto it = charMapCorrections.begin(); it != charMapCorrections.end(); ++it) {
        merged.charMapping[it.key()] = it.value();
        qCDebug(log_keyboard_layouts) << "Corrected char mapping: char" << QChar(it.key())
                                     << "-> Qt key" << it.value();
    }
    
    return merged;
}

QString KeyboardLayoutManager::exportLayoutToJson(const KeyboardLayoutConfig& config) const {
    QJsonObject root;
    root["name"] = config.name;
    root["right_to_left"] = config.isRightToLeft;
    
    // Export key_map
    QJsonObject keyMapObj;
    for (auto it = config.keyMap.begin(); it != config.keyMap.end(); ++it) {
        // Find the key name from Qt key code
        QString keyName;
        for (auto nameIt = KeyboardLayoutConfig::keyNameToQt.begin(); 
             nameIt != KeyboardLayoutConfig::keyNameToQt.end(); ++nameIt) {
            if (nameIt.value() == it.key()) {
                keyName = "Key_" + nameIt.key();
                break;
            }
        }
        if (!keyName.isEmpty()) {
            keyMapObj[keyName] = QString("0x%1").arg(it.value(), 2, 16, QChar('0'));
        }
    }
    root["key_map"] = keyMapObj;
    
    // Export char_mapping
    QJsonObject charMapObj;
    for (auto it = config.charMapping.begin(); it != config.charMapping.end(); ++it) {
        QString charStr = QChar(it.key());
        QString keyName;
        for (auto nameIt = KeyboardLayoutConfig::keyNameToQt.begin(); 
             nameIt != KeyboardLayoutConfig::keyNameToQt.end(); ++nameIt) {
            if (nameIt.value() == it.value()) {
                keyName = "Key_" + nameIt.key();
                break;
            }
        }
        if (!keyName.isEmpty()) {
            charMapObj[charStr] = keyName;
        }
    }
    root["char_mapping"] = charMapObj;
    
    // Export unicode_map
    QJsonObject unicodeMapObj;
    for (auto it = config.unicodeMap.begin(); it != config.unicodeMap.end(); ++it) {
        QString unicodeStr = QString("U+%1").arg(it.key(), 4, 16, QChar('0')).toUpper();
        unicodeMapObj[unicodeStr] = QString("0x%1").arg(it.value(), 2, 16, QChar('0'));
    }
    if (!unicodeMapObj.isEmpty()) {
        root["unicode_map"] = unicodeMapObj;
    }
    
    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented);
}

bool KeyboardLayoutManager::importLayoutFromJson(const QString& jsonPath) {
    KeyboardLayoutConfig config = KeyboardLayoutConfig::fromJsonFile(jsonPath);
    if (config.name.isEmpty()) {
        qWarning() << "Failed to load layout from" << jsonPath;
        return false;
    }
    
    layouts[config.name] = config;
    qCDebug(log_keyboard_layouts) << "Imported layout:" << config.name;
    return true;
}

QStringList KeyboardLayoutManager::getCustomLayouts() const {
    QString customDir = getCustomLayoutsDir();
    QDir dir(customDir);
    
    if (!dir.exists()) {
        return QStringList();
    }
    
    QStringList filters;
    filters << "*.json";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    
    QStringList customLayouts;
    for (const QFileInfo& file : files) {
        QString baseName = file.baseName();
        // Convert filename back to layout name
        baseName.replace("_", " ");
        
        // Check if this layout is loaded
        for (const QString& layoutName : layouts.keys()) {
            if (layoutName.toLower().replace(" ", "_") == file.baseName()) {
                customLayouts << layoutName;
                break;
            }
        }
    }
    
    return customLayouts;
}

bool KeyboardLayoutManager::deleteCustomLayout(const QString& name) {
    QString customDir = getCustomLayoutsDir();
    QString fileName = name;
    fileName.replace(" ", "_");
    fileName = fileName.toLower();
    QString filePath = customDir + "/" + fileName + ".json";
    
    QFile file(filePath);
    if (!file.exists()) {
        qWarning() << "Custom layout file not found:" << filePath;
        return false;
    }
    
    if (!file.remove()) {
        qWarning() << "Failed to delete custom layout file:" << filePath;
        return false;
    }
    
    // Remove from loaded layouts
    layouts.remove(name);
    
    qCDebug(log_keyboard_layouts) << "Deleted custom layout:" << name;
    return true;
}
