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
const KeyboardLayoutConfig QWERTZ_DE("German QWERTZ", false);
const KeyboardLayoutConfig QWERTY_DK("Danish QWERTY", false);
const KeyboardLayoutConfig QWERTY_SE("Swedish QWERTY", false);
const KeyboardLayoutConfig JAPANESE("Japanese", false);

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
                                    << "to Qt key code:" << qtKey 
                                    << "(original name:" << it.key() << ")";
        
        // Convert hex string to integer
        bool ok;
        uint8_t scanCode = valueStr.mid(2).toInt(&ok, 16);
        if (ok && qtKey != Qt::Key_unknown) {
            config.keyMap[qtKey] = scanCode;
            qCDebug(log_keyboard_layouts) << "Successfully mapped key" << keyName 
                                        << "(" << qtKey << ") to scancode 0x" + QString::number(scanCode, 16);
        } else {
            qCWarning(log_keyboard_layouts) << "Failed to map key" << keyName 
                                          << "value:" << valueStr 
                                          << "ok:" << ok 
                                          << "qtKey:" << qtKey;
        }
    }

    // Load char mapping
    QJsonObject charMap = json["char_mapping"].toObject();
    for (auto it = charMap.begin(); it != charMap.end(); ++it) {
        QChar character = it.key()[0];
        QString keyName = it.value().toString();
        int qtKey = QKeySequence::fromString(keyName)[0];
        
        config.charMapping[character.toLatin1()] = qtKey;
    }

    // Load shift keys
    QJsonArray shiftKeys = json["need_shift_keys"].toArray();
    for (const QJsonValue& value : shiftKeys) {
        QString keyStr = value.toString();
        if (keyStr.length() == 1) {
            config.needShiftKeys.append(keyStr[0].toLatin1());
        } else {
            // Handle hex values for special characters
            config.needShiftKeys.append(keyStr.toInt(nullptr, 16));
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
