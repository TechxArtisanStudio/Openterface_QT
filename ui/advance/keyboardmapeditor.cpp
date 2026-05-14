#include "keyboardmapeditor.h"
#include "serial/SerialPortManager.h"
#include "serial/ch9329.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QDateTime>
#include <QTimer>

Q_LOGGING_CATEGORY(log_keyboard_editor, "opf.ui.keyboardeditor")

// ===== KeyTestWidget Implementation =====

KeyTestWidget::KeyTestWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    m_promptLabel = new QLabel("Click here and press any key to test", this);
    m_promptLabel->setStyleSheet("QLabel { font-weight: bold; color: #0066cc; }");
    m_promptLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_promptLabel);
    
    // Use a QLabel as the key capture area instead of QLineEdit
    m_statusLabel = new QLabel("Ready to capture keys...", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setMinimumHeight(60);
    m_statusLabel->setStyleSheet(
        "QLabel {"
        "   background-color: #f0f0f0;"
        "   border: 2px solid #3498db;"
        "   border-radius: 5px;"
        "   padding: 10px;"
        "   font-size: 14pt;"
        "}"
    );
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);
    
    setLayout(layout);
    setMinimumHeight(120);
    
    // Make the widget itself focusable to capture keyboard events
    setFocusPolicy(Qt::StrongFocus);
    
    // Set a visual style to indicate focus
    setStyleSheet(
        "KeyTestWidget:focus {"
        "   background-color: #e8f4f8;"
        "   border: 2px solid #2980b9;"
        "}"
        "KeyTestWidget {"
        "   background-color: #ffffff;"
        "   border: 2px solid #cccccc;"
        "   border-radius: 5px;"
        "   padding: 5px;"
        "}"
    );
}

void KeyTestWidget::setCurrentLayout(const KeyboardLayoutConfig& layout)
{
    m_layout = layout;
}

void KeyTestWidget::keyPressEvent(QKeyEvent *event)
{
    // Ignore auto-repeat events
    if (event->isAutoRepeat()) {
        return;
    }
    
    int qtKey = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();
    QString keyText = event->text();
    
    qCDebug(log_keyboard_editor) << "=== Key Press Detected ===" 
                                  << "QtKey:" << qtKey 
                                  << "Text:" << keyText 
                                  << "Mods:" << mods;
    
    // Prevent modifier-only keys from being captured
    if (qtKey == Qt::Key_Shift || qtKey == Qt::Key_Control || 
        qtKey == Qt::Key_Alt || qtKey == Qt::Key_Meta) {
        m_statusLabel->setText("(Modifier key ignored - press a regular key)");
        return;
    }
    
    // Update status label to show the captured key
    QString displayText = keyText.isEmpty() ? 
        QKeySequence(qtKey).toString() : 
        QString("'%1'").arg(keyText);
    m_statusLabel->setText(QString("Captured: %1").arg(displayText));
    
    // Find HID scancode from current layout
    uint8_t currentHID = m_layout.keyMap.value(qtKey, 0);
    
    qCDebug(log_keyboard_editor) << "Direct keyMap lookup: Qt key 0x" << QString::number(qtKey, 16) 
                                 << "-> HID 0x" << QString::number(currentHID, 16);
    
    // If not found directly, try to find through character mapping
    if (currentHID == 0 && !keyText.isEmpty() && keyText.length() > 0) {
        QChar ch = keyText[0];
        uint16_t charCode = ch.unicode();
        
        qCDebug(log_keyboard_editor) << "Trying character mapping for:" << ch 
                                     << "Unicode: 0x" << QString::number(charCode, 16);
        
        // Check if this character has a mapping in the layout
        // Note: charMapping uses uint8_t key, so only works for ASCII/Latin-1 range
        if (charCode < 256) {
            uint8_t byteCode = static_cast<uint8_t>(charCode);
            qCDebug(log_keyboard_editor) << "Looking up byteCode:" << byteCode 
                                        << "in charMapping (size:" << m_layout.charMapping.size() << ")";
            
            if (m_layout.charMapping.contains(byteCode)) {
                int mappedQtKey = m_layout.charMapping.value(byteCode);
                qCDebug(log_keyboard_editor) << "charMapping[" << byteCode << "] = Qt key 0x" 
                                            << QString::number(mappedQtKey, 16);
                
                currentHID = m_layout.keyMap.value(mappedQtKey, 0);
                qCDebug(log_keyboard_editor) << "keyMap[0x" << QString::number(mappedQtKey, 16) 
                                            << "] = HID 0x" << QString::number(currentHID, 16);
                
                if (currentHID != 0) {
                    qCDebug(log_keyboard_editor) << "Found HID via character mapping:" 
                                                << "char=" << ch 
                                                << "(0x" << QString::number(charCode, 16) << ")"
                                                << "-> Qt key=0x" << QString::number(mappedQtKey, 16)
                                                << "-> HID=0x" << QString::number(currentHID, 16);
                }
            } else {
                qCDebug(log_keyboard_editor) << "Character" << ch << "not found in charMapping";
                
                // Debug: print first 10 entries of charMapping
                qCDebug(log_keyboard_editor) << "First 10 charMapping entries:";
                int count = 0;
                for (auto it = m_layout.charMapping.begin(); it != m_layout.charMapping.end() && count < 10; ++it, ++count) {
                    qCDebug(log_keyboard_editor) << "  charMapping[" << it.key() 
                                                << "] (char:" << QChar(it.key()) << ") = Qt key 0x" 
                                                << QString::number(it.value(), 16);
                }
            }
        } else {
            qCDebug(log_keyboard_editor) << "Character Unicode value" << charCode << "out of uint8_t range";
        }
    }
    
    // Build detailed information
    QString info = QString(
        "Key: %1\n"
        "Char: '%2' (Unicode: U+%3)\n"
        "Qt Key: 0x%4\n"
        "Modifiers: %5\n"
        "Current HID: 0x%6 (%7)\n"
        "Physical Key: %8"
    ).arg(QKeySequence(qtKey).toString())
     .arg(keyText.isEmpty() ? "(none)" : keyText)
     .arg(keyText.isEmpty() ? QString("0000") : QString::number(keyText[0].unicode(), 16).toUpper())
     .arg(qtKey, 0, 16)
     .arg(formatModifiers(mods))
     .arg(currentHID, 2, 16, QChar('0'))
     .arg(currentHID == 0 ? "Unknown (0x00)" : HIDScancode::describe(currentHID))
     .arg(describePhysicalKey(qtKey));
    
    qCDebug(log_keyboard_editor) << "Emitting keyDetected signal";
    emit keyDetected(qtKey, mods, keyText, currentHID, info);
    
    // Accept the event to prevent further propagation
    event->accept();
}

void KeyTestWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    m_statusLabel->setText("Ready - press any key...");
    qCDebug(log_keyboard_editor) << "KeyTestWidget gained focus";
}

void KeyTestWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    m_statusLabel->setText("Click to activate...");
    qCDebug(log_keyboard_editor) << "KeyTestWidget lost focus";
}

void KeyTestWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    QWidget::mousePressEvent(event);
}

QString KeyTestWidget::formatModifiers(Qt::KeyboardModifiers mods)
{
    QStringList modList;
    if (mods & Qt::ShiftModifier) modList << "Shift";
    if (mods & Qt::ControlModifier) modList << "Ctrl";
    if (mods & Qt::AltModifier) modList << "Alt";
    if (mods & Qt::MetaModifier) modList << "Meta";
    
    return modList.isEmpty() ? "None" : modList.join(" + ");
}

QString KeyTestWidget::describePhysicalKey(int qtKey)
{
    // Map Qt key to physical key description
    static QMap<int, QString> physicalKeys = {
        {Qt::Key_A, "A key"}, {Qt::Key_B, "B key"}, {Qt::Key_C, "C key"},
        {Qt::Key_1, "Number 1 key"}, {Qt::Key_2, "Number 2 key"}, {Qt::Key_3, "Number 3 key"},
        {Qt::Key_Minus, "Minus key"}, {Qt::Key_Equal, "Equal key"},
        {Qt::Key_BracketLeft, "Left bracket key"}, {Qt::Key_BracketRight, "Right bracket key"},
        {Qt::Key_Semicolon, "Semicolon key"}, {Qt::Key_Apostrophe, "Quote key"},
        {Qt::Key_Comma, "Comma key"}, {Qt::Key_Period, "Period key"}, {Qt::Key_Slash, "Slash key"}
    };
    
    return physicalKeys.value(qtKey, QKeySequence(qtKey).toString() + " key");
}

// ===== KeyMappingCorrection Implementation =====

QString KeyMappingCorrection::description() const
{
    return QString("%1 (Qt:0x%2) %3 -> HID:0x%4 (was 0x%5)")
        .arg(displayChar)
        .arg(qtKey, 0, 16)
        .arg(physicalKeyName)
        .arg(correctedHID, 2, 16, QChar('0'))
        .arg(originalHID, 2, 16, QChar('0'));
}

QString KeyMappingCorrection::statusString() const
{
    switch (status) {
        case Untested: return "Untested";
        case TestedCorrect: return "✓ Correct";
        case TestedWrong: return "✗ Wrong";
        case Modified: return "★ Fixed";
        default: return "?";
    }
}

// ===== KeyboardMapEditor Implementation =====

KeyboardMapEditor::KeyboardMapEditor(QWidget *parent) :
    QDialog(parent),
    m_currentQtKey(0),
    m_currentHID(0)
{
    setupUI();
    setupConnections();
    loadBaseLayouts();
    
    setWindowTitle("Keyboard Mapping Correction Tool");
    resize(900, 700);
}

KeyboardMapEditor::~KeyboardMapEditor()
{
    // Cleanup handled automatically by Qt parent-child relationships
}

void KeyboardMapEditor::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // === Top section: Base layout and custom name ===
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->addWidget(new QLabel("Base Layout:", this));
    
    m_baseLayoutCombo = new QComboBox(this);
    topLayout->addWidget(m_baseLayoutCombo);
    
    topLayout->addWidget(new QLabel("Custom Name:", this));
    
    m_customNameEdit = new QLineEdit(this);
    m_customNameEdit->setPlaceholderText("e.g., My_US_QWERTY");
    topLayout->addWidget(m_customNameEdit);
    
    mainLayout->addLayout(topLayout);
    
    // === Key test section ===
    QGroupBox* testGroup = new QGroupBox("1. Key Test Area", this);
    QVBoxLayout* testLayout = new QVBoxLayout();
    
    m_testWidget = new KeyTestWidget(this);
    testLayout->addWidget(m_testWidget);
    
    m_detectionInfo = new QTextEdit(this);
    m_detectionInfo->setReadOnly(true);
    m_detectionInfo->setMaximumHeight(120);
    m_detectionInfo->setPlaceholderText("Key detection info will appear here...");
    testLayout->addWidget(new QLabel("Detection Results:", this));
    testLayout->addWidget(m_detectionInfo);
    
    testGroup->setLayout(testLayout);
    mainLayout->addWidget(testGroup);
    
    // === Target verification section ===
    QGroupBox* verifyGroup = new QGroupBox("2. Target Device Verification & Correction", this);
    QVBoxLayout* verifyLayout = new QVBoxLayout();
    
    QHBoxLayout* targetLayout = new QHBoxLayout();
    targetLayout->addWidget(new QLabel("Target device shows:", this));
    m_targetCharEdit = new QLineEdit(this);
    m_targetCharEdit->setPlaceholderText("Enter char displayed on target");
    m_targetCharEdit->setMaxLength(5);
    targetLayout->addWidget(m_targetCharEdit);
    
    targetLayout->addWidget(new QLabel("Corrected HID:", this));
    m_correctedHIDEdit = new QLineEdit(this);
    m_correctedHIDEdit->setPlaceholderText("0x1F or auto");
    targetLayout->addWidget(m_correctedHIDEdit);
    
    m_sendToTargetButton = new QPushButton("🔄 Send to Target", this);
    m_sendToTargetButton->setEnabled(false);
    m_sendToTargetButton->setToolTip("Send the captured key to target device for testing");
    targetLayout->addWidget(m_sendToTargetButton);
    
    m_addButton = new QPushButton("Add Correction", this);
    m_addButton->setEnabled(false);
    targetLayout->addWidget(m_addButton);
    
    verifyLayout->addLayout(targetLayout);
    verifyGroup->setLayout(verifyLayout);
    mainLayout->addWidget(verifyGroup);
    
    // === Corrections table ===
    QGroupBox* tableGroup = new QGroupBox("Corrected Mappings", this);
    QVBoxLayout* tableLayout = new QVBoxLayout();
    
    m_correctionTable = new QTableWidget(this);
    m_correctionTable->setColumnCount(6);
    m_correctionTable->setHorizontalHeaderLabels({
        "Char", "Qt Key", "Original HID", "New HID", "Status", "Action"
    });
    
    m_correctionTable->horizontalHeader()->setStretchLastSection(false);
    m_correctionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_correctionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_correctionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    
    tableLayout->addWidget(m_correctionTable);
    
    m_removeButton = new QPushButton("Remove Selected", this);
    m_removeButton->setEnabled(false);
    tableLayout->addWidget(m_removeButton);
    
    tableGroup->setLayout(tableLayout);
    mainLayout->addWidget(tableGroup);
    
    // === Bottom buttons ===
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_saveButton = new QPushButton("Save Layout", this);
    m_testButton = new QPushButton("Test Layout", this);
    m_exportButton = new QPushButton("Export JSON", this);
    m_importButton = new QPushButton("Import JSON", this);
    QPushButton* cancelButton = new QPushButton("Cancel", this);
    
    buttonLayout->addWidget(m_testButton);
    buttonLayout->addWidget(m_exportButton);
    buttonLayout->addWidget(m_importButton);
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    setLayout(mainLayout);
}

void KeyboardMapEditor::setupConnections()
{
    connect(m_baseLayoutCombo, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, &KeyboardMapEditor::onBaseLayoutChanged);
    
    connect(m_testWidget, &KeyTestWidget::keyDetected,
            this, &KeyboardMapEditor::onKeyDetected);
    
    connect(m_targetCharEdit, &QLineEdit::textChanged,
            this, &KeyboardMapEditor::onTargetCharEntered);
    
    connect(m_sendToTargetButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onSendToTarget);
    
    connect(m_addButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onAddCorrection);
    
    connect(m_removeButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onRemoveCorrection);
    
    connect(m_saveButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onSaveLayout);
    
    connect(m_testButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onTestLayout);
    
    connect(m_exportButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onExportJson);
    
    connect(m_importButton, &QPushButton::clicked,
            this, &KeyboardMapEditor::onImportJson);
    
    connect(m_correctionTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_removeButton->setEnabled(m_correctionTable->currentRow() >= 0);
    });
}

void KeyboardMapEditor::loadBaseLayouts()
{
    m_baseLayoutCombo->clear();
    
    QStringList layouts = KeyboardLayoutManager::getInstance().getAvailableLayouts();
    m_baseLayoutCombo->addItems(layouts);
    
    // Select US layout by default if available
    int usIndex = m_baseLayoutCombo->findText("US");
    if (usIndex >= 0) {
        m_baseLayoutCombo->setCurrentIndex(usIndex);
    }
}

void KeyboardMapEditor::onBaseLayoutChanged(const QString& layoutName)
{
    m_baseLayout = KeyboardLayoutManager::getInstance().getLayout(layoutName);
    m_testWidget->setCurrentLayout(m_baseLayout);
    m_corrections.clear();
    updateCorrectionTable();
    
    qCDebug(log_keyboard_editor) << "Base layout changed to:" << layoutName;
    qCDebug(log_keyboard_editor) << "Layout has" << m_baseLayout.keyMap.size() << "keyMap entries";
    qCDebug(log_keyboard_editor) << "Layout has" << m_baseLayout.charMapping.size() << "charMapping entries";
    
    // Debug: Check | character mapping
    if (m_baseLayout.charMapping.contains(0x7C)) {
        int qtKey = m_baseLayout.charMapping.value(0x7C);
        uint8_t hid = m_baseLayout.keyMap.value(qtKey, 0);
        qCWarning(log_keyboard_editor) << "***** LAYOUT VERIFICATION: '|' character *****";
        qCWarning(log_keyboard_editor) << "  charMapping[0x7C] = Qt key 0x" << QString::number(qtKey, 16);
        qCWarning(log_keyboard_editor) << "  keyMap[0x" << QString::number(qtKey, 16) << "] = HID 0x" << QString::number(hid, 16);
    } else {
        qCWarning(log_keyboard_editor) << "***** WARNING: '|' (0x7C) NOT in charMapping! *****";
    }
}

void KeyboardMapEditor::onKeyDetected(int qtKey, Qt::KeyboardModifiers modifiers, 
                                     QString keyText, uint8_t currentHID, QString info)
{
    m_currentQtKey = qtKey;
    m_currentModifiers = modifiers;
    m_currentKeyText = keyText;
    m_currentHID = currentHID;
    
    m_detectionInfo->setText(info);
    m_addButton->setEnabled(true);
    m_sendToTargetButton->setEnabled(true);
    
    qCDebug(log_keyboard_editor) << "Key detected -" 
                                 << "Qt:" << QString::number(qtKey, 16) 
                                 << "HID:" << QString::number(currentHID, 16);
}

void KeyboardMapEditor::onTargetCharEntered()
{
    QString targetText = m_targetCharEdit->text().trimmed();
    if (targetText.isEmpty()) {
        m_correctedHIDEdit->clear();
        return;
    }
    
    // Suggest HID scancode
    QChar targetChar = targetText[0];
    QList<uint8_t> possibleHIDs = findPossibleHIDForChar(targetChar);
    
    if (!possibleHIDs.isEmpty()) {
        m_correctedHIDEdit->setText(QString("0x%1").arg(possibleHIDs.first(), 2, 16, QChar('0')));
        qCDebug(log_keyboard_editor) << "Suggested HID for" << targetChar 
                                     << ":" << QString::number(possibleHIDs.first(), 16);
    }
}

void KeyboardMapEditor::onSendToTarget()
{
    if (m_currentQtKey == 0) {
        QMessageBox::warning(this, "No Key Captured", 
            "Please press a key in the test area first.");
        return;
    }
    
    uint8_t hidToSend = m_currentHID;
    
    // If current HID is 0 (unmapped), check if user entered a HID code manually
    if (hidToSend == 0) {
        QString manualHID = m_correctedHIDEdit->text().trimmed();
        
        if (manualHID.isEmpty()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this, 
                "No HID Mapping", 
                QString("The captured key has no HID mapping (0x00).\n\n"
                       "Do you want to:\n"
                       "• Enter a HID code manually in the 'Corrected HID' field to test\n"
                       "• Or try common HID codes for '%1' character?\n\n"
                       "Click Yes to see suggested HID codes.")
                       .arg(m_currentKeyText.isEmpty() ? "this key" : m_currentKeyText),
                QMessageBox::Yes | QMessageBox::Cancel);
            
            if (reply == QMessageBox::Yes) {
                // Try to find possible HID codes based on key text
                if (!m_currentKeyText.isEmpty()) {
                    QList<uint8_t> suggestions = findPossibleHIDForChar(m_currentKeyText[0]);
                    if (!suggestions.isEmpty()) {
                        QString suggestMsg = QString("Suggested HID codes for '%1':\n").arg(m_currentKeyText);
                        for (int i = 0; i < qMin(suggestions.size(), 5); ++i) {
                            suggestMsg += QString("\n0x%1 - %2")
                                .arg(suggestions[i], 2, 16, QChar('0'))
                                .arg(HIDScancode::describe(suggestions[i]));
                        }
                        suggestMsg += "\n\nEnter one in 'Corrected HID' field and try again.";
                        QMessageBox::information(this, "Suggested HID Codes", suggestMsg);
                        
                        // Auto-fill first suggestion
                        m_correctedHIDEdit->setText(QString("0x%1").arg(suggestions[0], 2, 16, QChar('0')));
                    } else {
                        QMessageBox::information(this, "No Suggestions", 
                            "No HID code suggestions found.\nPlease enter a HID code manually (e.g., 0x1E for '1').");
                    }
                } else {
                    QMessageBox::information(this, "Manual Input Required", 
                        "Please enter a HID code manually in 'Corrected HID' field (e.g., 0x1E).");
                }
                m_correctedHIDEdit->setFocus();
            }
            return;
        }
        
        // Parse manually entered HID
        bool ok;
        QString cleanHex = manualHID.startsWith("0x") ? manualHID.mid(2) : manualHID;
        hidToSend = cleanHex.toUInt(&ok, 16);
        if (!ok || hidToSend == 0) {
            QMessageBox::warning(this, "Invalid HID", 
                "Please enter a valid HID scancode (e.g., 0x1F).");
            return;
        }
    }
    
    // Build keyboard command using the HID scancode
    QByteArray keyData = CMD_SEND_KB_GENERAL_DATA;
    
    // Convert Qt modifiers to HID modifier byte
    uint8_t modifierByte = 0;
    if (m_currentModifiers & Qt::ShiftModifier) modifierByte |= 0x02;
    if (m_currentModifiers & Qt::ControlModifier) modifierByte |= 0x01;
    if (m_currentModifiers & Qt::AltModifier) modifierByte |= 0x04;
    if (m_currentModifiers & Qt::MetaModifier) modifierByte |= 0x08;
    
    // Set modifier and key code for key press
    keyData[5] = static_cast<char>(modifierByte);
    keyData[7] = static_cast<char>(hidToSend);
    
    QString hidSource = (hidToSend == m_currentHID) ? "from layout" : "manual";
    qCDebug(log_keyboard_editor) << "Sending key to target:"
                                 << "HID=0x" << QString::number(hidToSend, 16)
                                 << "(" << hidSource << ")"
                                 << "Modifiers=0x" << QString::number(modifierByte, 16);
    
    // Send key press
    emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
    
    // Wait a bit then send key release
    QTimer::singleShot(50, this, [this, hidToSend]() {
        QByteArray releaseData = CMD_SEND_KB_GENERAL_DATA;
        releaseData[5] = static_cast<char>(0x00);  // No modifiers on release
        releaseData[7] = static_cast<char>(0x00);  // No key pressed
        
        qCDebug(log_keyboard_editor) << "Sending key release to target";
        emit SerialPortManager::getInstance().sendCommandAsync(releaseData, false);
        
        // Show message to remind user to check target device
        m_detectionInfo->append(QString("\n--- Sent HID 0x%1 to target ---").arg(hidToSend, 2, 16, QChar('0')));
        m_detectionInfo->append(QString("Check target device and enter the displayed character above."));
        
        // Focus on target char input field
        m_targetCharEdit->setFocus();
    });
}

void KeyboardMapEditor::onAddCorrection()
{
    if (m_currentQtKey == 0) {
        QMessageBox::warning(this, "No Key", 
            "Please press a key in the test area first.");
        return;
    }
    
    QString targetText = m_targetCharEdit->text().trimmed();
    QString hidText = m_correctedHIDEdit->text().trimmed();
    
    if (targetText.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", 
            "Please enter the character shown on the target device.");
        return;
    }
    
    QChar targetChar = targetText[0];
    
    // Parse HID code
    uint8_t correctedHID = 0;
    if (!hidText.isEmpty()) {
        bool ok;
        QString cleanHex = hidText.startsWith("0x") ? hidText.mid(2) : hidText;
        correctedHID = cleanHex.toUInt(&ok, 16);
        if (!ok || correctedHID == 0) {
            QMessageBox::warning(this, "Invalid HID", 
                "Please enter a valid HID scancode (e.g., 0x1F).");
            return;
        }
    } else {
        // Auto-detect from target character
        QList<uint8_t> possible = findPossibleHIDForChar(targetChar);
        if (possible.isEmpty()) {
            QMessageBox::warning(this, "Cannot Detect", 
                "Cannot auto-determine HID code. Please enter manually.");
            return;
        }
        correctedHID = possible.first();
    }
    
    // Create correction entry
    KeyMappingCorrection correction;
    correction.qtKey = m_currentQtKey;
    correction.qtKeyName = QKeySequence(m_currentQtKey).toString();
    correction.displayChar = targetChar;
    correction.modifiers = m_currentModifiers;
    correction.originalHID = m_currentHID;
    correction.correctedHID = correctedHID;
    correction.physicalKeyName = getPhysicalKeyName(m_currentQtKey);
    correction.status = KeyMappingCorrection::Modified;
    
    m_corrections[m_currentQtKey] = correction;
    
    updateCorrectionTable();
    
    // Clear fields for next entry
    m_targetCharEdit->clear();
    m_correctedHIDEdit->clear();
    m_testWidget->setFocus();
    m_addButton->setEnabled(false);
    
    QMessageBox::information(this, "Success", 
        QString("Correction added: %1 (0x%2 → 0x%3)")
        .arg(correction.displayChar)
        .arg(correction.originalHID, 2, 16, QChar('0'))
        .arg(correction.correctedHID, 2, 16, QChar('0')));
}

void KeyboardMapEditor::onRemoveCorrection()
{
    int currentRow = m_correctionTable->currentRow();
    if (currentRow < 0) {
        return;
    }
    
    // Get Qt key from table
    QTableWidgetItem* keyItem = m_correctionTable->item(currentRow, 1);
    if (!keyItem) {
        return;
    }
    
    // Find and remove the correction
    for (auto it = m_corrections.begin(); it != m_corrections.end(); ++it) {
        if (it.value().qtKeyName == keyItem->text()) {
            m_corrections.erase(it);
            break;
        }
    }
    
    updateCorrectionTable();
}

void KeyboardMapEditor::onSaveLayout()
{
    QString customName = m_customNameEdit->text().trimmed();
    if (customName.isEmpty()) {
        QMessageBox::warning(this, "Missing Name", 
            "Please enter a custom layout name.");
        return;
    }
    
    if (m_corrections.isEmpty()) {
        QMessageBox::warning(this, "No Corrections", 
            "No corrections have been added. Do you want to save an empty layout?");
    }
    
    // Apply corrections to base layout
    QMap<int, uint8_t> keyMapCorrections;
    QMap<uint8_t, int> charMapCorrections;
    
    for (auto it = m_corrections.begin(); it != m_corrections.end(); ++it) {
        const KeyMappingCorrection& corr = it.value();
        keyMapCorrections[corr.qtKey] = corr.correctedHID;
        
        if (!corr.displayChar.isNull() && corr.displayChar != QChar('?')) {
            charMapCorrections[corr.displayChar.unicode()] = corr.qtKey;
        }
    }
    
    KeyboardLayoutConfig customLayout = KeyboardLayoutManager::getInstance().mergeCorrections(
        m_baseLayout, keyMapCorrections, charMapCorrections
    );
    customLayout.name = customName;
    
    // Save to file
    if (KeyboardLayoutManager::getInstance().saveCustomLayout(customLayout, customName)) {
        QMessageBox::information(this, "Success", 
            QString("Custom keyboard layout '%1' saved!\n\n"
                    "Corrected %2 key mappings.\n"
                    "Please select this layout from the keyboard layout dropdown in the main window.")
            .arg(customName)
            .arg(m_corrections.size()));
        accept();
    } else {
        QMessageBox::critical(this, "Error", "Failed to save custom layout.");
    }
}

void KeyboardMapEditor::onTestLayout()
{
    applyCorrectionsToLayout();
    QMessageBox::information(this, "Test Layout", 
        "Corrections have been temporarily applied to the test area.\n"
        "Continue testing keys to verify the corrections are correct.");
}

void KeyboardMapEditor::onExportJson()
{
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Keyboard Layout", 
        QDir::homePath() + "/" + m_customNameEdit->text() + ".json",
        "JSON Files (*.json)");
    
    if (fileName.isEmpty()) {
        return;
    }
    
    QString customName = m_customNameEdit->text().trimmed();
    if (customName.isEmpty()) {
        customName = "Custom Layout";
    }
    
    QMap<int, uint8_t> keyMapCorrections;
    for (auto it = m_corrections.begin(); it != m_corrections.end(); ++it) {
        keyMapCorrections[it.value().qtKey] = it.value().correctedHID;
    }
    
    KeyboardLayoutConfig exportLayout = KeyboardLayoutManager::getInstance().mergeCorrections(
        m_baseLayout, keyMapCorrections, QMap<uint8_t, int>()
    );
    exportLayout.name = customName;
    
    QString jsonStr = KeyboardLayoutManager::getInstance().exportLayoutToJson(exportLayout);
    
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(jsonStr.toUtf8());
        file.close();
        QMessageBox::information(this, "Success", 
            QString("Layout exported to: %1").arg(fileName));
    } else {
        QMessageBox::critical(this, "Error", "Export failed.");
    }
}

void KeyboardMapEditor::onImportJson()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Import Keyboard Layout",
        QDir::homePath(),
        "JSON Files (*.json)");
    
    if (fileName.isEmpty()) {
        return;
    }
    
    if (KeyboardLayoutManager::getInstance().importLayoutFromJson(fileName)) {
        loadBaseLayouts(); // Refresh layouts
        QMessageBox::information(this, "Success", "Layout imported successfully.");
    } else {
        QMessageBox::critical(this, "Error", "Import failed.");
    }
}

void KeyboardMapEditor::updateCorrectionTable()
{
    m_correctionTable->setRowCount(0);
    
    int row = 0;
    for (auto it = m_corrections.begin(); it != m_corrections.end(); ++it) {
        const KeyMappingCorrection& corr = it.value();
        
        m_correctionTable->insertRow(row);
        
        m_correctionTable->setItem(row, 0, new QTableWidgetItem(corr.displayChar));
        m_correctionTable->setItem(row, 1, new QTableWidgetItem(corr.qtKeyName));
        m_correctionTable->setItem(row, 2, new QTableWidgetItem(
            QString("0x%1").arg(corr.originalHID, 2, 16, QChar('0'))));
        m_correctionTable->setItem(row, 3, new QTableWidgetItem(
            QString("0x%1").arg(corr.correctedHID, 2, 16, QChar('0'))));
        m_correctionTable->setItem(row, 4, new QTableWidgetItem(corr.statusString()));
        
        QPushButton* deleteBtn = new QPushButton("Delete", this);
        connect(deleteBtn, &QPushButton::clicked, [this, row]() {
            m_correctionTable->selectRow(row);
            onRemoveCorrection();
        });
        m_correctionTable->setCellWidget(row, 5, deleteBtn);
        
        row++;
    }
}

QList<uint8_t> KeyboardMapEditor::findPossibleHIDForChar(QChar ch)
{
    QList<uint8_t> results;
    
    // Search through all loaded layouts
    QStringList allLayouts = KeyboardLayoutManager::getInstance().getAvailableLayouts();
    for (const QString& layoutName : allLayouts) {
        KeyboardLayoutConfig layout = KeyboardLayoutManager::getInstance().getLayout(layoutName);
        
        // Search in char mapping
        for (auto it = layout.charMapping.begin(); it != layout.charMapping.end(); ++it) {
            if (QChar(it.key()) == ch) {
                int qtKey = it.value();
                uint8_t hid = layout.keyMap.value(qtKey, 0);
                if (hid != 0 && !results.contains(hid)) {
                    results.append(hid);
                }
            }
        }
    }
    
    return results;
}

QString KeyboardMapEditor::getPhysicalKeyName(int qtKey)
{
    return QKeySequence(qtKey).toString() + " key";
}

void KeyboardMapEditor::applyCorrectionsToLayout()
{
    QMap<int, uint8_t> keyMapCorrections;
    for (auto it = m_corrections.begin(); it != m_corrections.end(); ++it) {
        keyMapCorrections[it.value().qtKey] = it.value().correctedHID;
    }
    
    KeyboardLayoutConfig testLayout = KeyboardLayoutManager::getInstance().mergeCorrections(
        m_baseLayout, keyMapCorrections, QMap<uint8_t, int>()
    );
    
    m_testWidget->setCurrentLayout(testLayout);
}
