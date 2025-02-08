#ifndef KEYBOARD_LAYOUTS_H
#define KEYBOARD_LAYOUTS_H

#include <QMap>
#include <QString>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeySequence>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_keyboard_layouts)

struct KeyboardLayoutConfig {
    QString name;
    QMap<int, uint8_t> keyMap;
    QMap<uint8_t, int> charMapping;
    QList<int> needShiftKeys;
    bool isRightToLeft;
    
    // Constructor with default values
    KeyboardLayoutConfig(
        const QString& layoutName = "",
        bool rtl = false
    ) : name(layoutName), isRightToLeft(rtl) {}

    // Load from JSON file
    static KeyboardLayoutConfig fromJsonFile(const QString& filePath);
};

class KeyboardLayoutManager {
public:
    static KeyboardLayoutManager& getInstance();
    
    // Load all layouts from config directory
    void loadLayouts(const QString& configDir = "config/keyboards");
    
    // Get a specific layout
    KeyboardLayoutConfig getLayout(const QString& name) const;
    
    // List available layouts
    QStringList getAvailableLayouts() const;

private:
    KeyboardLayoutManager() {} // Private constructor for singleton
    QMap<QString, KeyboardLayoutConfig> layouts;
};

#endif // KEYBOARD_LAYOUTS_H 