#ifndef VIRTUALKEYBOARDDIALOG_H
#define VIRTUALKEYBOARDDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPoint>
#include <QMouseEvent>
#include <QTimer>
#include "../../target/KeyboardLayouts.h"

class ShiftedKeyButton;

class VirtualKeyboardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VirtualKeyboardDialog(QWidget *parent = nullptr, const QString& layoutName = "US QWERTY");
    ~VirtualKeyboardDialog() = default;

    // Public method to show key press feedback
    void showKeyPressFeedback(int keyCode, int durationMs = 150);

    // Set keyboard layout dynamically
    void setKeyboardLayout(const QString& layoutName);

    // Get current layout name
    QString getCurrentLayoutName() const { return m_currentLayoutName; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setupUI();
    void createKeyboardLayout(); // Keep for backward compatibility
    QPushButton* createKeyButton(const QString& text, int keyCode, bool isModifier = false);
    void registerShiftedKeyButton(ShiftedKeyButton* button);
    void clearKeyboardLayout(); // Helper to clear existing layout (not currently used)

    // Row builders — each takes a QVBoxLayout* for the rows container
    void addFunctionKeyRow(QVBoxLayout* rowsLayout);
    void addNumberRow(QVBoxLayout* rowsLayout);
    void addTabRow(QVBoxLayout* rowsLayout);
    void addCapsRow(QVBoxLayout* rowsLayout);
    void addShiftRow(QVBoxLayout* rowsLayout);
    void addNavigationRow(QVBoxLayout* rowsLayout);
    void addBottomRow(QVBoxLayout* rowsLayout);

    // Helper to find widget by key code
    QWidget* findWidgetByKeyCode(int keyCode);

    // Update key labels when layout changes
    void updateKeyLabelsForLayout(const QString& layoutName);

    // Helper function to find Qt key code for a given character in the current layout
    int findQtKeyForChar(const QString& baseChar, const QString& shiftedChar, const KeyboardLayoutConfig& config);

private slots:
    void onKeyClicked();
    void onModifierToggled(bool checked);

private:
    QVBoxLayout* mainLayout;
    QVBoxLayout* m_rowsLayout;   // container for independent row layouts
    QGridLayout* keyboardLayout; // kept for compatibility, not used
    int currentModifiers;
    QMap<QPushButton*, int> modifierButtons;
    QMap<int, QWidget*> keyCodeToWidgetMap; // Map key codes to widgets (QPushButton or ShiftedKeyButton)

    // Drag support
    bool m_dragging;
    QPoint m_dragPosition;

    // Layout support
    QString m_currentLayoutName;
};

#endif // VIRTUALKEYBOARDDIALOG_H
