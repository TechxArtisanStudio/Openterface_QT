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

// Initialize all layouts
struct LayoutInitializer {
    LayoutInitializer() {
        // The initialization functions are removed as per the instructions
    }
} layoutInitializer;

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
    QMap<QString, int> keyNameToQt;
    keyNameToQt["A"] = Qt::Key_A;
    keyNameToQt["B"] = Qt::Key_B;
    keyNameToQt["C"] = Qt::Key_C;
    keyNameToQt["D"] = Qt::Key_D;
    keyNameToQt["E"] = Qt::Key_E;
    keyNameToQt["F"] = Qt::Key_F;
    keyNameToQt["G"] = Qt::Key_G;
    keyNameToQt["H"] = Qt::Key_H;
    keyNameToQt["I"] = Qt::Key_I;
    keyNameToQt["J"] = Qt::Key_J;
    keyNameToQt["K"] = Qt::Key_K;
    keyNameToQt["L"] = Qt::Key_L;
    keyNameToQt["M"] = Qt::Key_M;
    keyNameToQt["N"] = Qt::Key_N;
    keyNameToQt["O"] = Qt::Key_O;
    keyNameToQt["P"] = Qt::Key_P;
    keyNameToQt["Q"] = Qt::Key_Q;
    keyNameToQt["R"] = Qt::Key_R;
    keyNameToQt["S"] = Qt::Key_S;
    keyNameToQt["T"] = Qt::Key_T;
    keyNameToQt["U"] = Qt::Key_U;
    keyNameToQt["V"] = Qt::Key_V;
    keyNameToQt["W"] = Qt::Key_W;
    keyNameToQt["X"] = Qt::Key_X;
    keyNameToQt["Y"] = Qt::Key_Y;
    keyNameToQt["Z"] = Qt::Key_Z;
    keyNameToQt["0"] = Qt::Key_0;
    keyNameToQt["1"] = Qt::Key_1;
    keyNameToQt["2"] = Qt::Key_2;
    keyNameToQt["3"] = Qt::Key_3;
    keyNameToQt["4"] = Qt::Key_4;
    keyNameToQt["5"] = Qt::Key_5;
    keyNameToQt["6"] = Qt::Key_6;
    keyNameToQt["7"] = Qt::Key_7;
    keyNameToQt["8"] = Qt::Key_8;
    keyNameToQt["9"] = Qt::Key_9;
    keyNameToQt["Space"] = Qt::Key_Space;
    keyNameToQt["Return"] = Qt::Key_Return;
    keyNameToQt["Tab"] = Qt::Key_Tab;
    keyNameToQt["Backspace"] = Qt::Key_Backspace;
    keyNameToQt["Delete"] = Qt::Key_Delete;
    keyNameToQt["Escape"] = Qt::Key_Escape;
    keyNameToQt["Shift"] = Qt::Key_Shift;
    keyNameToQt["Control"] = Qt::Key_Control;
    keyNameToQt["Alt"] = Qt::Key_Alt;
    keyNameToQt["CapsLock"] = Qt::Key_CapsLock;
    keyNameToQt["Minus"] = Qt::Key_Minus;
    keyNameToQt["Equal"] = Qt::Key_Equal;
    keyNameToQt["BracketLeft"] = Qt::Key_BracketLeft;
    keyNameToQt["BracketRight"] = Qt::Key_BracketRight;
    keyNameToQt["Backslash"] = Qt::Key_Backslash;
    keyNameToQt["Semicolon"] = Qt::Key_Semicolon;
    keyNameToQt["Apostrophe"] = Qt::Key_Apostrophe;
    keyNameToQt["QuoteLeft"] = Qt::Key_QuoteLeft;
    keyNameToQt["Comma"] = Qt::Key_Comma;
    keyNameToQt["Period"] = Qt::Key_Period;
    keyNameToQt["Slash"] = Qt::Key_Slash;
    keyNameToQt["Hash"] = Qt::Key_NumberSign;

    // Function keys
    keyNameToQt["F1"] = Qt::Key_F1;
    keyNameToQt["F2"] = Qt::Key_F2;
    keyNameToQt["F3"] = Qt::Key_F3;
    keyNameToQt["F4"] = Qt::Key_F4;
    keyNameToQt["F5"] = Qt::Key_F5;
    keyNameToQt["F6"] = Qt::Key_F6;
    keyNameToQt["F7"] = Qt::Key_F7;
    keyNameToQt["F8"] = Qt::Key_F8;
    keyNameToQt["F9"] = Qt::Key_F9;
    keyNameToQt["F10"] = Qt::Key_F10;
    keyNameToQt["F11"] = Qt::Key_F11;
    keyNameToQt["F12"] = Qt::Key_F12;

    // Navigation keys
    keyNameToQt["Up"] = Qt::Key_Up;
    keyNameToQt["Down"] = Qt::Key_Down;
    keyNameToQt["Left"] = Qt::Key_Left;
    keyNameToQt["Right"] = Qt::Key_Right;
    keyNameToQt["PageUp"] = Qt::Key_PageUp;
    keyNameToQt["PageDown"] = Qt::Key_PageDown;
    keyNameToQt["Home"] = Qt::Key_Home;
    keyNameToQt["End"] = Qt::Key_End;
    keyNameToQt["Insert"] = Qt::Key_Insert;
    keyNameToQt["Delete"] = Qt::Key_Delete;

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
                                        << "(" << qtKey << ") to scancode 0x" 
                                        << QString::number(scanCode, 16);
        } else {
            qCWarning(log_keyboard_layouts) << "Failed to map key" << keyName 
                                          << "value:" << valueStr 
                                          << "ok:" << ok 
                                          << "qtKey:" << qtKey;
        }
    }

    qCDebug(log_keyboard_layouts) << "Final keyMap size:" << config.keyMap.size();
    qCDebug(log_keyboard_layouts) << "KeyMap contents:";
    for (auto it = config.keyMap.begin(); it != config.keyMap.end(); ++it) {
        qCDebug(log_keyboard_layouts) << "  Qt key:" << it.key() 
                                    << "-> Scancode: 0x" << QString::number(it.value(), 16);
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