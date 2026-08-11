#ifndef VIRTUALKEYBOARDPAGE_H
#define VIRTUALKEYBOARDPAGE_H

#include <QWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include "customkeymanager.h"

// Custom QTreeWidget for the available keys panel (drag source)
class AvailableKeysTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit AvailableKeysTree(QWidget *parent = nullptr);
protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
};

// Custom QListWidget for the toolbar preview (drop target + internal reorder)
class ToolbarPreviewList : public QListWidget {
    Q_OBJECT
public:
    explicit ToolbarPreviewList(QWidget *parent = nullptr);
signals:
    void externalDrop(int index, const QString& displayName, const QList<int>& keyCodes);
    void itemsReordered();
protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    Qt::DropActions supportedDropActions() const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    QPoint m_dragStartPos;
    QListWidgetItem *m_dragStartItem;
};

// Main configuration page embedded in Advanced Settings
class VirtualKeyboardPage : public QWidget {
    Q_OBJECT
public:
    explicit VirtualKeyboardPage(QWidget *parent = nullptr);

    void refreshPreview();

signals:
    void configChanged();

private:
    void setupUI();
    void populateAvailableKeys();
    void populatePreview();
    void saveCurrentConfig();
    void refreshPresetCombo();
    QString formatComboString(const QList<int>& keyCodes);

    // Actions
    void addKey();
    void addSeparator();
    void removeSelected();
    void moveSelectedUp();
    void moveSelectedDown();
    void resetToDefault();
    void importJson();
    void exportJson();
    void loadPreset();
    void savePreset();
    void deletePreset();

    // Drag-and-drop handler
    void handleExternalDrop(int index, const QString& displayName, const QList<int>& keyCodes);

    // UI widgets
    AvailableKeysTree *availableKeysTree;
    ToolbarPreviewList *previewList;
    QPushButton *addKeyBtn;
    QPushButton *addSepBtn;
    QPushButton *removeBtn;
    QPushButton *moveUpBtn;
    QPushButton *moveDownBtn;
    QPushButton *resetBtn;
    QPushButton *importBtn;
    QPushButton *exportBtn;
    QComboBox *presetCombo;
    QPushButton *loadPresetBtn;
    QPushButton *savePresetBtn;
    QPushButton *deletePresetBtn;

    static const int MODIFIER_COUNT = 4;
};

#endif // VIRTUALKEYBOARDPAGE_H
