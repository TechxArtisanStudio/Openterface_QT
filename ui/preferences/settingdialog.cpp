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
    , m_currentPageIndex(-1)

{
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
    // Connect the tree widget's currentItemChanged signal to a slot
    connect(settingTree, &QTreeWidget::currentItemChanged, this, &SettingDialog::changePage);

    // Set initial page to General (index 0)
    if (settingTree->topLevelItemCount() > 0) {
        settingTree->setCurrentItem(settingTree->topLevelItem(0));
        m_currentPageIndex = 0;
        stackedWidget->setCurrentIndex(0);
    }
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

    settingTree->setRootIsDecorated(false);

    // QStringList names = {"Log"};
    QStringList names = {
        tr("General"),              // 0
        tr("Video"),                // 1
        tr("Audio"),                // 2
        tr("Target Control"),       // 3
        tr("MCP"),                  // 4
        tr("Video Firmware"),       // 5
        tr("Control Chip Firmware"),// 6
        tr("EDID Configuration"),   // 7
        tr("Virtual Keyboard")      // 8
    };
    for (const QString &name : names) {     // add item to setting tree
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
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
    addScrollablePage(mcpPage);
    addScrollablePage(firmwarePage);
    addScrollablePage(controlChipFirmwarePage);
    addScrollablePage(edidConfigPage);
    addScrollablePage(virtualKeyboardPage);
}

void SettingDialog::createLayout() {
    qDebug() << "createLayout";
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

    if (itemText == tr("General")) newPageIndex = 0;
    else if (itemText == tr("Video")) newPageIndex = 1;
    else if (itemText == tr("Audio")) newPageIndex = 2;
    else if (itemText == tr("Target Control")) newPageIndex = 3;
    else if (itemText == tr("MCP")) newPageIndex = 4;
    else if (itemText == tr("Video Firmware")) newPageIndex = 5;
    else if (itemText == tr("Control Chip Firmware")) newPageIndex = 6;
    else if (itemText == tr("EDID Configuration")) newPageIndex = 7;
    else if (itemText == tr("Virtual Keyboard")) newPageIndex = 8;

    // Only switch page if it's different from the current page
    if (newPageIndex != -1 && newPageIndex != m_currentPageIndex) {
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
    for (int i = 0; i < settingTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = settingTree->topLevelItem(i);
        if (item->text(0) == pageName) {
            settingTree->setCurrentItem(item);
            return;
        }
    }
}
