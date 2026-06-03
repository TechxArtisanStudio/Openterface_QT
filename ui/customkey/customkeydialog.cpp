#include "customkeydialog.h"
#include "../globalsetting.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QInputDialog>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <QLineEdit>
#include <QHeaderView>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_customkey_dialog, "opf.ui.customkeydialog")

// ─── KeyComboCaptureDialog ────────────────────────────────────────────────────

KeyComboCaptureDialog::KeyComboCaptureDialog(QList<int> currentKeyCodes, QWidget *parent)
    : QDialog(parent), m_keyCodes(currentKeyCodes), m_capturing(true)
{
    setWindowTitle(tr("Capture Key Combination"));
    setModal(true);
    resize(420, 280);
    buildUI();
    updateComboDisplay();
}

QList<int> KeyComboCaptureDialog::getKeyCodes() const
{
    return m_keyCodes;
}

void KeyComboCaptureDialog::buildUI()
{
    auto *layout = new QVBoxLayout(this);

    promptLabel = new QLabel(tr("Hold down modifiers and press keys.\nMax 6 keys total. Click Done to finish."), this);
    promptLabel->setAlignment(Qt::AlignCenter);
    promptLabel->setWordWrap(true);
    layout->addWidget(promptLabel);

    currentComboLabel = new QLabel(this);
    currentComboLabel->setAlignment(Qt::AlignCenter);
    currentComboLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2196F3; padding: 10px; background: #f5f5f5; border-radius: 4px;");
    layout->addWidget(currentComboLabel);

    layout->addStretch();

    auto *btnLayout = new QHBoxLayout;
    clearBtn = new QPushButton(tr("Clear"), this);
    doneBtn = new QPushButton(tr("Done"), this);
    cancelBtn = new QPushButton(tr("Cancel"), this);

    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(doneBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(doneBtn, &QPushButton::clicked, [this]() {
        if (m_keyCodes.isEmpty()) {
            QMessageBox::warning(this, tr("Warning"), tr("Please capture at least one key."));
        } else {
            accept();
        }
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(clearBtn, &QPushButton::clicked, [this]() {
        m_keyCodes.clear();
        updateComboDisplay();
    });
}

void KeyComboCaptureDialog::updateComboDisplay()
{
    if (m_keyCodes.isEmpty()) {
        currentComboLabel->setText(tr("Waiting for key press..."));
    } else {
        QStringList parts;
        for (int kc : m_keyCodes) {
            parts << CustomKeyManager::codeToKeyName(kc);
        }
        currentComboLabel->setText(parts.join(" + "));
    }

    doneBtn->setEnabled(!m_keyCodes.isEmpty());
    clearBtn->setEnabled(!m_keyCodes.isEmpty());

    if (m_keyCodes.size() >= MAX_KEY_COMBO) {
        promptLabel->setText(tr("Maximum keys reached. Click Done to finish."));
        m_capturing = false;
    } else {
        promptLabel->setText(tr("Hold modifiers and press keys (%1/%2).").arg(m_keyCodes.size()).arg(MAX_KEY_COMBO));
        m_capturing = true;
    }
}

void KeyComboCaptureDialog::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();

    // Ignore modifier-only presses when they are the first key
    if (!CustomKeyManager::isModifierKey(key)) {
        if (m_capturing && !m_keyCodes.contains(key)) {
            m_keyCodes << key;
            updateComboDisplay();
        }
    } else {
        // Allow modifier to be added if we have other keys already, or if user holds it with another key
        // For modifier keys, we add them only if not already present
        if (m_capturing && !m_keyCodes.contains(key)) {
            m_keyCodes << key;
            updateComboDisplay();
        }
    }

    QDialog::keyPressEvent(event);
}

void KeyComboCaptureDialog::keyReleaseEvent(QKeyEvent* event)
{
    // Don't close on release, just pass through
    QDialog::keyReleaseEvent(event);
}

// ─── CustomKeyDialog ──────────────────────────────────────────────────────────

CustomKeyDialog::CustomKeyDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Custom Key Configuration"));
    setModal(true);
    resize(700, 600);
    buildUI();
    populateKeysList();
    refreshPresetCombo();
}

void CustomKeyDialog::buildUI()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Keys table
    keysTable = new QTableWidget(0, 2, this);
    keysTable->setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Combo Keys"));
    keysTable->horizontalHeader()->setStretchLastSection(true);
    keysTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    keysTable->horizontalHeader()->resizeSection(0, 150);
    keysTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    keysTable->setSelectionMode(QAbstractItemView::SingleSelection);
    // Allow inline editing of name column on double-click
    keysTable->setEditTriggers(QAbstractItemView::DoubleClicked);
    keysTable->verticalHeader()->setVisible(false);
    keysTable->setAlternatingRowColors(true);

    mainLayout->addWidget(new QLabel(tr("Custom Keys (displayed on toolbar):")));
    mainLayout->addWidget(keysTable);

    // CRUD buttons
    auto *crudLayout = new QHBoxLayout;

    addBtn = new QPushButton(tr("+"), this);
    addBtn->setFixedWidth(36);
    addBtn->setToolTip(tr("Add key or separator"));
    QMenu* addMenu = new QMenu(addBtn);
    addMenu->addAction(tr("Add Key"), this, &CustomKeyDialog::addKey);
    addMenu->addAction(tr("Add Separator"), this, &CustomKeyDialog::addSeparator);
    addBtn->setMenu(addMenu);

    removeBtn = new QPushButton(tr("-"), this);
    removeBtn->setFixedWidth(36);
    moveUpBtn = new QPushButton(tr("↑"), this);
    moveUpBtn->setFixedWidth(36);
    moveDownBtn = new QPushButton(tr("↓"), this);
    moveDownBtn->setFixedWidth(36);

    crudLayout->addWidget(addBtn);
    crudLayout->addWidget(removeBtn);
    crudLayout->addSpacing(10);
    crudLayout->addWidget(moveUpBtn);
    crudLayout->addWidget(moveDownBtn);
    crudLayout->addStretch();
    mainLayout->addLayout(crudLayout);

    // Import / Export
    auto *ioLayout = new QHBoxLayout;
    importBtn = new QPushButton(tr("Import JSON..."));
    exportBtn = new QPushButton(tr("Export JSON..."));
    ioLayout->addWidget(importBtn);
    ioLayout->addWidget(exportBtn);
    mainLayout->addLayout(ioLayout);

    // Preset section
    auto *presetGroup = new QGroupBox(tr("Presets"));
    auto *presetLayout = new QHBoxLayout(presetGroup);
    presetLabel = new QLabel(tr("Preset:"));
    presetCombo = new QComboBox;
    presetCombo->setMinimumWidth(200);
    savePresetBtn = new QPushButton(tr("Save"));
    QPushButton* saveAsBtn = new QPushButton(tr("Save As"));
    deletePresetBtn = new QPushButton(tr("Delete"));
    presetLayout->addWidget(presetLabel);
    presetLayout->addWidget(presetCombo);
    presetLayout->addWidget(savePresetBtn);
    presetLayout->addWidget(saveAsBtn);
    presetLayout->addWidget(deletePresetBtn);
    mainLayout->addWidget(presetGroup);

    // Close button
    closeBtn = new QPushButton(tr("Close"));
    auto *closeLayout = new QHBoxLayout;
    closeLayout->addStretch();
    closeLayout->addWidget(closeBtn);
    mainLayout->addLayout(closeLayout);

    // Connections
    connect(keysTable, &QTableWidget::cellDoubleClicked, this, &CustomKeyDialog::onCellDoubleClicked);
    connect(keysTable, &QTableWidget::itemChanged, this, &CustomKeyDialog::onItemChanged);
    connect(removeBtn, &QPushButton::clicked, this, &CustomKeyDialog::removeKey);
    connect(moveUpBtn, &QPushButton::clicked, this, &CustomKeyDialog::moveKeyUp);
    connect(moveDownBtn, &QPushButton::clicked, this, &CustomKeyDialog::moveKeyDown);
    connect(importBtn, &QPushButton::clicked, this, &CustomKeyDialog::importJson);
    connect(exportBtn, &QPushButton::clicked, this, &CustomKeyDialog::exportJson);
    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CustomKeyDialog::loadPreset);
    connect(savePresetBtn, &QPushButton::clicked, this, &CustomKeyDialog::savePreset);
    connect(saveAsBtn, &QPushButton::clicked, this, &CustomKeyDialog::saveAsPreset);
    connect(deletePresetBtn, &QPushButton::clicked, this, &CustomKeyDialog::deletePreset);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void CustomKeyDialog::populateKeysList()
{
    keysTable->blockSignals(true);
    keysTable->setRowCount(0);

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();

    for (int i = 0; i < keys.size(); ++i) {
        const CustomKeyInfo &info = keys[i];
        keysTable->insertRow(i);

        QTableWidgetItem *nameItem = new QTableWidgetItem(info.displayName);
        QTableWidgetItem *comboItem = new QTableWidgetItem();

        if (info.isSeparator) {
            nameItem->setText(tr("--- Separator ---"));
            nameItem->setForeground(Qt::gray);
            nameItem->setTextAlignment(Qt::AlignCenter);
            comboItem->setText(tr("---"));
            comboItem->setForeground(Qt::gray);
            comboItem->setTextAlignment(Qt::AlignCenter);
            // Mark as non-editable
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            comboItem->setFlags(comboItem->flags() & ~Qt::ItemIsEditable);
        } else {
            comboItem->setText(formatComboString(info.keyCodes));
            // Name is editable, combo is read-only (double-click triggers capture)
            nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
            comboItem->setFlags(comboItem->flags() & ~Qt::ItemIsEditable);
        }

        keysTable->setItem(i, 0, nameItem);
        keysTable->setItem(i, 1, comboItem);
    }

    keysTable->blockSignals(false);
}

QString CustomKeyDialog::formatComboString(const QList<int>& keyCodes)
{
    if (keyCodes.isEmpty()) {
        return tr("(Double-click to capture)");
    }

    QStringList parts;
    for (int kc : keyCodes) {
        parts << CustomKeyManager::codeToKeyName(kc);
    }
    return parts.join(" + ");
}

void CustomKeyDialog::saveRowName(int row, const QString& name)
{
    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();
    if (row >= keys.size() || keys[row].isSeparator) return;

    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    if (trimmed.length() > 6) trimmed = trimmed.left(6);

    keys[row].displayName = trimmed;
    mgr.setKeys(keys);
}

void CustomKeyDialog::onCellDoubleClicked(int row, int column)
{
    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();
    if (row >= keys.size()) return;

    if (keys[row].isSeparator) return;

    // Column 0 (Name) is handled by inline editing via DoubleClicked edit trigger
    // Column 1 (Combo Keys) triggers capture dialog
    if (column == 1) {
        captureKeysForRow(row);
    }
}

void CustomKeyDialog::onItemChanged(QTableWidgetItem* item)
{
    if (!item) return;
    int row = item->row();
    int col = item->column();

    // Only handle name column changes
    if (col != 0) return;

    saveRowName(row, item->text());
}

void CustomKeyDialog::captureKeysForRow(int row)
{
    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();
    if (row >= keys.size() || keys[row].isSeparator) return;

    KeyComboCaptureDialog dlg(keys[row].keyCodes, this);
    if (dlg.exec() == QDialog::Accepted) {
        QList<int> newKeyCodes = dlg.getKeyCodes();
        if (!newKeyCodes.isEmpty()) {
            keys[row].keyCodes = newKeyCodes;
            mgr.setKeys(keys);
            populateKeysList();
        }
    }
}

void CustomKeyDialog::addKey()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Add Key"),
        tr("Enter button name (max 6 chars):"), QLineEdit::Normal,
        QString(), &ok);

    if (!ok || name.isEmpty()) return;
    if (name.length() > 6) name = name.left(6);

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();

    CustomKeyInfo info;
    info.displayName = name;
    info.isSeparator = false;
    keys.append(info);

    int newRow = keys.size() - 1;
    mgr.setKeys(keys);
    populateKeysList();
    keysTable->selectRow(newRow);

    // Immediately capture keys for the new entry
    captureKeysForRow(newRow);
}

void CustomKeyDialog::addSeparator()
{
    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();

    CustomKeyInfo sep;
    sep.isSeparator = true;
    sep.displayName = "";
    keys.append(sep);
    mgr.setKeys(keys);
    populateKeysList();
}

void CustomKeyDialog::removeKey()
{
    int row = keysTable->currentRow();
    if (row < 0) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();
    if (row >= keys.size()) return;

    keys.removeAt(row);
    mgr.setKeys(keys);
    populateKeysList();
}

void CustomKeyDialog::moveKeyUp()
{
    int row = keysTable->currentRow();
    if (row <= 0) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();
    keys.swapItemsAt(row, row - 1);
    mgr.setKeys(keys);
    populateKeysList();
    keysTable->setCurrentCell(row - 1, 0);
}

void CustomKeyDialog::moveKeyDown()
{
    int row = keysTable->currentRow();
    if (row < 0 || row >= keysTable->rowCount() - 1) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = mgr.getKeys();
    keys.swapItemsAt(row, row + 1);
    mgr.setKeys(keys);
    populateKeysList();
    keysTable->setCurrentCell(row + 1, 0);
}

void CustomKeyDialog::importJson()
{
    // Start from last imported path if available
    QString startDir = GlobalSetting::instance().getLastCustomKeyImportPath();

    QString file = QFileDialog::getOpenFileName(this, tr("Import Custom Keys JSON"),
        startDir, tr("JSON Files (*.json)"));
    if (file.isEmpty()) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    if (mgr.importFromJson(file)) {
        // Remember this path for next time
        GlobalSetting::instance().setLastCustomKeyImportPath(file);
        mgr.setKeys(mgr.getKeys());
        populateKeysList();
        refreshPresetCombo();
        QMessageBox::information(this, tr("Import"), tr("Keys imported successfully."));
    } else {
        QMessageBox::warning(this, tr("Import"), tr("Failed to import keys from %1").arg(file));
    }
}

void CustomKeyDialog::exportJson()
{
    QString file = QFileDialog::getSaveFileName(this, tr("Export Custom Keys JSON"),
        QString(), tr("JSON Files (*.json)"));
    if (file.isEmpty()) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    if (mgr.exportToJson(file)) {
        QMessageBox::information(this, tr("Export"), tr("Keys exported to %1").arg(file));
    } else {
        QMessageBox::warning(this, tr("Export"), tr("Failed to export keys."));
    }
}

void CustomKeyDialog::loadPreset()
{
    QString name = presetCombo->currentText();
    if (name.isEmpty()) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    if (mgr.loadPreset(name)) {
        mgr.setKeys(mgr.getKeys());
        populateKeysList();
    }
}

void CustomKeyDialog::savePreset()
{
    // Save to current selected preset (overwrite)
    QString name = presetCombo->currentText();
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("Save"), tr("No preset selected to save."));
        return;
    }
    if (name == "Default") {
        QMessageBox::information(this, tr("Save"), tr("Cannot overwrite the built-in Default preset. Use Save As instead."));
        return;
    }

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    if (mgr.savePreset(name)) {
        refreshPresetCombo();
        QMessageBox::information(this, tr("Save"), tr("Preset '%1' saved.").arg(name));
    } else {
        QMessageBox::warning(this, tr("Save"), tr("Failed to save preset."));
    }
}

void CustomKeyDialog::saveAsPreset()
{
    // Save current keys as a new preset
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Save As"),
        tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    if (mgr.savePreset(name)) {
        refreshPresetCombo();
        // Select the newly saved preset in the combo box
        int idx = presetCombo->findText(name);
        if (idx >= 0) presetCombo->setCurrentIndex(idx);
        QMessageBox::information(this, tr("Save As"), tr("Preset '%1' saved.").arg(name));
    } else {
        QMessageBox::warning(this, tr("Save As"), tr("Failed to save preset."));
    }
}

void CustomKeyDialog::deletePreset()
{
    QString name = presetCombo->currentText();
    if (name.isEmpty() || name == "Default" || name == "current") {
        QMessageBox::information(this, tr("Delete Preset"),
            tr("Cannot delete the current selection."));
        return;
    }

    int ret = QMessageBox::question(this, tr("Delete Preset"),
        tr("Delete preset '%1'?").arg(name));
    if (ret != QMessageBox::Yes) return;

    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    if (mgr.deletePreset(name)) {
        refreshPresetCombo();
        QMessageBox::information(this, tr("Delete Preset"), tr("Preset deleted."));
    }
}

void CustomKeyDialog::refreshPresetCombo()
{
    presetCombo->blockSignals(true);
    presetCombo->clear();
    CustomKeyManager &mgr = CustomKeyManager::getInstance();
    QStringList presets = mgr.getPresets();
    presetCombo->addItems(presets);
    int idx = presets.indexOf(mgr.getCurrentPresetName());
    if (idx >= 0) presetCombo->setCurrentIndex(idx);
    presetCombo->blockSignals(false);
}
