#include "VirtualKeyboardDialog.h"
#include "../../host/HostManager.h"
#include "../../target/KeyboardLayouts.h"
#include "log/opflogging.h"
#include <QKeySequence>
#include <QDebug>
#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QFontDatabase>
#include "ShiftedKeyButton.h"

OPF_LOGGING_CATEGORY(log_virtual_keyboard, "opf.ui.keyboard")

// Neutral key style (no color coding)
static const QString buttonStyle =
    "QPushButton { "
    "   border: 1px solid #999; border-radius: 4px; "
    "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "       stop:0 #ffffff, stop:0.5 #f5f5f5, stop:1 #e8e8e8); "
    "   color: #000; padding: 4px 6px; margin: 1px; "
    "   font-size: 11px; font-weight: 600; "
    "} "
    "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f0f0f0, stop:0.5 #e8e8e8, stop:1 #d8d8d8); } "
    "QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d0d0d0, stop:0.5 #c0c0c0, stop:1 #b0b0b0); }";

static const QString modifierKeyStyle =
    "QPushButton { "
    "   border: 1px solid #999; border-radius: 4px; "
    "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
    "       stop:0 #f5f5f5, stop:0.5 #e0e0e0, stop:1 #cccccc); "
    "   color: #222; padding: 4px 6px; margin: 1px; "
    "   font-size: 10px; font-weight: 600; "
    "} "
    "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #eeeeee, stop:0.5 #d8d8d8, stop:1 #c0c0c0); } "
    "QPushButton:checked { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d0e8ff, stop:0.5 #a0c8ff, stop:1 #80b0ff); border: 2px solid #4a90d9; color: #1a3a5c; } "
    "QPushButton:checked:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #c0dfff, stop:0.5 #90c0ff, stop:1 #70a8ff); }";

VirtualKeyboardDialog::VirtualKeyboardDialog(QWidget *parent, const QString& layoutName)
    : QDialog(parent)
    , currentModifiers(0)
    , m_dragging(false)
    , m_currentLayoutName(layoutName)
{
    qInfo() << "[VirtualKeyboardDialog] Constructor called with layoutName:" << layoutName;

    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    setupUI();
    createKeyboardLayout();

    // Update key labels based on the selected layout
    if (!m_currentLayoutName.isEmpty()) {
        qInfo() << "[VirtualKeyboardDialog] Calling updateKeyLabelsForLayout with:" << m_currentLayoutName;
        updateKeyLabelsForLayout(m_currentLayoutName);
    } else {
        qWarning() << "[VirtualKeyboardDialog] m_currentLayoutName is empty, skipping updateKeyLabelsForLayout";
    }

    // Set a font that supports Japanese characters
    QFont japaneseFont;
    QStringList japaneseFontFamilies = {
        "Noto Sans CJK JP",
        "Takao Gothic",
        "IPAGothic",
        "Droid Sans Japanese",
        "Noto Sans Mono CJK JP",
        "Droid Sans Fallback"
    };

    for (const QString& family : japaneseFontFamilies) {
        QFont testFont(family);
        if (QFontDatabase::families().contains(family)) {
            japaneseFont = testFont;
            qCInfo(log_virtual_keyboard) << "Found Japanese font family:" << family;
            break;
        }
    }

    // If none of the specific Japanese fonts are found, try to find any font that supports Japanese
    if (japaneseFont.family().isEmpty()) {
        // Fallback: try to find a font that can display Japanese characters
        QStringList families = QFontDatabase::families();
        for (const QString& family : families) {
            QFont testFont(family);
            // Check if the font can actually render Japanese characters
            // This is a heuristic - if the font name contains certain keywords, it might support CJK
            if (family.contains("Noto", Qt::CaseInsensitive) &&
                (family.contains("CJK", Qt::CaseInsensitive) ||
                 family.contains("Sans", Qt::CaseInsensitive))) {
                japaneseFont = testFont;
                qCInfo(log_virtual_keyboard) << "Found fallback Japanese font family:" << family;
                break;
            }
        }
    }

    if (!japaneseFont.family().isEmpty()) {
        // Set the font on this dialog
        this->setFont(japaneseFont);
        qCInfo(log_virtual_keyboard) << "Set dialog font to:" << japaneseFont.family();
    } else {
        qCInfo(log_virtual_keyboard) << "No Japanese-capable font found, using system default";
    }

    adjustSize();
}

void VirtualKeyboardDialog::setupUI()
{
    setStyleSheet(
        "QDialog { "
        "   background-color: #e5e5e5; "
        "   border: 1px solid #999; "
        "   border-radius: 8px; "
        "}"
    );

    mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(3);
    mainLayout->setContentsMargins(10, 6, 10, 10);

    QWidget* keyboardContainer = new QWidget(this);
    keyboardContainer->setStyleSheet(
        "QWidget { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "       stop:0 #d0d0d0, stop:0.1 #e0e0e0, stop:0.9 #d8d8d8, stop:1 #c0c0c0); "
        "   border: 2px solid #888; "
        "   border-radius: 6px; "
        "}"
    );

    // Use QVBoxLayout for rows — each row is an independent QHBoxLayout
    QVBoxLayout* rowsLayout = new QVBoxLayout(keyboardContainer);
    rowsLayout->setSpacing(3);
    rowsLayout->setContentsMargins(6, 6, 6, 6);

    mainLayout->addWidget(keyboardContainer);
    setLayout(mainLayout);

    // Store rowsLayout for row builders
    keyboardLayout = nullptr; // not used anymore
    m_rowsLayout = rowsLayout;
}

void VirtualKeyboardDialog::createKeyboardLayout()
{
    // Build the standard QWERTY layout for visual consistency
    // The actual character mapping is handled by KeyboardLayoutManager
    // based on m_currentLayoutName when sending keystrokes
    addFunctionKeyRow(m_rowsLayout);
    addNumberRow(m_rowsLayout);
    addTabRow(m_rowsLayout);
    addCapsRow(m_rowsLayout);
    addShiftRow(m_rowsLayout);
    addBottomRow(m_rowsLayout);
}

void VirtualKeyboardDialog::setKeyboardLayout(const QString& layoutName)
{
    if (m_currentLayoutName == layoutName) {
        return; // No change needed
    }

    qCInfo(log_virtual_keyboard) << "VirtualKeyboardDialog: Changing layout from" << m_currentLayoutName << "to" << layoutName;
    m_currentLayoutName = layoutName;

    // Clear existing layout and rebuild with new key positions
    clearKeyboardLayout();
    createKeyboardLayout();

    // Update key labels based on the new layout
    updateKeyLabelsForLayout(layoutName);
}

void VirtualKeyboardDialog::updateKeyLabelsForLayout(const QString& layoutName)
{
    qCInfo(log_virtual_keyboard) << "updateKeyLabelsForLayout called with layout:" << layoutName;

    // Get the layout configuration
    KeyboardLayoutConfig config = KeyboardLayoutManager::getInstance().getLayout(layoutName);
    if (config.name.isEmpty()) {
        qWarning() << "Layout not found:" << layoutName;
        return;
    }

    qCInfo(log_virtual_keyboard) << "Found layout:" << config.name << "with" << config.charMapping.size() << "char mappings";

    // Debug: Show charMapping entries for number keys
    qInfo() << "[updateKeyLabelsForLayout] charMapping for number keys:";
    for (int keyCode = Qt::Key_0; keyCode <= Qt::Key_9; ++keyCode) {
        qInfo() << "  Looking for characters that map to keyCode" << keyCode << "(0x" << QString::number(keyCode, 16) << "):";
        for (auto it = config.charMapping.begin(); it != config.charMapping.end(); ++it) {
            if (it.value() == keyCode) {
                QChar ch = QChar(it.key());
                bool needsShift = config.needShiftKeys.contains(ch.unicode());
                qInfo() << "    Character" << QString(ch) << "(U+" << QString::number(ch.unicode(), 16) << ") needsShift:" << needsShift;
            }
        }
    }

    // Build a mapping: Qt key code -> (base character, shifted character)
    // The JSON charMapping tells us: character -> Qt key code
    // We need to reverse this to: Qt key code -> character(s)
    QMap<int, QPair<QString, QString>> keyToChars;

    // Special keys that should keep their text labels (functional keys)
    QSet<int> specialFunctionKeys;
    specialFunctionKeys.insert(Qt::Key_Tab);
    specialFunctionKeys.insert(Qt::Key_Backspace);
    specialFunctionKeys.insert(Qt::Key_Return);
    specialFunctionKeys.insert(Qt::Key_Enter);
    specialFunctionKeys.insert(Qt::Key_Escape);
    specialFunctionKeys.insert(Qt::Key_Delete);
    specialFunctionKeys.insert(Qt::Key_Insert);
    specialFunctionKeys.insert(Qt::Key_Pause);
    specialFunctionKeys.insert(Qt::Key_Print);
    specialFunctionKeys.insert(Qt::Key_SysReq);
    specialFunctionKeys.insert(Qt::Key_Clear);
    specialFunctionKeys.insert(Qt::Key_CapsLock);
    specialFunctionKeys.insert(Qt::Key_NumLock);
    specialFunctionKeys.insert(Qt::Key_ScrollLock);
    specialFunctionKeys.insert(Qt::Key_Kanji);
    specialFunctionKeys.insert(Qt::Key_Muhenkan);
    specialFunctionKeys.insert(Qt::Key_Henkan);
    specialFunctionKeys.insert(Qt::Key_yen);
    specialFunctionKeys.insert(Qt::Key_Zenkaku);
    specialFunctionKeys.insert(Qt::Key_Zenkaku_Hankaku);
    // Function keys
    for (int i = Qt::Key_F1; i <= Qt::Key_F35; ++i) {
        specialFunctionKeys.insert(i);
    }
    // Modifier keys (they show text like "Ctrl", "Alt", etc.)
    specialFunctionKeys.insert(Qt::Key_Control);
    specialFunctionKeys.insert(Qt::Key_Shift);
    specialFunctionKeys.insert(Qt::Key_Alt);
    specialFunctionKeys.insert(Qt::Key_Meta);
    specialFunctionKeys.insert(Qt::Key_AltGr);
    // Navigation keys
    specialFunctionKeys.insert(Qt::Key_Left);
    specialFunctionKeys.insert(Qt::Key_Up);
    specialFunctionKeys.insert(Qt::Key_Right);
    specialFunctionKeys.insert(Qt::Key_Down);
    specialFunctionKeys.insert(Qt::Key_Home);
    specialFunctionKeys.insert(Qt::Key_End);
    specialFunctionKeys.insert(Qt::Key_PageUp);
    specialFunctionKeys.insert(Qt::Key_PageDown);

    // Iterate through char_mapping to build the reverse mapping
    // charMapping is QMap<int, int> where:
    // - key (int) is the Unicode codepoint of the character
    // - value (int) is the Qt key code
    for (auto it = config.charMapping.begin(); it != config.charMapping.end(); ++it) {
        QChar ch = QChar(it.key());
        int qtKeyCode = it.value();

        if (qtKeyCode == 0) {
            continue;
        }

        // Skip special function keys - we'll keep their text labels unchanged
        if (specialFunctionKeys.contains(qtKeyCode)) {
            continue;
        }

        // Initialize the entry if not present
        if (!keyToChars.contains(qtKeyCode)) {
            keyToChars[qtKeyCode] = qMakePair(QString(), QString());
        }

        // Determine if this character requires shift
        bool needsShift = config.needShiftKeys.contains(ch.unicode());

        // Check if this is a basic ASCII letter or digit
        bool isBasicLetter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');

        if (needsShift) {
            // This character is produced when shift is pressed
            QString& currentShifted = keyToChars[qtKeyCode].second;
            // Overwrite if:
            // 1. Slot is empty, OR
            // 2. Current is a basic letter/digit and new is special (prefer special shifted chars like "È" over "E")
            bool currentIsBasic = !currentShifted.isEmpty() &&
                                  ((currentShifted[0] >= 'a' && currentShifted[0] <= 'z') ||
                                   (currentShifted[0] >= 'A' && currentShifted[0] <= 'Z') ||
                                   (currentShifted[0] >= '0' && currentShifted[0] <= '9'));
            if (currentShifted.isEmpty() || (currentIsBasic && !isBasicLetter)) {
                currentShifted = QString(ch);
            }
        } else {
            // This character is produced without shift
            QString& currentBase = keyToChars[qtKeyCode].first;
            // Only overwrite if slot is empty or current is also a basic letter
            // This prevents special chars like "è" from overwriting basic "e"
            bool currentIsBasic = !currentBase.isEmpty() &&
                                  ((currentBase[0] >= 'a' && currentBase[0] <= 'z') ||
                                   (currentBase[0] >= 'A' && currentBase[0] <= 'Z') ||
                                   (currentBase[0] >= '0' && currentBase[0] <= '9'));
            if (currentBase.isEmpty() || (currentIsBasic && isBasicLetter)) {
                currentBase = QString(ch);
            }
        }
    }

    // Second pass: for keys that only have one character defined,
    // duplicate it to both base and shifted (for keys like Space, Enter, etc.)
    for (auto it = keyToChars.begin(); it != keyToChars.end(); ++it) {
        if (it.value().first.isEmpty() && !it.value().second.isEmpty()) {
            // Only shifted defined, use it for base too
            it.value().first = it.value().second;
        } else if (!it.value().first.isEmpty() && it.value().second.isEmpty()) {
            // Only base defined, use it for shifted too
            it.value().second = it.value().first;
        }
    }

    qCInfo(log_virtual_keyboard) << "Built keyToChars mapping with" << keyToChars.size() << "entries";

    // Debug: Print ALL keyToChars entries for letter keys
    qInfo() << "[updateKeyLabelsForLayout] keyToChars for letter keys:";
    for (int keyCode = Qt::Key_A; keyCode <= Qt::Key_Z; ++keyCode) {
        if (keyToChars.contains(keyCode)) {
            QString base = keyToChars[keyCode].first;
            QString shifted = keyToChars[keyCode].second;
            qInfo() << "  Key" << QChar('A' + (keyCode - Qt::Key_A)) << "(0x" << QString::number(keyCode, 16) << "): base='" << base << "' shifted='" << shifted << "'";
        }
    }

    // Now update all the buttons
    int updatedCount = 0;
    int skippedCount = 0;
    qInfo() << "[updateKeyLabelsForLayout] Updating" << keyCodeToWidgetMap.size() << "widgets";
    for (auto it = keyCodeToWidgetMap.begin(); it != keyCodeToWidgetMap.end(); ++it) {
        int keyCode = it.key();
        QWidget* widget = it.value();

        if (!widget) continue;

        QPushButton* button = qobject_cast<QPushButton*>(widget);
        ShiftedKeyButton* shiftedButton = qobject_cast<ShiftedKeyButton*>(widget);

        if (button && !shiftedButton) {
            // Regular button - update text if we have a mapping and it's not a special function key
            if (!specialFunctionKeys.contains(keyCode) && keyToChars.contains(keyCode)) {
                QString baseChar = keyToChars[keyCode].first;
                if (!baseChar.isEmpty()) {
                    button->setText(baseChar);
                    updatedCount++;
                    // Log all key updates
                    qInfo() << "[updateKeyLabelsForLayout] Updated QPushButton keyCode:" << keyCode << "(0x" << QString::number(keyCode, 16) << ") to text:" << baseChar;
                }
            } else {
                skippedCount++;
            }
            // Special function keys keep their text unchanged
        } else if (shiftedButton) {
            // ShiftedKeyButton - update both base and shifted characters
            if (keyToChars.contains(keyCode)) {
                QString baseChar = keyToChars[keyCode].first;
                QString shiftedChar = keyToChars[keyCode].second;

                if (!baseChar.isEmpty() || !shiftedChar.isEmpty()) {
                    // Update the shifted button's labels
                    shiftedButton->setCharacters(baseChar, shiftedChar);
                    updatedCount++;
                    // Log all key updates
                    qInfo() << "[updateKeyLabelsForLayout] Updated ShiftedKeyButton keyCode:" << keyCode << "(0x" << QString::number(keyCode, 16) << ") to base:" << baseChar << "shifted:" << shiftedChar;
                }
            } else {
                skippedCount++;
            }
        }
    }

    qCInfo(log_virtual_keyboard) << "updateKeyLabelsForLayout complete:" << updatedCount << "updated," << skippedCount << "skipped," << keyCodeToWidgetMap.size() << "total widgets";
}

// Helper function to find what character a Qt key code produces in a given layout
static QString findCharForQtKey(int qtKeyCode, const KeyboardLayoutConfig& config, bool shifted = false)
{
    qCInfo(log_virtual_keyboard) << "findCharForQtKey: looking for keyCode" << qtKeyCode << "in layout" << config.name << "(shifted=" << shifted << ")";

    // Debug: Print all entries in char_mapping
    qCInfo(log_virtual_keyboard) << "char_mapping contents:";
    for (auto it = config.charMapping.begin(); it != config.charMapping.end(); ++it) {
        QChar ch = QChar(it.key());
        int mappedKeyCode = it.value();
        qCInfo(log_virtual_keyboard) << "  " << QString(ch) << "(U+" << QString::number(ch.unicode(), 16) << ") -> keyCode 0x" << QString::number(mappedKeyCode, 16) << " (" << mappedKeyCode << ")";
    }

    // Build reverse mapping: Qt key code -> character
    // For layouts like Japanese, we need to find ALL characters that map to this key
    // and pick the right one (unshifted by default, shifted if requested)
    QList<QChar> unshiftedChars;
    QList<QChar> shiftedChars;

    for (auto it = config.charMapping.begin(); it != config.charMapping.end(); ++it) {
        QChar ch = QChar(it.key());
        int mappedKeyCode = it.value();

        if (mappedKeyCode == qtKeyCode) {
            qCInfo(log_virtual_keyboard) << "  Found char" << QString(ch) << "(U+" << QString::number(ch.unicode(), 16) << ") -> keyCode" << mappedKeyCode;

            // Check if this character needs shift
            bool needsShift = config.needShiftKeys.contains(ch.unicode());
            qCInfo(log_virtual_keyboard) << "    needsShift: " << needsShift << " (shifted=" << shifted << ")";

            if (needsShift) {
                shiftedChars.append(ch);
                qCInfo(log_virtual_keyboard) << "    Added to shiftedChars";
            } else {
                unshiftedChars.append(ch);
                qCInfo(log_virtual_keyboard) << "    Added to unshiftedChars";
            }
        }
    }

    qCInfo(log_virtual_keyboard) << "  unshiftedChars count:" << unshiftedChars.size() << ", shiftedChars count:" << shiftedChars.size();

    // Helper to find basic ASCII letter in a list (prefer "e" over "è")
    auto findBasicLetter = [](const QList<QChar>& chars) -> QChar {
        for (QChar ch : chars) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                return ch;
            }
        }
        return QChar();
    };

    // Return the appropriate character, preferring basic ASCII letters
    if (shifted && !shiftedChars.isEmpty()) {
        QChar basicLetter = findBasicLetter(shiftedChars);
        if (!basicLetter.isNull()) {
            qCInfo(log_virtual_keyboard) << "  Returning basic shifted char" << QString(basicLetter);
            return QString(basicLetter);
        }
        qCInfo(log_virtual_keyboard) << "  Returning first shifted char" << QString(shiftedChars.first());
        return QString(shiftedChars.first());
    } else if (!unshiftedChars.isEmpty()) {
        QChar basicLetter = findBasicLetter(unshiftedChars);
        if (!basicLetter.isNull()) {
            qCInfo(log_virtual_keyboard) << "  Returning basic unshifted char" << QString(basicLetter);
            return QString(basicLetter);
        }
        qCInfo(log_virtual_keyboard) << "  Returning first unshifted char" << QString(unshiftedChars.first());
        return QString(unshiftedChars.first());
    } else if (!shiftedChars.isEmpty()) {
        // If we only have shifted chars, return the first one
        QChar basicLetter = findBasicLetter(shiftedChars);
        if (!basicLetter.isNull()) {
            qCInfo(log_virtual_keyboard) << "  Returning basic shifted char" << QString(basicLetter);
            return QString(basicLetter);
        }
        qCInfo(log_virtual_keyboard) << "  Returning first shifted char" << QString(shiftedChars.first());
        return QString(shiftedChars.first());
    }

    qCInfo(log_virtual_keyboard) << "  NO MATCH FOUND for keyCode" << qtKeyCode;

    // Fallback: return default character for this key code
    if (qtKeyCode >= Qt::Key_A && qtKeyCode <= Qt::Key_Z) {
        QChar ch = QChar('A' + (qtKeyCode - Qt::Key_A));
        qCInfo(log_virtual_keyboard) << "  Using fallback char" << QString(ch);
        return shifted ? QString(ch).toUpper() : QString(ch).toLower();
    }

    return QString();
}

// Helper function to find Qt key code for a given character in the current layout
int VirtualKeyboardDialog::findQtKeyForChar(const QString& baseChar, const QString& shiftedChar, const KeyboardLayoutConfig& config)
{
    // If we have a base character, look it up in char_mapping
    if (!baseChar.isEmpty()) {
        QChar baseCh = baseChar[0];  // Take first character (should be single char)
        if (config.charMapping.contains(baseCh.unicode())) {
            return config.charMapping.value(baseCh.unicode());
        }
    }

    // If we have a shifted character and base lookup failed, try the shifted character
    if (!shiftedChar.isEmpty()) {
        QChar shiftedCh = shiftedChar[0];  // Take first character
        if (config.charMapping.contains(shiftedCh.unicode())) {
            return config.charMapping.value(shiftedCh.unicode());
        }
    }

    // If neither worked, return 0 (invalid key code)
    return 0;
}

void VirtualKeyboardDialog::clearKeyboardLayout()
{
    // Clear the widget map
    keyCodeToWidgetMap.clear();
    modifierButtons.clear();

    // Remove all widgets from the rows layout
    if (m_rowsLayout) {
        QLayoutItem* item;
        while ((item = m_rowsLayout->takeAt(0)) != nullptr) {
            if (item->layout()) {
                // Recursively delete sub-layouts
                QLayout* subLayout = item->layout();
                QLayoutItem* subItem;
                while ((subItem = subLayout->takeAt(0)) != nullptr) {
                    if (subItem->widget()) {
                        subItem->widget()->deleteLater();
                    }
                    delete subItem;
                }
                delete subLayout;
            } else if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }
}

QPushButton* VirtualKeyboardDialog::createKeyButton(const QString& text, int keyCode, bool isModifier)
{
    QPushButton* button = new QPushButton(text, this);

    if (isModifier) {
        button->setStyleSheet(modifierKeyStyle);
    } else {
        button->setStyleSheet(buttonStyle);
    }

    // Set font that supports Japanese characters
    QFont buttonFont;
    QStringList japaneseFontFamilies = {
        "Noto Sans CJK JP",
        "Takao Gothic",
        "IPAGothic",
        "Droid Sans Japanese",
        "Noto Sans Mono CJK JP",
        "Droid Sans Fallback"
    };

    for (const QString& family : japaneseFontFamilies) {
        if (QFontDatabase::families().contains(family)) {
            buttonFont = QFont(family);
            break;
        }
    }

    if (!buttonFont.family().isEmpty()) {
        button->setFont(buttonFont);
    }

    button->setProperty("keyCode", keyCode);
    button->setProperty("isModifier", isModifier);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Store in map for quick lookup
    keyCodeToWidgetMap[keyCode] = button;

    if (!isModifier) {
        connect(button, &QPushButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    }

    return button;
}

void VirtualKeyboardDialog::showKeyPressFeedback(int keyCode, int durationMs)
{
    QWidget* widget = findWidgetByKeyCode(keyCode);
    if (!widget) return;

    QPushButton* button = qobject_cast<QPushButton*>(widget);
    ShiftedKeyButton* shiftedButton = qobject_cast<ShiftedKeyButton*>(widget);

    if (button) {
        // Temporarily change the button style to show pressed state
        QString pressedStyle =
            "QPushButton { "
            "   border: 2px solid #4a90d9; border-radius: 4px; "
            "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "       stop:0 #d0e8ff, stop:0.5 #a0c8ff, stop:1 #80b0ff); "
            "   color: #1a3a5c; padding: 4px 6px; margin: 1px; "
            "   font-size: 11px; font-weight: 500; "
            "}";

        button->setStyleSheet(pressedStyle);

        // Restore original style after duration
        QTimer::singleShot(durationMs, this, [button, this, keyCode]() {
            // Check if it's a modifier button
            bool isModifier = button->property("isModifier").toBool();
            if (isModifier) {
                button->setStyleSheet(modifierKeyStyle);
            } else {
                button->setStyleSheet(buttonStyle);
            }
        });
    } else if (shiftedButton) {
        // For ShiftedKeyButton, we need to access the internal button
        // We can use findChild to get the QPushButton inside
        QPushButton* internalButton = shiftedButton->findChild<QPushButton*>();
        if (internalButton) {
            QString pressedStyle =
                "QPushButton { "
                "   border: 2px solid #4a90d9; border-radius: 4px; "
                "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                "       stop:0 #d0e8ff, stop:0.5 #a0c8ff, stop:1 #80b0ff); "
                "   color: transparent; padding: 4px 6px; margin: 1px; "
                "   font-size: 11px; font-weight: 500; "
                "}";

            internalButton->setStyleSheet(pressedStyle);

            // Restore original style after duration
            QTimer::singleShot(durationMs, this, [internalButton]() {
                QString originalStyle =
                    "QPushButton { "
                    "   border: 1px solid #999; border-radius: 4px; "
                    "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                    "       stop:0 #ffffff, stop:0.5 #f5f5f5, stop:1 #e8e8e8); "
                    "   color: transparent; padding: 4px 6px; margin: 1px; "
                    "   font-size: 11px; font-weight: 500; "
                    "} "
                    "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f0f0f0, stop:0.5 #e8e8e8, stop:1 #d8d8d8); } "
                    "QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #d0d0d0, stop:0.5 #c0c0c0, stop:1 #b0b0b0); }";
                internalButton->setStyleSheet(originalStyle);
            });
        }
    }
}

QWidget* VirtualKeyboardDialog::findWidgetByKeyCode(int keyCode)
{
    return keyCodeToWidgetMap.value(keyCode, nullptr);
}

void VirtualKeyboardDialog::registerShiftedKeyButton(ShiftedKeyButton* button)
{
    if (button) {
        int keyCode = button->property("keyCode").toInt();
        if (keyCode) {
            keyCodeToWidgetMap[keyCode] = button;
        }
    }
}

static void setKeyWidth(QPushButton* btn, int baseWidth, double factor)
{
    int w = static_cast<int>(baseWidth * factor);
    btn->setMinimumWidth(w);
    btn->setMaximumWidth(w);
    btn->setFixedHeight(40);
}

static void setKeyWidth(QWidget* widget, int baseWidth, double factor)
{
    int w = static_cast<int>(baseWidth * factor);
    widget->setMinimumWidth(w);
    widget->setMaximumWidth(w);
}

// Helper: add a fixed-width spacer to a QHBoxLayout
static void addSpacer(QHBoxLayout* layout, int pixelWidth)
{
    QSpacerItem* spacer = new QSpacerItem(pixelWidth, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    layout->addItem(spacer);
}

void VirtualKeyboardDialog::addFunctionKeyRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    QPushButton* escBtn = createKeyButton("Esc", Qt::Key_Escape);
    setKeyWidth(escBtn, baseWidth, 1.0);
    row->addWidget(escBtn);

    for (int i = 0; i < 4; i++) {
        QPushButton* fBtn = createKeyButton(QString("F%1").arg(i + 1), Qt::Key_F1 + i);
        setKeyWidth(fBtn, baseWidth, 1.0);
        row->addWidget(fBtn);
    }

    for (int i = 0; i < 4; i++) {
        QPushButton* fBtn = createKeyButton(QString("F%1").arg(i + 5), Qt::Key_F5 + i);
        setKeyWidth(fBtn, baseWidth, 1.0);
        row->addWidget(fBtn);
    }

    for (int i = 0; i < 4; i++) {
        QPushButton* fBtn = createKeyButton(QString("F%1").arg(i + 9), Qt::Key_F9 + i);
        setKeyWidth(fBtn, baseWidth, 1.0);
        row->addWidget(fBtn);
    }

    QPushButton* delBtn = createKeyButton("Del", Qt::Key_Delete);
    setKeyWidth(delBtn, baseWidth, 1.0);
    row->addWidget(delBtn);

    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::addNumberRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    addSpacer(row, baseWidth * 0.05);

    ShiftedKeyButton* graveBtn = new ShiftedKeyButton("`", "~", Qt::Key_QuoteLeft, this);
    graveBtn->setFixedHeight(40);
    graveBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(graveBtn, baseWidth, 1.0);
    connect(graveBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(graveBtn);
    row->addWidget(graveBtn);

    for (int i = 0; i < 9; i++) {
        int keyCode = Qt::Key_1 + i;
        QString baseChar = QString::number(i + 1);
        QString shiftedChars[] = {"!", "@", "#", "$", "%", "^", "&", "*", "("};
        QString shiftedChar = shiftedChars[i];
        ShiftedKeyButton* numBtn = new ShiftedKeyButton(baseChar, shiftedChar, keyCode, this);
        numBtn->setFixedHeight(40);
        numBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setKeyWidth(numBtn, baseWidth, 1.0);
        connect(numBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
        registerShiftedKeyButton(numBtn);
        row->addWidget(numBtn);
    }

    ShiftedKeyButton* zeroBtn = new ShiftedKeyButton("0", ")", Qt::Key_0, this);
    zeroBtn->setFixedHeight(40);
    zeroBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(zeroBtn, baseWidth, 1.0);
    connect(zeroBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(zeroBtn);
    row->addWidget(zeroBtn);

    ShiftedKeyButton* minusBtn = new ShiftedKeyButton("-", "_", Qt::Key_Minus, this);
    minusBtn->setFixedHeight(40);
    minusBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(minusBtn, baseWidth, 1.0);
    connect(minusBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(minusBtn);
    row->addWidget(minusBtn);

    ShiftedKeyButton* equalBtn = new ShiftedKeyButton("=", "+", Qt::Key_Equal, this);
    equalBtn->setFixedHeight(40);
    equalBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(equalBtn, baseWidth, 1.0);
    connect(equalBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(equalBtn);
    row->addWidget(equalBtn);

    QPushButton* backspaceBtn = createKeyButton("Backspace", Qt::Key_Backspace);
    backspaceBtn->setStyleSheet(buttonStyle + " QPushButton { font-size: 9px; }");
    setKeyWidth(backspaceBtn, baseWidth, 2.0);
    row->addWidget(backspaceBtn);

    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::addTabRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    addSpacer(row, baseWidth * 0.1);

    // Get current layout config
    KeyboardLayoutConfig config = KeyboardLayoutManager::getInstance().getLayout(m_currentLayoutName);
    bool useLayout = !config.name.isEmpty();

    QPushButton* tabBtn = createKeyButton("Tab", Qt::Key_Tab);
    setKeyWidth(tabBtn, baseWidth, 1.5);
    row->addWidget(tabBtn);

    // For QWERTY layout, use hardcoded keys; for other layouts, use dynamic mapping
    if (useLayout && m_currentLayoutName != "US QWERTY") {
        // Dynamic layout: for each physical key position, find what character it produces
        // Physical key positions: Q, W, E, R, T, Y, U, I, O, P
        QList<int> physicalKeyCodes = {
            Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
            Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P
        };

        for (int keyCode : physicalKeyCodes) {
            // Find what character this key produces in the current layout
            QString displayChar = findCharForQtKey(keyCode, config, false);
            qCInfo(log_virtual_keyboard) << "Key" << keyCode << "-> displayChar:" << displayChar << "unicode:" << (displayChar.isEmpty() ? -1 : displayChar[0].unicode());
            if (displayChar.isEmpty()) {
                // Fallback to default QWERTY character
                displayChar = QChar('A' + (keyCode - Qt::Key_A)).toLower();
            }

            // Don't call toUpper() on Japanese characters - they don't have case
            QString buttonText = displayChar;
            if (displayChar.length() == 1 && displayChar[0].isLetter() && displayChar[0].isLower()) {
                buttonText = displayChar.toUpper();
            }

            QPushButton* keyBtn = createKeyButton(buttonText, keyCode);
            setKeyWidth(keyBtn, baseWidth, 1.0);
            row->addWidget(keyBtn);
        }
    } else {
        // Standard QWERTY layout
        QString keys = "QWERTYUIOP";
        for (int i = 0; i < keys.length(); i++) {
            QChar key = keys[i];
            int keyCode = Qt::Key_A + (key.toLatin1() - 'A');
            QPushButton* keyBtn = createKeyButton(key, keyCode);
            setKeyWidth(keyBtn, baseWidth, 1.0);
            row->addWidget(keyBtn);
        }
    }

    ShiftedKeyButton* leftBracketBtn = new ShiftedKeyButton("[", "{", Qt::Key_BracketLeft, this);
    leftBracketBtn->setFixedHeight(40);
    leftBracketBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(leftBracketBtn, baseWidth, 1.0);
    connect(leftBracketBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(leftBracketBtn);
    row->addWidget(leftBracketBtn);

    ShiftedKeyButton* rightBracketBtn = new ShiftedKeyButton("]", "}", Qt::Key_BracketRight, this);
    rightBracketBtn->setFixedHeight(40);
    rightBracketBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(rightBracketBtn, baseWidth, 1.0);
    connect(rightBracketBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(rightBracketBtn);
    row->addWidget(rightBracketBtn);

    ShiftedKeyButton* backslashBtn = new ShiftedKeyButton("\\", "|", Qt::Key_Backslash, this);
    backslashBtn->setFixedHeight(40);
    backslashBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(backslashBtn, baseWidth, 1.5);
    connect(backslashBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(backslashBtn);
    row->addWidget(backslashBtn);

    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::addCapsRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    addSpacer(row, baseWidth * 0.15);

    QPushButton* capsBtn = createKeyButton("Caps Lock", Qt::Key_CapsLock);
    capsBtn->setStyleSheet(buttonStyle + " QPushButton { font-size: 9px; }");
    setKeyWidth(capsBtn, baseWidth, 1.75);
    row->addWidget(capsBtn);

    // Get current layout config
    KeyboardLayoutConfig config = KeyboardLayoutManager::getInstance().getLayout(m_currentLayoutName);
    bool useLayout = !config.name.isEmpty();

    // For QWERTY layout, use hardcoded keys; for other layouts, use dynamic mapping
    if (useLayout && m_currentLayoutName != "US QWERTY") {
        // Dynamic layout: for each physical key position, find what character it produces
        // Physical key positions: A, S, D, F, G, H, J, K, L
        QList<int> physicalKeyCodes = {
            Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
            Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L
        };

        for (int keyCode : physicalKeyCodes) {
            // Find what character this key produces in the current layout
            QString displayChar = findCharForQtKey(keyCode, config, false);
            qCInfo(log_virtual_keyboard) << "Key" << keyCode << "-> displayChar:" << displayChar << "unicode:" << (displayChar.isEmpty() ? -1 : displayChar[0].unicode());
            if (displayChar.isEmpty()) {
                // Fallback to default QWERTY character
                displayChar = QChar('A' + (keyCode - Qt::Key_A)).toLower();
            }

            // Don't call toUpper() on Japanese characters - they don't have case
            QString buttonText = displayChar;
            if (displayChar.length() == 1 && displayChar[0].isLetter() && displayChar[0].isLower()) {
                buttonText = displayChar.toUpper();
            }

            QPushButton* keyBtn = createKeyButton(buttonText, keyCode);
            setKeyWidth(keyBtn, baseWidth, 1.0);
            row->addWidget(keyBtn);
        }
    } else {
        // Standard QWERTY layout
        QString keys = "ASDFGHJKL";
        for (int i = 0; i < keys.length(); i++) {
            QChar key = keys[i];
            int keyCode = Qt::Key_A + (key.toLatin1() - 'A');
            QPushButton* keyBtn = createKeyButton(key, keyCode);
            setKeyWidth(keyBtn, baseWidth, 1.0);
            row->addWidget(keyBtn);
        }
    }

    ShiftedKeyButton* semicolonBtn = new ShiftedKeyButton(";", ":", Qt::Key_Semicolon, this);
    semicolonBtn->setFixedHeight(40);
    semicolonBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(semicolonBtn, baseWidth, 1.0);
    connect(semicolonBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(semicolonBtn);
    row->addWidget(semicolonBtn);

    ShiftedKeyButton* apostropheBtn = new ShiftedKeyButton("'", "\"", Qt::Key_Apostrophe, this);
    apostropheBtn->setFixedHeight(40);
    apostropheBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(apostropheBtn, baseWidth, 1.0);
    connect(apostropheBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(apostropheBtn);
    row->addWidget(apostropheBtn);

    // Rectangle Enter key (ANSI layout)
    QPushButton* enterBtn = createKeyButton("Enter", Qt::Key_Return);
    setKeyWidth(enterBtn, baseWidth, 2.25);
    row->addWidget(enterBtn);

    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::addShiftRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    addSpacer(row, baseWidth * 0.175);

    QPushButton* leftShiftBtn = createKeyButton("Shift", Qt::Key_Shift, true);
    leftShiftBtn->setCheckable(true);
    modifierButtons[leftShiftBtn] = Qt::ShiftModifier;
    connect(leftShiftBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(leftShiftBtn, baseWidth, 2.25);
    row->addWidget(leftShiftBtn);

    // Get current layout config
    KeyboardLayoutConfig config = KeyboardLayoutManager::getInstance().getLayout(m_currentLayoutName);
    bool useLayout = !config.name.isEmpty();

    // For QWERTY layout, use hardcoded keys; for other layouts, use dynamic mapping
    if (useLayout && m_currentLayoutName != "US QWERTY") {
        // Dynamic layout: for each physical key position, find what character it produces
        // Physical key positions: Z, X, C, V, B, N, M
        QList<int> physicalKeyCodes = {
            Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B,
            Qt::Key_N, Qt::Key_M
        };

        for (int keyCode : physicalKeyCodes) {
            // Find what character this key produces in the current layout
            QString displayChar = findCharForQtKey(keyCode, config, false);
            qCInfo(log_virtual_keyboard) << "Key" << keyCode << "-> displayChar:" << displayChar << "unicode:" << (displayChar.isEmpty() ? -1 : displayChar[0].unicode());
            if (displayChar.isEmpty()) {
                // Fallback to default QWERTY character
                displayChar = QChar('A' + (keyCode - Qt::Key_A)).toLower();
            }

            // Don't call toUpper() on Japanese characters - they don't have case
            QString buttonText = displayChar;
            if (displayChar.length() == 1 && displayChar[0].isLetter() && displayChar[0].isLower()) {
                buttonText = displayChar.toUpper();
            }

            QPushButton* keyBtn = createKeyButton(buttonText, keyCode);
            setKeyWidth(keyBtn, baseWidth, 1.0);
            row->addWidget(keyBtn);
        }
    } else {
        // Standard QWERTY layout
        QString keys = "ZXCVBNM";
        for (int i = 0; i < keys.length(); i++) {
            QChar key = keys[i];
            int keyCode = Qt::Key_A + (key.toLatin1() - 'A');
            QPushButton* keyBtn = createKeyButton(key, keyCode);
            setKeyWidth(keyBtn, baseWidth, 1.0);
            row->addWidget(keyBtn);
        }
    }

    ShiftedKeyButton* commaBtn = new ShiftedKeyButton(",", "<", Qt::Key_Comma, this);
    commaBtn->setFixedHeight(40);
    commaBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(commaBtn, baseWidth, 1.0);
    connect(commaBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(commaBtn);
    row->addWidget(commaBtn);

    ShiftedKeyButton* periodBtn = new ShiftedKeyButton(".", ">", Qt::Key_Period, this);
    periodBtn->setFixedHeight(40);
    periodBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(periodBtn, baseWidth, 1.0);
    connect(periodBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(periodBtn);
    row->addWidget(periodBtn);

    ShiftedKeyButton* slashBtn = new ShiftedKeyButton("/", "?", Qt::Key_Slash, this);
    slashBtn->setFixedHeight(40);
    slashBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setKeyWidth(slashBtn, baseWidth, 1.0);
    connect(slashBtn, &ShiftedKeyButton::clicked, this, &VirtualKeyboardDialog::onKeyClicked);
    registerShiftedKeyButton(slashBtn);
    row->addWidget(slashBtn);

    QPushButton* rightShiftBtn = createKeyButton("Shift", Qt::Key_Shift, true);
    rightShiftBtn->setCheckable(true);
    modifierButtons[rightShiftBtn] = Qt::ShiftModifier;
    connect(rightShiftBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(rightShiftBtn, baseWidth, 2.75);
    row->addWidget(rightShiftBtn);

    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::addNavigationRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    addSpacer(row, baseWidth * 2.0);

    QPushButton* insertBtn = createKeyButton("Insert", Qt::Key_Insert);
    setKeyWidth(insertBtn, baseWidth, 1.0);
    row->addWidget(insertBtn);

    QPushButton* homeBtn = createKeyButton("Home", Qt::Key_Home);
    setKeyWidth(homeBtn, baseWidth, 1.0);
    row->addWidget(homeBtn);

    QPushButton* pageUpBtn = createKeyButton("PgUp", Qt::Key_PageUp);
    setKeyWidth(pageUpBtn, baseWidth, 1.0);
    row->addWidget(pageUpBtn);

    QPushButton* delBtn = createKeyButton("Del", Qt::Key_Delete);
    setKeyWidth(delBtn, baseWidth, 1.0);
    row->addWidget(delBtn);

    QPushButton* endBtn = createKeyButton("End", Qt::Key_End);
    setKeyWidth(endBtn, baseWidth, 1.0);
    row->addWidget(endBtn);

    QPushButton* pageDownBtn = createKeyButton("PgDn", Qt::Key_PageDown);
    setKeyWidth(pageDownBtn, baseWidth, 1.0);
    row->addWidget(pageDownBtn);

    addSpacer(row, baseWidth * 3.0);

    QPushButton* upBtn = createKeyButton("Up", Qt::Key_Up);
    setKeyWidth(upBtn, baseWidth, 1.0);
    row->addWidget(upBtn);

    QPushButton* leftBtn = createKeyButton("Left", Qt::Key_Left);
    setKeyWidth(leftBtn, baseWidth, 1.0);
    row->addWidget(leftBtn);

    QPushButton* downBtn = createKeyButton("Down", Qt::Key_Down);
    setKeyWidth(downBtn, baseWidth, 1.0);
    row->addWidget(downBtn);

    QPushButton* rightBtn = createKeyButton("Right", Qt::Key_Right);
    setKeyWidth(rightBtn, baseWidth, 1.0);
    row->addWidget(rightBtn);

    row->addStretch();
    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::addBottomRow(QVBoxLayout* rowsLayout)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setSpacing(2);
    row->setContentsMargins(0, 0, 0, 0);

    int baseWidth = 36;

    addSpacer(row, baseWidth * 0.2);

    QPushButton* leftCtrlBtn = createKeyButton("Ctrl", Qt::Key_Control, true);
    leftCtrlBtn->setCheckable(true);
    modifierButtons[leftCtrlBtn] = Qt::ControlModifier;
    connect(leftCtrlBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(leftCtrlBtn, baseWidth, 1.25);
    row->addWidget(leftCtrlBtn);

    QPushButton* leftWinBtn = createKeyButton("Win", Qt::Key_Meta, true);
    leftWinBtn->setCheckable(true);
    modifierButtons[leftWinBtn] = Qt::MetaModifier;
    connect(leftWinBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(leftWinBtn, baseWidth, 1.25);
    row->addWidget(leftWinBtn);

    QPushButton* leftAltBtn = createKeyButton("Alt", Qt::Key_Alt, true);
    leftAltBtn->setCheckable(true);
    modifierButtons[leftAltBtn] = Qt::AltModifier;
    connect(leftAltBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(leftAltBtn, baseWidth, 1.25);
    row->addWidget(leftAltBtn);

    QPushButton* spaceBtn = createKeyButton("Space", Qt::Key_Space);
    setKeyWidth(spaceBtn, baseWidth, 6.25);
    row->addWidget(spaceBtn);

    QPushButton* rightAltBtn = createKeyButton("Alt", Qt::Key_Alt, true);
    rightAltBtn->setCheckable(true);
    modifierButtons[rightAltBtn] = Qt::AltModifier;
    connect(rightAltBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(rightAltBtn, baseWidth, 1.25);
    row->addWidget(rightAltBtn);

    QPushButton* rightWinBtn = createKeyButton("Win", Qt::Key_Meta, true);
    rightWinBtn->setCheckable(true);
    modifierButtons[rightWinBtn] = Qt::MetaModifier;
    connect(rightWinBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(rightWinBtn, baseWidth, 1.25);
    row->addWidget(rightWinBtn);

    QPushButton* menuBtn = createKeyButton("Menu", Qt::Key_Menu, true);
    setKeyWidth(menuBtn, baseWidth, 1.25);
    row->addWidget(menuBtn);

    QPushButton* rightCtrlBtn = createKeyButton("Ctrl", Qt::Key_Control, true);
    rightCtrlBtn->setCheckable(true);
    modifierButtons[rightCtrlBtn] = Qt::ControlModifier;
    connect(rightCtrlBtn, &QPushButton::toggled, this, &VirtualKeyboardDialog::onModifierToggled);
    setKeyWidth(rightCtrlBtn, baseWidth, 1.25);
    row->addWidget(rightCtrlBtn);

    rowsLayout->addLayout(row);
}

void VirtualKeyboardDialog::onKeyClicked()
{
    QObject* senderObj = sender();
    int keyCode = 0;

    if (QPushButton* button = qobject_cast<QPushButton*>(senderObj)) {
        keyCode = button->property("keyCode").toInt();
    } else if (ShiftedKeyButton* shiftedButton = qobject_cast<ShiftedKeyButton*>(senderObj)) {
        keyCode = shiftedButton->property("keyCode").toInt();
    }

    if (!keyCode) return;

    // Show visual feedback
    showKeyPressFeedback(keyCode);

    if (keyCode == Qt::Key_Delete &&
        (currentModifiers & Qt::ControlModifier) &&
        (currentModifiers & Qt::AltModifier)) {
        HostManager::getInstance().sendCtrlAltDel();
        for (auto it = modifierButtons.begin(); it != modifierButtons.end(); ++it) {
            it.key()->setChecked(false);
        }
        currentModifiers = 0;
        return;
    }

    HostManager::getInstance().handleFunctionKey(keyCode, currentModifiers);

    for (auto it = modifierButtons.begin(); it != modifierButtons.end(); ++it) {
        if (it.value() == Qt::ShiftModifier && it.key()->isChecked()) {
            it.key()->setChecked(false);
        }
    }
    currentModifiers &= ~Qt::ShiftModifier;
}

void VirtualKeyboardDialog::onModifierToggled(bool checked)
{
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    int modifier = modifierButtons.value(button, 0);
    if (modifier == 0) return;

    if (checked) {
        currentModifiers |= modifier;
    } else {
        currentModifiers &= ~modifier;
    }

    for (auto it = modifierButtons.begin(); it != modifierButtons.end(); ++it) {
        if (it.value() == modifier && it.key() != button) {
            it.key()->setChecked(checked);
        }
    }
}

void VirtualKeyboardDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void VirtualKeyboardDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void VirtualKeyboardDialog::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}
