/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "settingdialog.h"
#include "ui_settingdialog.h"
#include "logpage.h"
#include "targetcontrolpage.h"
#include "videopage.h"
#include "firmwarepage.h"
#include "controlchipfirmwarepage.h"
#include "mcppage.h"
#include "edidconfigpage.h"
#include "../customkey/virtualkeyboardpage.h"
#include "../chat/ToolsSettingsPage.h"
#include "host/cameramanager.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRegularExpression>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <QScrollArea>
#include <QSplitter>
#include <QDebug>
#include <QLoggingCategory>
#include <QSettings>
#include <QElapsedTimer>
#include <QList>
#include <QSerialPortInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QCloseEvent>
#include <QByteArray>

SettingDialog::SettingDialog(CameraManager *cameraManager, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingDialog)
    , m_cameraManager(cameraManager)
    , settingTree(new QTreeWidget(this))
    , stackedWidget(new QStackedWidget(this))
    , logPage(new LogPage(this))
    , audioPage(new AudioPage(this))
    , videoPage(new VideoPage(cameraManager, this))
    , mcpPage(new McpPage(this))
    , targetControlPage(new TargetControlPage(this))
    , firmwarePage(new FirmwarePage(this))
    , controlChipFirmwarePage(new ControlChipFirmwarePage(this))
    , edidConfigPage(new EdidConfigPage(this))
    , virtualKeyboardPage(new VirtualKeyboardPage(this))
    , chatSettingsPage(new ChatSettingsPage(this))
    , toolsSettingsPage(new ToolsSettingsPage(this))
    , m_currentPageIndex(-1)

{
    // Initialize the list of settings pages for dirty-checking
    m_pages << logPage << videoPage << qobject_cast<PreferencePageBase*>(audioPage)
            << targetControlPage << mcpPage
            << qobject_cast<PreferencePageBase*>(toolsSettingsPage)
            << qobject_cast<PreferencePageBase*>(chatSettingsPage);

    ui->setupUi(this);
    createSettingTree();
    createPages();
    createLayout();

    // Set dialog size and allow free resizing
    resize(800, 600);

    // Set initial splitter sizes: 4/27 tree (~15%), 23/27 content
    QList<int> sizes;
    int totalWidth = width();
    sizes << totalWidth * 4 / 27 << totalWidth * 23 / 27;
    splitter->setSizes(sizes);

    setWindowTitle(tr("Preferences"));
    logPage->initLogSettings();
    videoPage->initVideoSettings();
    targetControlPage->initHardwareSetting();
    mcpPage->initMcpSettings();

    // Force clear dirty state after all init - some widget signals may fire during init
    for (auto *page : m_pages) {
        if (page) page->clearDirty();
    }

    // Set initial page to General (index 0) - before connecting signal to avoid spurious changePage
    if (settingTree->topLevelItemCount() > 0) {
        m_currentPageIndex = 0;
        stackedWidget->setCurrentIndex(0);
        settingTree->setCurrentItem(settingTree->topLevelItem(0));
    }

    // Connect signal AFTER all init is complete to avoid false unsaved-changes triggers
    connect(settingTree, &QTreeWidget::currentItemChanged, this, &SettingDialog::changePage);
}

SettingDialog::~SettingDialog()
{
    delete ui;
}

void SettingDialog::createSettingTree() {
    // qDebug() << "creating setting Tree";
    settingTree->setColumnCount(1);
    settingTree->setHeaderHidden(true);
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);

    // Allow expanding/collapsing of parent items
    settingTree->setRootIsDecorated(true);

    // Top-level items (flat structure)
    QStringList topLevelNames = {
        tr("General"),              // 0
        tr("Video"),                // 1
        tr("Audio"),                // 2
        tr("Target Control"),       // 3
        tr("Video Firmware"),       // 4
        tr("Control Chip Firmware"),// 5
        tr("EDID Configuration"),   // 6
        tr("Virtual Keyboard"),     // 7
    };

    for (const QString &name : topLevelNames) {
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }

    // AI category with sub-items (hierarchical structure)
    QTreeWidgetItem *aiParent = new QTreeWidgetItem(settingTree);
    aiParent->setText(0, tr("AI"));
    aiParent->setFlags(aiParent->flags() | Qt::ItemIsAutoTristate);

    // AI sub-items
    QStringList aiSubItems = {
        tr("MCP"),      // 8
        tr("Tools"),    // 9
        tr("Chat")      // 10
    };

    for (const QString &name : aiSubItems) {
        QTreeWidgetItem *child = new QTreeWidgetItem(aiParent);
        child->setText(0, name);
    }

    // Expand the AI category by default
    aiParent->setExpanded(true);
}

void SettingDialog::createPages() {
    // Wrap each page in a QScrollArea so content can scroll both vertically and horizontally
    auto addScrollablePage = [this](QWidget *page) {
        QScrollArea *scrollArea = new QScrollArea(this);
        scrollArea->setWidget(page);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        stackedWidget->addWidget(scrollArea);
    };

    addScrollablePage(logPage);
    addScrollablePage(videoPage);
    addScrollablePage(audioPage);
    addScrollablePage(targetControlPage);
    addScrollablePage(firmwarePage);
    addScrollablePage(controlChipFirmwarePage);
    addScrollablePage(edidConfigPage);
    addScrollablePage(virtualKeyboardPage);
    addScrollablePage(mcpPage);
    addScrollablePage(toolsSettingsPage);
    addScrollablePage(chatSettingsPage);
}

void SettingDialog::createLayout() {
    splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(settingTree);
    splitter->addWidget(stackedWidget);
    splitter->setStretchFactor(1, 1);
    splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter, 1);

    setLayout(mainLayout);
}

void SettingDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous) {

    if (!current) {
        current = previous;
        if (!current) return;
    }

    QString itemText = current->text(0);
    int newPageIndex = -1;

    // Top-level items
    if (itemText == tr("General")) newPageIndex = 0;
    else if (itemText == tr("Video")) newPageIndex = 1;
    else if (itemText == tr("Audio")) newPageIndex = 2;
    else if (itemText == tr("Target Control")) newPageIndex = 3;
    else if (itemText == tr("Video Firmware")) newPageIndex = 4;
    else if (itemText == tr("Control Chip Firmware")) newPageIndex = 5;
    else if (itemText == tr("EDID Configuration")) newPageIndex = 6;
    else if (itemText == tr("Virtual Keyboard")) newPageIndex = 7;
    // AI sub-items
    else if (itemText == tr("MCP")) newPageIndex = 8;
    else if (itemText == tr("Tools")) newPageIndex = 9;
    else if (itemText == tr("Chat")) newPageIndex = 10;
    else if (itemText == tr("AI")) {
        // If user clicks on the parent "AI" item, select the first child (MCP)
        QTreeWidgetItem *firstChild = current->child(0);
        if (firstChild) {
            settingTree->setCurrentItem(firstChild);
            return;
        }
    }

    // Only switch page if it is different from the current page
    if (newPageIndex != -1 && newPageIndex != m_currentPageIndex) {
        // Check for unsaved changes before switching
        if (hasUnsavedChanges()) {
            auto result = promptSaveDiscardCancel();
            if (result == QMessageBox::Save) {
                applyAllDirtyPages();
            } else if (result == QMessageBox::Cancel) {
                // Restore previous selection, block signals to avoid recursion
                settingTree->blockSignals(true);
                if (previous) settingTree->setCurrentItem(previous);
                settingTree->blockSignals(false);
                return;
            }
            // Discard: proceed without saving
        }
        stackedWidget->setCurrentIndex(newPageIndex);
        m_currentPageIndex = newPageIndex;
    }

}

TargetControlPage* SettingDialog::getTargetControlPage() {
    return targetControlPage;
}

VideoPage* SettingDialog::getVideoPage() {
    return videoPage;
}

LogPage* SettingDialog::getLogPage() {
    return logPage;
}

McpPage* SettingDialog::getMcpPage() {
    return mcpPage;
}

FirmwarePage* SettingDialog::getFirmwarePage() {
    return firmwarePage;
}

VirtualKeyboardPage* SettingDialog::getVirtualKeyboardPage() {
    return virtualKeyboardPage;
}

void SettingDialog::selectPage(const QString& pageName) {
    // Search in top-level items
    for (int i = 0; i < settingTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = settingTree->topLevelItem(i);
        if (item->text(0) == pageName) {
            settingTree->setCurrentItem(item);
            return;
        }
        // Search in child items
        for (int j = 0; j < item->childCount(); ++j) {
            QTreeWidgetItem *child = item->child(j);
            if (child->text(0) == pageName) {
                settingTree->setCurrentItem(child);
                return;
            }
        }
    }
}

bool SettingDialog::hasUnsavedChanges() const
{
    for (auto *page : m_pages) {
        if (page && page->isDirty()) return true;
    }
    return false;
}

QStringList SettingDialog::dirtyPageNames() const
{
    QStringList names;
    // Map m_pages indices to names (m_pages only contains PreferencePageBase pages)
    // m_pages order: logPage, videoPage, audioPage, targetControlPage, mcpPage, toolsSettingsPage, chatSettingsPage
    QStringList pageNames = {
        tr("General"),              // 0: logPage
        tr("Video"),                // 1: videoPage
        tr("Audio"),                // 2: audioPage
        tr("Target Control"),       // 3: targetControlPage
        tr("MCP"),                  // 4: mcpPage
        tr("Tools"),                // 5: toolsSettingsPage
        tr("Chat")                  // 6: chatSettingsPage
    };

    for (int i = 0; i < m_pages.size() && i < pageNames.size(); ++i) {
        if (m_pages[i] && m_pages[i]->isDirty()) {
            names << pageNames[i];
        }
    }
    return names;
}

void SettingDialog::applyAllDirtyPages()
{
    for (auto *page : m_pages) {
        if (page && page->isDirty()) {
            page->applySettings();
            page->captureSnapshot();
            page->clearDirty();
        }
    }
}

QMessageBox::StandardButton SettingDialog::promptSaveDiscardCancel()
{
    QStringList names = dirtyPageNames();
    QString detail;
    if (!names.isEmpty()) {
        detail = tr("Modified pages: %1").arg(names.join(", "));
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Unsaved Changes"));
    msgBox.setText(tr("You have unsaved changes."));
    msgBox.setInformativeText(tr("Do you want to save your changes?"));
    if (!detail.isEmpty()) {
        msgBox.setDetailedText(detail);
    }
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    return static_cast<QMessageBox::StandardButton>(msgBox.exec());
}

void SettingDialog::closeEvent(QCloseEvent *event)
{
    if (hasUnsavedChanges()) {
        auto result = promptSaveDiscardCancel();
        if (result == QMessageBox::Save) {
            applyAllDirtyPages();
            reject();  // emits finished signal so MainWindow can clean up
        } else if (result == QMessageBox::Cancel) {
            event->ignore();
            return;
        } else {
            // Discard
            reject();
        }
    } else {
        reject();
    }
}

