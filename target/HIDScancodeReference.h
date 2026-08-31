#ifndef HIDSCANCODEREFERENCE_H
#define HIDSCANCODEREFERENCE_H

#include <QMap>
#include <QString>
#include <QChar>
#include <QList>

/**
 * @brief USB HID Scancode Reference
 * 
 * This class provides reference data for USB HID keyboard scancodes
 * and helper functions to work with them.
 */
namespace HIDScancode {

/**
 * @brief Information about a HID scancode
 */
struct KeyInfo {
    uint8_t scancode;
    QString name;
    QString description;
    
    KeyInfo() : scancode(0) {}
    KeyInfo(uint8_t code, const QString& n, const QString& desc = QString())
        : scancode(code), name(n), description(desc) {}
};

/**
 * @brief Get reference information for all HID scancodes
 */
inline const QMap<uint8_t, KeyInfo>& getReference() {
    static QMap<uint8_t, KeyInfo> reference = {
        // Letters
        {0x04, KeyInfo(0x04, "A", "Letter A")},
        {0x05, KeyInfo(0x05, "B", "Letter B")},
        {0x06, KeyInfo(0x06, "C", "Letter C")},
        {0x07, KeyInfo(0x07, "D", "Letter D")},
        {0x08, KeyInfo(0x08, "E", "Letter E")},
        {0x09, KeyInfo(0x09, "F", "Letter F")},
        {0x0A, KeyInfo(0x0A, "G", "Letter G")},
        {0x0B, KeyInfo(0x0B, "H", "Letter H")},
        {0x0C, KeyInfo(0x0C, "I", "Letter I")},
        {0x0D, KeyInfo(0x0D, "J", "Letter J")},
        {0x0E, KeyInfo(0x0E, "K", "Letter K")},
        {0x0F, KeyInfo(0x0F, "L", "Letter L")},
        {0x10, KeyInfo(0x10, "M", "Letter M")},
        {0x11, KeyInfo(0x11, "N", "Letter N")},
        {0x12, KeyInfo(0x12, "O", "Letter O")},
        {0x13, KeyInfo(0x13, "P", "Letter P")},
        {0x14, KeyInfo(0x14, "Q", "Letter Q")},
        {0x15, KeyInfo(0x15, "R", "Letter R")},
        {0x16, KeyInfo(0x16, "S", "Letter S")},
        {0x17, KeyInfo(0x17, "T", "Letter T")},
        {0x18, KeyInfo(0x18, "U", "Letter U")},
        {0x19, KeyInfo(0x19, "V", "Letter V")},
        {0x1A, KeyInfo(0x1A, "W", "Letter W")},
        {0x1B, KeyInfo(0x1B, "X", "Letter X")},
        {0x1C, KeyInfo(0x1C, "Y", "Letter Y")},
        {0x1D, KeyInfo(0x1D, "Z", "Letter Z")},
        
        // Numbers
        {0x1E, KeyInfo(0x1E, "1", "Number 1 (US: 1/!, FR: 1/&)")},
        {0x1F, KeyInfo(0x1F, "2", "Number 2 (US: 2/@, FR: 2/é/~)")},
        {0x20, KeyInfo(0x20, "3", "Number 3 (US: 3/#, FR: 3/\"/# )")},
        {0x21, KeyInfo(0x21, "4", "Number 4 (US: 4/$, FR: 4/')")},
        {0x22, KeyInfo(0x22, "5", "Number 5 (US: 5/%, FR: 5/()")},
        {0x23, KeyInfo(0x23, "6", "Number 6 (US: 6/^, FR: 6/-)")},
        {0x24, KeyInfo(0x24, "7", "Number 7 (US: 7/&, FR: 7/è)")},
        {0x25, KeyInfo(0x25, "8", "Number 8 (US: 8/*, FR: 8/_)")},
        {0x26, KeyInfo(0x26, "9", "Number 9 (US: 9/(, FR: 9/ç)")},
        {0x27, KeyInfo(0x27, "0", "Number 0 (US: 0/), FR: 0/à)")},
        
        // Special keys
        {0x28, KeyInfo(0x28, "Enter", "Enter/Return key")},
        {0x29, KeyInfo(0x29, "Escape", "Escape key")},
        {0x2A, KeyInfo(0x2A, "Backspace", "Backspace key")},
        {0x2B, KeyInfo(0x2B, "Tab", "Tab key")},
        {0x2C, KeyInfo(0x2C, "Space", "Spacebar")},
        
        // Punctuation and symbols (varies by layout)
        {0x2D, KeyInfo(0x2D, "-/_", "Minus/Underscore (US: -/_, FR: )/°)")},
        {0x2E, KeyInfo(0x2E, "=/+", "Equal/Plus (US: =/+, FR: =/+)")},
        {0x2F, KeyInfo(0x2F, "[/{", "Left Bracket (US: [/{, FR: ^/¨)")},
        {0x30, KeyInfo(0x30, "]/}", "Right Bracket (US: ]/}, FR: $/£)")},
        {0x31, KeyInfo(0x31, "\\/|", "Backslash/Pipe (US: \\/|, FR: */μ)")},
        {0x32, KeyInfo(0x32, "Non-US #", "Non-US Hash/Tilde (UK: #/~)")},
        {0x33, KeyInfo(0x33, ";/:", "Semicolon/Colon (US: ;/:, FR: M key)")},
        {0x34, KeyInfo(0x34, "'/\"", "Apostrophe/Quote (US: '/\", FR: ù/%)")},
        {0x35, KeyInfo(0x35, "`/~", "Grave/Tilde (US: `/~, FR: ²/³)")},
        {0x36, KeyInfo(0x36, ",/<", "Comma/Less (US: ,/<, FR: ,/? )")},
        {0x37, KeyInfo(0x37, "./>", "Period/Greater (US: ./>, FR: ;/. )")},
        {0x38, KeyInfo(0x38, "?", "Slash/Question (US: /?, FR: :/! /§)")},
        
        // Lock keys
        {0x39, KeyInfo(0x39, "CapsLock", "Caps Lock")},
        
        // Function keys
        {0x3A, KeyInfo(0x3A, "F1", "Function key F1")},
        {0x3B, KeyInfo(0x3B, "F2", "Function key F2")},
        {0x3C, KeyInfo(0x3C, "F3", "Function key F3")},
        {0x3D, KeyInfo(0x3D, "F4", "Function key F4")},
        {0x3E, KeyInfo(0x3E, "F5", "Function key F5")},
        {0x3F, KeyInfo(0x3F, "F6", "Function key F6")},
        {0x40, KeyInfo(0x40, "F7", "Function key F7")},
        {0x41, KeyInfo(0x41, "F8", "Function key F8")},
        {0x42, KeyInfo(0x42, "F9", "Function key F9")},
        {0x43, KeyInfo(0x43, "F10", "Function key F10")},
        {0x44, KeyInfo(0x44, "F11", "Function key F11")},
        {0x45, KeyInfo(0x45, "F12", "Function key F12")},
        
        // Navigation keys
        {0x49, KeyInfo(0x49, "Insert", "Insert key")},
        {0x4A, KeyInfo(0x4A, "Home", "Home key")},
        {0x4B, KeyInfo(0x4B, "PageUp", "Page Up key")},
        {0x4C, KeyInfo(0x4C, "Delete", "Delete Forward key")},
        {0x4D, KeyInfo(0x4D, "End", "End key")},
        {0x4E, KeyInfo(0x4E, "PageDown", "Page Down key")},
        {0x4F, KeyInfo(0x4F, "RightArrow", "Right Arrow key")},
        {0x50, KeyInfo(0x50, "LeftArrow", "Left Arrow key")},
        {0x51, KeyInfo(0x51, "DownArrow", "Down Arrow key")},
        {0x52, KeyInfo(0x52, "UpArrow", "Up Arrow key")},
        
        // Numpad Enter
        {0x58, KeyInfo(0x58, "NumpadEnter", "Numpad Enter key")},
        
        // Modifier keys
        {0xE0, KeyInfo(0xE0, "LeftCtrl", "Left Control")},
        {0xE1, KeyInfo(0xE1, "LeftShift", "Left Shift")},
        {0xE2, KeyInfo(0xE2, "LeftAlt", "Left Alt")},
        {0xE3, KeyInfo(0xE3, "LeftMeta", "Left GUI/Meta/Windows")},
        {0xE4, KeyInfo(0xE4, "RightCtrl", "Right Control")},
        {0xE5, KeyInfo(0xE5, "RightShift", "Right Shift")},
        {0xE6, KeyInfo(0xE6, "RightAlt", "Right Alt/AltGr")},
        {0xE7, KeyInfo(0xE7, "RightMeta", "Right GUI/Meta/Windows")}
    };
    return reference;
}

/**
 * @brief Find HID scancode by key name
 */
inline uint8_t findByName(const QString& keyName) {
    const auto& ref = getReference();
    for (auto it = ref.begin(); it != ref.end(); ++it) {
        if (it.value().name.compare(keyName, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }
    return 0;
}

/**
 * @brief Get description for a HID scancode
 */
inline QString describe(uint8_t scancode) {
    const auto& ref = getReference();
    if (ref.contains(scancode)) {
        const KeyInfo& info = ref[scancode];
        return QString("%1 - %2").arg(info.name).arg(info.description);
    }
    return QString("Unknown (0x%1)").arg(scancode, 2, 16, QChar('0'));
}

/**
 * @brief Find possible HID scancodes for a character by searching all layouts
 */
QList<uint8_t> findPossibleScancodes(QChar character);

/**
 * @brief Special character groups for testing
 */
namespace CharacterGroups {
    inline const QList<QChar> punctuation() {
        return {'!', '@', '#', '$', '%', '^', '&', '*'};
    }
    
    inline const QList<QChar> brackets() {
        return {'(', ')', '[', ']', '{', '}', '<', '>'};
    }
    
    inline const QList<QChar> quotes() {
        return {'\'', '"', '`', '~'};
    }
    
    inline const QList<QChar> math() {
        return {'+', '-', '=', '_', '/', '\\', '|'};
    }
    
    inline const QList<QChar> other() {
        return {';', ':', ',', '.', '?'};
    }
    
    inline const QList<QChar> allSpecialChars() {
        QList<QChar> all;
        all << punctuation() << brackets() << quotes() << math() << other();
        return all;
    }
}

} // namespace HIDScancode

#endif // HIDSCANCODEREFERENCE_H
