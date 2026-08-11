#include "virtualkeyboardpage.h"
#include "customkeydialog.h"
#include "../globalsetting.h"
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDataStream>
#include <QDebug>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDrag>
#include <QApplication>
#include <QMouseEvent>

// ============================================================
// AvailableKeysTree — drag source for available keys
// ============================================================

static const QString KEY_MIME_TYPE = "application/x-openterface-key";
static const QString PREVIEW_MIME_TYPE = "application/x-openterface-preview-internal";

AvailableKeysTree::AvailableKeysTree(QWidget *parent) : QTreeWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

QStringList AvailableKeysTree::mimeTypes() const
{
    return {KEY_MIME_TYPE};
}

QMimeData* AvailableKeysTree::mimeData(const QList<QTreeWidgetItem*>& items) const
{
    if (items.isEmpty()) return nullptr;
    QTreeWidgetItem *item = items.first();
    // Only allow dragging child items (not category headers)
    if (!item->parent()) return nullptr;

    QString displayName = item->data(0, Qt::UserRole).toString();
    QList<int> keyCodes = item->data(0, Qt::UserRole + 1).value<QList<int>>();

    QMimeData *mime = new QMimeData();
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << displayName << keyCodes;
    mime->setData(KEY_MIME_TYPE, data);
    return mime;
}

// ============================================================
// ToolbarPreviewList — drop target + internal reorder
// ============================================================

ToolbarPreviewList::ToolbarPreviewList(QWidget *parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    // Do NOT use DragDrop mode — it causes the model to auto-remove items during drag.
    // Instead, use DropOnly so we can receive drops, but handle drag initiation manually
    // via mousePressEvent/mouseMoveEvent.
    setDragEnabled(false);
    setDragDropMode(QAbstractItemView::DropOnly);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setMovement(QListView::Snap);
    setFlow(QListView::TopToBottom);
    m_dragStartItem = nullptr;
}

QStringList ToolbarPreviewList::mimeTypes() const
{
    return {KEY_MIME_TYPE, PREVIEW_MIME_TYPE};
}

Qt::DropActions ToolbarPreviewList::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

QMimeData* ToolbarPreviewList::mimeData(const QList<QListWidgetItem*>& items) const
{
    if (items.isEmpty()) return nullptr;
    QListWidgetItem *item = items.first();
    int sourceRow = row(item);

    // Don't allow dragging modifier items (first 4)
    if (sourceRow < 4) return nullptr;

    // Encode internal drag data with source row
    QMimeData *mime = new QMimeData();
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    QString displayName = item->text();
    bool isSeparator = item->data(Qt::UserRole).toBool();
    QList<int> keyCodes = item->data(Qt::UserRole + 1).value<QList<int>>();
    stream << sourceRow << displayName << isSeparator << keyCodes;

    mime->setData(PREVIEW_MIME_TYPE, data);
    return mime;
}

void ToolbarPreviewList::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(KEY_MIME_TYPE) || event->mimeData()->hasFormat(PREVIEW_MIME_TYPE)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ToolbarPreviewList::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(KEY_MIME_TYPE) || event->mimeData()->hasFormat(PREVIEW_MIME_TYPE)) {
        QListWidgetItem *hoverItem = itemAt(event->position().toPoint());
        if (hoverItem) {
            int hoverRow = row(hoverItem);
            // Don't allow dropping above modifier items
            if (hoverRow < 4 && event->mimeData()->hasFormat(PREVIEW_MIME_TYPE)) {
                event->ignore();
                return;
            }
        }
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ToolbarPreviewList::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(KEY_MIME_TYPE)) {
        // External drop from available keys tree — add new item
        QByteArray data = event->mimeData()->data(KEY_MIME_TYPE);
        QDataStream stream(&data, QIODevice::ReadOnly);
        QString displayName;
        QList<int> keyCodes;
        stream >> displayName >> keyCodes;

        // Determine drop position
        QListWidgetItem *dropItem = itemAt(event->position().toPoint());
        int dropIndex = dropItem ? row(dropItem) : count();
        if (dropIndex < 4) dropIndex = 4;

        emit externalDrop(dropIndex, displayName, keyCodes);
        event->accept();
    } else if (event->mimeData()->hasFormat(PREVIEW_MIME_TYPE)) {
        // Internal reorder — the model did NOT remove the item (we used manual drag)
        QByteArray data = event->mimeData()->data(PREVIEW_MIME_TYPE);
        QDataStream stream(&data, QIODevice::ReadOnly);
        int sourceRow;
        QString displayName;
        bool isSeparator;
        QList<int> keyCodes;
        stream >> sourceRow >> displayName >> isSeparator >> keyCodes;

        // Don't allow moving modifier items
        if (sourceRow < 4 || sourceRow >= count()) {
            event->ignore();
            return;
        }

        // Determine target position
        QListWidgetItem *dropItem = itemAt(event->position().toPoint());
        int targetRow = dropItem ? row(dropItem) : count();
        if (targetRow < 4) targetRow = 4;

        // Take the item out of its current position
        QListWidgetItem *movedItem = takeItem(sourceRow);
        if (!movedItem) {
            event->ignore();
            return;
        }

        // Adjust target if source was before target (list shifted left after removal)
        int insertRow = targetRow;
        if (sourceRow < targetRow) {
            insertRow--;
        }
        if (insertRow < 4) insertRow = 4;
        if (insertRow > count()) insertRow = count();

        insertItem(insertRow, movedItem);
        setCurrentRow(insertRow);

        event->accept();
        emit itemsReordered();
    } else {
        event->ignore();
    }
}

// Manual drag initiation — replaces Qt's built-in drag mechanism to avoid
// the model auto-removing items during internal reorder.
void ToolbarPreviewList::mousePressEvent(QMouseEvent *event)
{
    QListWidget::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        m_dragStartItem = itemAt(event->pos());
    }
}

void ToolbarPreviewList::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        m_dragStartItem = nullptr;
        return;
    }
    if (!m_dragStartItem) return;

    int sourceRow = row(m_dragStartItem);
    if (sourceRow < 4) {
        m_dragStartItem = nullptr;
        return; // Can't drag modifier items
    }

    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;

    // Create MIME data
    QMimeData *mime = mimeData({m_dragStartItem});
    if (!mime) {
        m_dragStartItem = nullptr;
        return;
    }

    // Create and configure drag
    QDrag *drag = new QDrag(this);
    drag->setMimeData(mime);

    // Use the item's visual rect as the drag pixmap
    QRect itemRect = visualItemRect(m_dragStartItem);
    if (itemRect.isValid() && !itemRect.isEmpty()) {
        QPixmap pixmap = viewport()->grab(itemRect);
        drag->setPixmap(pixmap);
        QPoint cursorInViewport = viewport()->mapFromGlobal(QCursor::pos());
        drag->setHotSpot(cursorInViewport - itemRect.topLeft());
    }

    // Execute the drag — blocks until drop completes or is cancelled
    // The model does NOT remove the item because we didn't use Qt's internal drag
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);

    m_dragStartItem = nullptr;
}

// ============================================================
// VirtualKeyboardPage
// ============================================================

VirtualKeyboardPage::VirtualKeyboardPage(QWidget *parent) : QWidget(parent)
{
    setupUI();
    populateAvailableKeys();
    populatePreview();
    refreshPresetCombo();
}

void VirtualKeyboardPage::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Title
    QLabel *titleLabel = new QLabel(tr("Virtual Keyboard Configuration"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    QLabel *subtitleLabel = new QLabel(tr("Drag keys from the left panel to add them to the toolbar. Drag within the toolbar to reorder."));
    subtitleLabel->setStyleSheet("color: palette(dark);");
    mainLayout->addWidget(subtitleLabel);

    // Splitter for left and right panels
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel — Available Keys
    QGroupBox *availableGroup = new QGroupBox(tr("Available Keys"));
    QVBoxLayout *availableLayout = new QVBoxLayout(availableGroup);
    availableKeysTree = new AvailableKeysTree();
    availableKeysTree->setHeaderHidden(true);
    availableKeysTree->setRootIsDecorated(true);
    availableKeysTree->setMinimumWidth(200);
    availableLayout->addWidget(availableKeysTree);
    splitter->addWidget(availableGroup);

    // Right panel — Toolbar Preview
    QGroupBox *previewGroup = new QGroupBox(tr("Toolbar Preview"));
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);
    previewList = new ToolbarPreviewList();
    previewList->setMinimumHeight(300);
    previewLayout->addWidget(previewList);
    splitter->addWidget(previewGroup);

    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter, 1);

    // Action buttons row
    QHBoxLayout *actionLayout = new QHBoxLayout();

    addKeyBtn = new QPushButton(tr("+ Add Key"));
    addSepBtn = new QPushButton(tr("+ Add Separator"));
    removeBtn = new QPushButton(tr("- Remove"));
    moveUpBtn = new QPushButton(tr("↑ Up"));
    moveDownBtn = new QPushButton(tr("↓ Down"));
    resetBtn = new QPushButton(tr("Reset to Default"));

    actionLayout->addWidget(addKeyBtn);
    actionLayout->addWidget(addSepBtn);
    actionLayout->addWidget(removeBtn);
    actionLayout->addWidget(moveUpBtn);
    actionLayout->addWidget(moveDownBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(resetBtn);
    mainLayout->addLayout(actionLayout);

    // Import/Export + Presets row
    QHBoxLayout *ioLayout = new QHBoxLayout();

    importBtn = new QPushButton(tr("Import JSON..."));
    exportBtn = new QPushButton(tr("Export JSON..."));
    ioLayout->addWidget(importBtn);
    ioLayout->addWidget(exportBtn);
    ioLayout->addSpacing(20);

    ioLayout->addWidget(new QLabel(tr("Preset:")));
    presetCombo = new QComboBox();
    presetCombo->setMinimumWidth(150);
    ioLayout->addWidget(presetCombo);

    loadPresetBtn = new QPushButton(tr("Load"));
    savePresetBtn = new QPushButton(tr("Save"));
    deletePresetBtn = new QPushButton(tr("Delete"));
    ioLayout->addWidget(loadPresetBtn);
    ioLayout->addWidget(savePresetBtn);
    ioLayout->addWidget(deletePresetBtn);
    ioLayout->addStretch();

    mainLayout->addLayout(ioLayout);

    // Connections
    connect(addKeyBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::addKey);
    connect(addSepBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::addSeparator);
    connect(removeBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::removeSelected);
    connect(moveUpBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::moveSelectedUp);
    connect(moveDownBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::moveSelectedDown);
    connect(resetBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::resetToDefault);
    connect(importBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::importJson);
    connect(exportBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::exportJson);
    connect(loadPresetBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::loadPreset);
    connect(savePresetBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::savePreset);
    connect(deletePresetBtn, &QPushButton::clicked, this, &VirtualKeyboardPage::deletePreset);
    connect(previewList, &ToolbarPreviewList::externalDrop, this, &VirtualKeyboardPage::handleExternalDrop);

    // When internal reorder happens via drag, save config
    connect(previewList, &ToolbarPreviewList::itemsReordered, this, [this]() {
        saveCurrentConfig();
    });

    // Double-click on preview item to edit key combo
    connect(previewList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        int row = previewList->row(item);
        if (row < MODIFIER_COUNT) return; // Can't edit modifier items
        if (item->data(Qt::UserRole).toBool()) return; // Separator

        QList<int> currentCodes = item->data(Qt::UserRole + 1).value<QList<int>>();
        KeyComboCaptureDialog captureDlg(currentCodes, this);
        if (captureDlg.exec() == QDialog::Accepted) {
            QList<int> newCodes = captureDlg.getKeyCodes();
            item->setData(Qt::UserRole + 1, QVariant::fromValue(newCodes));
            item->setText(formatComboString(newCodes));
            saveCurrentConfig();
        }
    });
}

void VirtualKeyboardPage::populateAvailableKeys()
{
    availableKeysTree->clear();

    // Helper lambda to add a category with keys
    auto addCategory = [this](const QString& categoryName,
                              const QList<QPair<QString, int>>& keys) {
        QTreeWidgetItem *category = new QTreeWidgetItem(availableKeysTree);
        category->setText(0, categoryName);
        category->setFlags(category->flags() & ~Qt::ItemIsDragEnabled);

        for (const auto& key : keys) {
            QTreeWidgetItem *item = new QTreeWidgetItem(category);
            item->setText(0, key.first);
            item->setData(0, Qt::UserRole, key.first);  // displayName
            item->setData(0, Qt::UserRole + 1, QVariant::fromValue(QList<int>{key.second}));  // keyCodes
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        category->setExpanded(true);
    };

    // Function Keys
    addCategory(tr("Function Keys"), {
        {"F1", Qt::Key_F1}, {"F2", Qt::Key_F2}, {"F3", Qt::Key_F3},
        {"F4", Qt::Key_F4}, {"F5", Qt::Key_F5}, {"F6", Qt::Key_F6},
        {"F7", Qt::Key_F7}, {"F8", Qt::Key_F8}, {"F9", Qt::Key_F9},
        {"F10", Qt::Key_F10}, {"F11", Qt::Key_F11}, {"F12", Qt::Key_F12}
    });

    // Navigation Keys
    addCategory(tr("Navigation"), {
        {"Home", Qt::Key_Home}, {"End", Qt::Key_End},
        {"PgUp", Qt::Key_PageUp}, {"PgDn", Qt::Key_PageDown},
        {"Ins", Qt::Key_Insert}, {"Del", Qt::Key_Delete}
    });

    // Special Keys
    addCategory(tr("Special Keys"), {
        {"Esc", Qt::Key_Escape}, {"PrtSc", Qt::Key_Print},
        {"ScrLk", Qt::Key_ScrollLock}, {"Pause", Qt::Key_Pause}
    });

    // Lock Keys
    addCategory(tr("Lock Keys"), {
        {"CapsLk", Qt::Key_CapsLock}, {"NumLk", Qt::Key_NumLock}
    });

    // Arrow Keys
    addCategory(tr("Arrow Keys"), {
        {"Up", Qt::Key_Up}, {"Down", Qt::Key_Down},
        {"Left", Qt::Key_Left}, {"Right", Qt::Key_Right}
    });
}

void VirtualKeyboardPage::populatePreview()
{
    previewList->clear();

    // Add modifier items (locked, not draggable)
    QStringList modifierNames = {"Ctrl", "Alt", "Shift", "Win"};
    QList<int> modifierKeyCodes = {Qt::Key_Control, Qt::Key_Alt, Qt::Key_Shift, Qt::Key_Meta};
    for (int i = 0; i < MODIFIER_COUNT; ++i) {
        QListWidgetItem *item = new QListWidgetItem(modifierNames[i]);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // Not draggable
        item->setBackground(QColor(200, 220, 240));
        item->setForeground(Qt::darkBlue);
        item->setData(Qt::UserRole, false);  // not separator
        item->setData(Qt::UserRole + 1, QVariant::fromValue(QList<int>{modifierKeyCodes[i]}));
        previewList->addItem(item);
    }

    // Add custom keys from CustomKeyManager
    CustomKeyManager &keyManager = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = keyManager.getKeys();

    for (const CustomKeyInfo &info : keys) {
        QListWidgetItem *item = new QListWidgetItem();

        if (info.isSeparator) {
            item->setText("--- Separator ---");
            item->setData(Qt::UserRole, true);  // is separator
            item->setData(Qt::UserRole + 1, QVariant::fromValue(QList<int>()));
            item->setBackground(QColor(230, 230, 230));
            item->setForeground(Qt::gray);
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        } else if (!info.specialCombo.isEmpty() && info.specialCombo == "ctrl_alt_del") {
            item->setText("Ctrl+Alt+Del");
            item->setData(Qt::UserRole, false);
            item->setData(Qt::UserRole + 1, QVariant::fromValue(QList<int>{Qt::Key_Control, Qt::Key_Alt, Qt::Key_Delete}));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        } else {
            item->setText(info.displayName.isEmpty() ? formatComboString(info.keyCodes) : info.displayName);
            item->setData(Qt::UserRole, false);  // not separator
            item->setData(Qt::UserRole + 1, QVariant::fromValue(info.keyCodes));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }

        previewList->addItem(item);
    }
}

void VirtualKeyboardPage::saveCurrentConfig()
{
    QList<CustomKeyInfo> keys;

    for (int i = MODIFIER_COUNT; i < previewList->count(); ++i) {
        QListWidgetItem *item = previewList->item(i);
        CustomKeyInfo info;

        bool isSeparator = item->data(Qt::UserRole).toBool();
        info.isSeparator = isSeparator;

        if (isSeparator) {
            info.displayName = "---";
        } else {
            info.keyCodes = item->data(Qt::UserRole + 1).value<QList<int>>();
            // Check for special combos
            if (info.keyCodes.contains(Qt::Key_Control) &&
                info.keyCodes.contains(Qt::Key_Alt) &&
                info.keyCodes.contains(Qt::Key_Delete)) {
                info.specialCombo = "ctrl_alt_del";
                info.displayName = "Ctrl+Alt+Del";
            } else {
                info.displayName = item->text();
            }
        }

        keys.append(info);
    }

    CustomKeyManager::getInstance().setKeys(keys);
    emit configChanged();
}

void VirtualKeyboardPage::refreshPresetCombo()
{
    presetCombo->clear();
    QStringList presets = CustomKeyManager::getInstance().getPresets();
    presetCombo->addItems(presets);

    // Select current preset
    QString current = CustomKeyManager::getInstance().getCurrentPresetName();
    int idx = presetCombo->findText(current);
    if (idx >= 0) presetCombo->setCurrentIndex(idx);
}

QString VirtualKeyboardPage::formatComboString(const QList<int>& keyCodes)
{
    QStringList parts;
    for (int code : keyCodes) {
        parts << CustomKeyManager::codeToKeyName(code);
    }
    return parts.join(" + ");
}

// ============================================================
// Action handlers
// ============================================================

void VirtualKeyboardPage::addKey()
{
    QString name = QInputDialog::getText(this, tr("Add Key"), tr("Key name (max 6 chars):"),
                                          QLineEdit::Normal, "", nullptr);
    if (name.isEmpty()) return;
    if (name.length() > 6) name = name.left(6);

    KeyComboCaptureDialog captureDlg(QList<int>(), this);
    if (captureDlg.exec() == QDialog::Accepted) {
        QList<int> keyCodes = captureDlg.getKeyCodes();
        if (keyCodes.isEmpty()) return;

        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, false);
        item->setData(Qt::UserRole + 1, QVariant::fromValue(keyCodes));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        previewList->addItem(item);
        saveCurrentConfig();
    }
}

void VirtualKeyboardPage::addSeparator()
{
    QListWidgetItem *item = new QListWidgetItem("--- Separator ---");
    item->setData(Qt::UserRole, true);
    item->setData(Qt::UserRole + 1, QVariant::fromValue(QList<int>()));
    item->setBackground(QColor(230, 230, 230));
    item->setForeground(Qt::gray);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    previewList->addItem(item);
    saveCurrentConfig();
}

void VirtualKeyboardPage::removeSelected()
{
    QListWidgetItem *item = previewList->currentItem();
    if (!item) return;
    int row = previewList->row(item);
    if (row < MODIFIER_COUNT) {
        QMessageBox::information(this, tr("Cannot Remove"), tr("Modifier keys cannot be removed."));
        return;
    }
    delete previewList->takeItem(row);
    saveCurrentConfig();
}

void VirtualKeyboardPage::moveSelectedUp()
{
    int row = previewList->currentRow();
    if (row <= MODIFIER_COUNT) return; // Can't move modifier items or first item

    QListWidgetItem *item = previewList->takeItem(row);
    previewList->insertItem(row - 1, item);
    previewList->setCurrentRow(row - 1);
    saveCurrentConfig();
}

void VirtualKeyboardPage::moveSelectedDown()
{
    int row = previewList->currentRow();
    if (row < MODIFIER_COUNT || row >= previewList->count() - 1) return;

    QListWidgetItem *item = previewList->takeItem(row);
    previewList->insertItem(row + 1, item);
    previewList->setCurrentRow(row + 1);
    saveCurrentConfig();
}

void VirtualKeyboardPage::resetToDefault()
{
    if (QMessageBox::question(this, tr("Reset"), tr("Reset to default configuration?"))
        != QMessageBox::Yes) return;

    CustomKeyManager::getInstance().loadPreset("Default");
    populatePreview();
    saveCurrentConfig();
    refreshPresetCombo();
}

void VirtualKeyboardPage::importJson()
{
    QString filePath = QFileDialog::getOpenFileName(this, tr("Import JSON"),
                                                     QString(), tr("JSON Files (*.json)"));
    if (filePath.isEmpty()) return;

    if (CustomKeyManager::getInstance().importFromJson(filePath)) {
        populatePreview();
        refreshPresetCombo();
        GlobalSetting::instance().setLastCustomKeyImportPath(filePath);
    } else {
        QMessageBox::warning(this, tr("Import Failed"), tr("Could not import from file."));
    }
}

void VirtualKeyboardPage::exportJson()
{
    QString filePath = QFileDialog::getSaveFileName(this, tr("Export JSON"),
                                                     QString(), tr("JSON Files (*.json)"));
    if (filePath.isEmpty()) return;

    if (!CustomKeyManager::getInstance().exportToJson(filePath)) {
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not export to file."));
    }
}

void VirtualKeyboardPage::loadPreset()
{
    QString preset = presetCombo->currentText();
    if (preset.isEmpty()) return;

    if (CustomKeyManager::getInstance().loadPreset(preset)) {
        populatePreview();
    } else {
        QMessageBox::warning(this, tr("Load Failed"), tr("Could not load preset."));
    }
}

void VirtualKeyboardPage::savePreset()
{
    QString preset = presetCombo->currentText();
    if (preset.isEmpty()) {
        preset = QInputDialog::getText(this, tr("Save Preset"), tr("Preset name:"));
        if (preset.isEmpty()) return;
    }

    if (CustomKeyManager::getInstance().savePreset(preset)) {
        refreshPresetCombo();
    } else {
        QMessageBox::warning(this, tr("Save Failed"), tr("Could not save preset."));
    }
}

void VirtualKeyboardPage::deletePreset()
{
    QString preset = presetCombo->currentText();
    if (preset.isEmpty() || preset == "Default") return;

    if (QMessageBox::question(this, tr("Delete Preset"),
                               tr("Delete preset '%1'?").arg(preset)) == QMessageBox::Yes) {
        CustomKeyManager::getInstance().deletePreset(preset);
        refreshPresetCombo();
    }
}

void VirtualKeyboardPage::handleExternalDrop(int index, const QString& displayName, const QList<int>& keyCodes)
{
    // Ensure index is after modifier items
    if (index < MODIFIER_COUNT) index = MODIFIER_COUNT;

    QListWidgetItem *item = new QListWidgetItem(displayName);
    item->setData(Qt::UserRole, false);
    item->setData(Qt::UserRole + 1, QVariant::fromValue(keyCodes));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    previewList->insertItem(index, item);
    previewList->setCurrentRow(index);
    saveCurrentConfig();
}

void VirtualKeyboardPage::refreshPreview()
{
    populatePreview();
}
