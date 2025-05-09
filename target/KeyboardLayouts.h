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
    QMap<uint32_t, uint8_t> unicodeMap;
    QList<int> needShiftKeys;
    QList<int> needAltGrKeys;
    bool isRightToLeft;
    
    // Constructor with default values
    KeyboardLayoutConfig(
        const QString& layoutName = "",
        bool rtl = false
    ) : name(layoutName), isRightToLeft(rtl) {}

    // Load from JSON file
    static KeyboardLayoutConfig fromJsonFile(const QString& filePath);

    static void initializeKeyNameToQt(QMap<QString, int>& keyNameToQt) {
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
        keyNameToQt["Minus"] = Qt::Key_Minus;           // -
        keyNameToQt["Equal"] = Qt::Key_Equal;           // =
        keyNameToQt["BracketLeft"] = Qt::Key_BracketLeft;    // [
        keyNameToQt["BracketRight"] = Qt::Key_BracketRight;  // ]
        keyNameToQt["Backslash"] = Qt::Key_Backslash;   //
        keyNameToQt["Semicolon"] = Qt::Key_Semicolon;   // ;
        keyNameToQt["Apostrophe"] = Qt::Key_Apostrophe; // '
        keyNameToQt["QuoteLeft"] = Qt::Key_QuoteLeft;   // `
        keyNameToQt["Comma"] = Qt::Key_Comma;           // ,
        keyNameToQt["Period"] = Qt::Key_Period;         // .
        keyNameToQt["Slash"] = Qt::Key_Slash;           // /
        keyNameToQt["Hash"] = Qt::Key_NumberSign;       // #
        keyNameToQt["Ampersand"] = Qt::Key_Ampersand;   // &
        keyNameToQt["Asterisk"] = Qt::Key_Asterisk;     // *
        keyNameToQt["ParenLeft"] = Qt::Key_ParenLeft;   // (
        keyNameToQt["ParenRight"] = Qt::Key_ParenRight; // )
        keyNameToQt["Exclam"] = Qt::Key_Exclam;         // !
        keyNameToQt["At"] = Qt::Key_At;                 // @
        keyNameToQt["Dollar"] = Qt::Key_Dollar;         // $
        keyNameToQt["Percent"] = Qt::Key_Percent;       // %
        keyNameToQt["AsciiCircum"] = Qt::Key_AsciiCircum; // ^
        keyNameToQt["AsciiTilde"] = Qt::Key_AsciiTilde; // ~
        keyNameToQt["Underscore"] = Qt::Key_Underscore; // _
        keyNameToQt["Plus"] = Qt::Key_Plus;             // +
        keyNameToQt["BraceLeft"] = Qt::Key_BraceLeft;   // {
        keyNameToQt["BraceRight"] = Qt::Key_BraceRight; // }
        keyNameToQt["Bar"] = Qt::Key_Bar;               // |
        keyNameToQt["Colon"] = Qt::Key_Colon;           // :
        keyNameToQt["QuoteDbl"] = Qt::Key_QuoteDbl;     // "
        keyNameToQt["Less"] = Qt::Key_Less;             // <
        keyNameToQt["Greater"] = Qt::Key_Greater;       // >
        keyNameToQt["Question"] = Qt::Key_Question;     // ?
        // UK-specific keys
        keyNameToQt["sterling"] = Qt::Key_sterling;     // £
        keyNameToQt["AltGr"] = Qt::Key_AltGr;
        // German-specific keys
        keyNameToQt["Udiaeresis"] = Qt::Key_Udiaeresis; // Ü
        keyNameToQt["Adiaeresis"] = Qt::Key_Adiaeresis; // Ä
        keyNameToQt["Odiaeresis"] = Qt::Key_Odiaeresis; // Ö
        keyNameToQt["ssharp"] = Qt::Key_ssharp;         // ß
        keyNameToQt["Egrave"] = Qt::Key_Egrave;         // È
        keyNameToQt["Eacute"] = Qt::Key_Eacute;         // É
        keyNameToQt["Agrave"] = Qt::Key_Agrave;         // À
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
        // Japanese-specific keys
        keyNameToQt["Henkan"] = Qt::Key_Henkan;         // 変換
        keyNameToQt["Kana"] = Qt::Key_Kanji;            // かな/カナ
        keyNameToQt["KatakanaHiragana"] = Qt::Key_Katakana; // カタカナ/ひらがな
        keyNameToQt["Muhenkan"] = Qt::Key_Muhenkan;     // 無変換
        keyNameToQt["Yen"] = Qt::Key_yen;               // ¥
        keyNameToQt["ZenkakuHankaku"] = Qt::Key_Zenkaku_Hankaku; // 全角/半角
        // Scandinavian keys
        keyNameToQt["AE"] = Qt::Key_AE;                 // Æ
        keyNameToQt["Aring"] = Qt::Key_Aring;           // Å
        keyNameToQt["Ooblique"] = Qt::Key_Ooblique;     // Ø
        keyNameToQt["section"] = Qt::Key_section;       // §
        keyNameToQt["Section"] = Qt::Key_section;       // §
        keyNameToQt["onehalf"] = Qt::Key_onehalf;       // ½
        // Dead keys
        keyNameToQt["Dead_Acute"] = Qt::Key_Dead_Acute;       // ´ (dead key)
        keyNameToQt["Dead_Circumflex"] = Qt::Key_Dead_Circumflex; // ^ (dead key)
        keyNameToQt["Dead_Diaeresis"] = Qt::Key_Dead_Diaeresis;  // ¨ (dead key)
        keyNameToQt["Dead_Grave"] = Qt::Key_Dead_Grave;      // ` (dead key)
        keyNameToQt["Dead_Tilde"] = Qt::Key_Dead_Tilde;      // ~ (dead key)
        // Additional special characters
        keyNameToQt["AsciiCircum"] = Qt::Key_AsciiCircum;     // ^
        keyNameToQt["Grave"] = Qt::Key_QuoteLeft;       // `
        keyNameToQt["acute"] = Qt::Key_acute;           // ´
        keyNameToQt["currency"] = Qt::Key_currency;     // ¤
        keyNameToQt["NumberSign"] = Qt::Key_NumberSign; // #
        // altgr keys
        
        keyNameToQt["degree"] = Qt::Key_degree;   // ¨
    }
    
private:
    static QMap<QString, int> keyNameToQt;
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
