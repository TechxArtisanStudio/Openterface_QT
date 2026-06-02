#ifndef CUSTOMKEYDIALOG_H
#define CUSTOMKEYDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "customkeymanager.h"

// Lightweight dialog for capturing a key combination (up to 6 simultaneous keys)
class KeyComboCaptureDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyComboCaptureDialog(QList<int> currentKeyCodes = QList<int>(), QWidget *parent = nullptr);

    QList<int> getKeyCodes() const;

private:
    QLabel* promptLabel;
    QLabel* currentComboLabel;
    QPushButton* doneBtn;
    QPushButton* cancelBtn;
    QPushButton* clearBtn;

    QList<int> m_keyCodes;
    bool m_capturing;

    void buildUI();
    void updateComboDisplay();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
};

// Main custom key configuration dialog
class CustomKeyDialog : public QDialog {
    Q_OBJECT
public:
    explicit CustomKeyDialog(QWidget *parent = nullptr);

private:
    QTableWidget* keysTable;
    QPushButton* addBtn;
    QPushButton* removeBtn;
    QPushButton* moveUpBtn;
    QPushButton* moveDownBtn;
    QPushButton* importBtn;
    QPushButton* exportBtn;
    QPushButton* closeBtn;
    QLabel* presetLabel;
    QComboBox* presetCombo;
    QPushButton* savePresetBtn;
    QPushButton* deletePresetBtn;

    void buildUI();
    void populateKeysList();
    void addKey();
    void addSeparator();
    void removeKey();
    void moveKeyUp();
    void moveKeyDown();
    void importJson();
    void exportJson();
    void loadPreset();
    void savePreset();
    void saveAsPreset();
    void deletePreset();
    void refreshPresetCombo();
    void captureKeysForRow(int row);
    QString formatComboString(const QList<int>& keyCodes);
    void saveRowName(int row, const QString& name);

private slots:
    void onCellDoubleClicked(int row, int column);
    void onItemChanged(QTableWidgetItem* item);
};

#endif // CUSTOMKEYDIALOG_H
