#ifndef KEYBOARDMAPEDITOR_H
#define KEYBOARDMAPEDITOR_H

#include <QDialog>
#include <QMap>
#include <QPushButton>
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLoggingCategory>
#include <cstdint>
#include "target/KeyboardLayouts.h"
#include "target/HIDScancodeReference.h"

Q_DECLARE_LOGGING_CATEGORY(log_keyboard_editor)

/**
 * @brief Widget for testing keyboard input and detecting key presses
 */
class KeyTestWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KeyTestWidget(QWidget *parent = nullptr);
    void setCurrentLayout(const KeyboardLayoutConfig& layout);
    
signals:
    void keyDetected(int qtKey, Qt::KeyboardModifiers modifiers, QString keyText, uint8_t currentHID, QString info);
    
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    
private:
    KeyboardLayoutConfig m_layout;
    QLabel* m_promptLabel;
    QLabel* m_statusLabel;
    QString formatModifiers(Qt::KeyboardModifiers mods);
    QString describePhysicalKey(int qtKey);
};

/**
 * @brief Structure to hold keyboard mapping correction information
 */
struct KeyMappingCorrection {
    int qtKey;                      // Qt key code (e.g., Qt::Key_At)
    QString qtKeyName;              // Display name (e.g., "Key_At")
    QChar displayChar;              // Character to display (e.g., '@')
    Qt::KeyboardModifiers modifiers; // Required modifiers
    uint8_t originalHID;            // Original HID scancode
    uint8_t correctedHID;           // Corrected HID scancode
    QString physicalKeyName;        // Physical key description
    
    enum Status {
        Untested,
        TestedCorrect,
        TestedWrong,
        Modified
    };
    Status status;
    
    KeyMappingCorrection()
        : qtKey(0), originalHID(0), correctedHID(0), status(Untested) {}
    
    QString description() const;
    QString statusString() const;
};

/**
 * @brief Main keyboard mapping editor dialog
 */
class KeyboardMapEditor : public QDialog
{
    Q_OBJECT

public:
    explicit KeyboardMapEditor(QWidget *parent = nullptr);
    ~KeyboardMapEditor();

private slots:
    void onBaseLayoutChanged(const QString& layoutName);
    void onKeyDetected(int qtKey, Qt::KeyboardModifiers modifiers, QString keyText, uint8_t currentHID, QString info);
    void onTargetCharEntered();
    void onAddCorrection();
    void onSendToTarget();
    void onRemoveCorrection();
    void onSaveLayout();
    void onTestLayout();
    void onExportJson();
    void onImportJson();
    void updateCorrectionTable();
    
private:
    // Data
    KeyboardLayoutConfig m_baseLayout;
    QMap<int, KeyMappingCorrection> m_corrections;  // Qt key -> correction info
    
    // Current testing state
    int m_currentQtKey;
    Qt::KeyboardModifiers m_currentModifiers;
    QString m_currentKeyText;
    uint8_t m_currentHID;
    
    // UI components
    KeyTestWidget* m_testWidget;
    QComboBox* m_baseLayoutCombo;
    QLineEdit* m_customNameEdit;
    QTextEdit* m_detectionInfo;
    QLineEdit* m_targetCharEdit;
    QLineEdit* m_correctedHIDEdit;
    QPushButton* m_addButton;
    QPushButton* m_sendToTargetButton;
    QPushButton* m_removeButton;
    QPushButton* m_saveButton;
    QPushButton* m_testButton;
    QPushButton* m_exportButton;
    QPushButton* m_importButton;
    QTableWidget* m_correctionTable;
    
    void setupUI();
    void setupConnections();
    void loadBaseLayouts();
    QList<uint8_t> findPossibleHIDForChar(QChar ch);
    QString getPhysicalKeyName(int qtKey);
    void applyCorrectionsToLayout();
};

#endif // KEYBOARDMAPEDITOR_H
