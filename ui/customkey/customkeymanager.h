#ifndef CUSTOMKEYMANAGER_H
#define CUSTOMKEYMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_custom_keys)

// Maximum number of keys in a single key combination
static const int MAX_KEY_COMBO = 6;

// A single step in a key sequence (kept for backwards compatibility with handleKeySequence)
struct KeyStep {
    int modifiers;   // Qt modifier flags (Qt::ControlModifier, etc.)
    int keyCode;     // Qt key code
};

// Information about a custom key button
struct CustomKeyInfo {
    QString displayName;          // Display name (max 6 chars shown on button)
    QList<int> keyCodes;          // Key codes (modifiers + regular keys, max 6)
    bool isSeparator;             // True for separator/divider
    QString specialCombo;         // "ctrl_alt_del" or empty
};

class CustomKeyManager : public QObject {
    Q_OBJECT

public:
    static CustomKeyManager& getInstance();

    // Load configuration from default embedded resource
    void initialize();

    // Get current key list
    QList<CustomKeyInfo> getKeys() const;

    // Set current key list (triggers save)
    void setKeys(const QList<CustomKeyInfo>& keys);

    // Import keys from external JSON file
    bool importFromJson(const QString& filePath);

    // Export current keys to JSON file
    bool exportToJson(const QString& filePath) const;

    // Get preset names (all .json files in custom keys dir)
    QStringList getPresets() const;

    // Load a specific preset by filename
    bool loadPreset(const QString& presetName);

    // Save current keys as a preset
    bool savePreset(const QString& presetName);

    // Delete a preset
    bool deletePreset(const QString& presetName);

    // Get current preset name
    QString getCurrentPresetName() const;

    // Convert human-readable key name to Qt key code
    static int keyNameToCode(const QString& name);

    // Convert Qt key code to human-readable key name
    static QString codeToKeyName(int code);

    // Convert human-readable modifier name to Qt modifier flag
    static int modifierNameToFlag(const QString& name);

    // Check if a key code is a modifier key
    static bool isModifierKey(int keyCode);

    // Convert Qt modifier flag to list of names
    static QStringList modifierFlagToNames(int modifiers);

private:
    explicit CustomKeyManager(QObject *parent = nullptr);

    QString getCustomKeysDir() const;
    QList<CustomKeyInfo> parseJsonKeys(const QJsonArray& keysArray) const;
    QJsonArray toJsonKeys(const QList<CustomKeyInfo>& keys) const;
    bool loadFromFile(const QString& filePath);

    QList<CustomKeyInfo> m_keys;
    QString m_currentPresetName;
};

#endif // CUSTOMKEYMANAGER_H
