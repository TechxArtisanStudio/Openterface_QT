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

#include "advancedsettingsdialog.h"
#include "mcppage.h"
#include "firmwarepage.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QDebug>


AdvancedSettingsDialog::AdvancedSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , settingTree(new QTreeWidget(this))
    , stackedWidget(new QStackedWidget(this))
    , firmwarePage(new FirmwarePage(this))
    , mcpPage(new McpPage(this))
    , buttonWidget(new QWidget(this))
    , m_currentPageIndex(-1)
    , m_changingPage(false)
    , m_pageChangeTimer(new QTimer(this))
{
    createSettingTree();
    createPages();
    createButtons();
    createLayout();

    resize(800, 600);

    QList<int> sizes;
    int totalWidth = width();
    sizes << totalWidth * 4 / 27 << totalWidth * 23 / 27;
    splitter->setSizes(sizes);

    setWindowTitle(tr("Advanced Settings"));

    firmwarePage->updateVersionDisplay();
    mcpPage->initMcpSettings();

    connect(settingTree, &QTreeWidget::currentItemChanged, this, &AdvancedSettingsDialog::changePage);

    connect(m_pageChangeTimer, &QTimer::timeout, this, [this]() {
        m_changingPage = false;
        m_pageChangeTimer->stop();
    });

    if (settingTree->topLevelItemCount() > 0) {
        settingTree->setCurrentItem(settingTree->topLevelItem(0));
        m_currentPageIndex = 0;
        stackedWidget->setCurrentIndex(0);
    }
}

AdvancedSettingsDialog::~AdvancedSettingsDialog()
{
}

void AdvancedSettingsDialog::createSettingTree() {
    settingTree->setColumnCount(1);
    settingTree->setHeaderHidden(true);
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);
    settingTree->setRootIsDecorated(false);

    QStringList names = {tr("Firmware"), tr("MCP")};
    for (const QString &name : names) {
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
}

void AdvancedSettingsDialog::createPages() {
    auto addScrollablePage = [this](QWidget *page) {
        QScrollArea *scrollArea = new QScrollArea(this);
        scrollArea->setWidget(page);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        stackedWidget->addWidget(scrollArea);
    };

    addScrollablePage(firmwarePage);
    addScrollablePage(mcpPage);
}

void AdvancedSettingsDialog::createButtons() {
    QPushButton *okButton = new QPushButton(tr("OK"));
    QPushButton *applyButton = new QPushButton(tr("Apply"));
    QPushButton *cancelButton = new QPushButton(tr("Cancel"));

    okButton->setFixedSize(80, 30);
    applyButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);

    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);

    connect(okButton, &QPushButton::clicked, this, &AdvancedSettingsDialog::handleOkButton);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &AdvancedSettingsDialog::applyAccordingPage);
}

void AdvancedSettingsDialog::createLayout() {
    splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(settingTree);
    splitter->addWidget(stackedWidget);
    splitter->setStretchFactor(1, 1);
    splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(buttonWidget);

    setLayout(mainLayout);
}

void AdvancedSettingsDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    if (m_changingPage) {
        return;
    }

    if (!current) {
        current = previous;
        if (!current) return;
    }

    QString itemText = current->text(0);
    int newPageIndex = -1;

    if (itemText == tr("Firmware")) {
        newPageIndex = 0;
    } else if (itemText == tr("MCP")) {
        newPageIndex = 1;
    }

    if (newPageIndex != -1 && newPageIndex != m_currentPageIndex) {
        m_changingPage = true;

        stackedWidget->setCurrentIndex(newPageIndex);
        m_currentPageIndex = newPageIndex;

        m_pageChangeTimer->start(200);
    }
}

void AdvancedSettingsDialog::applyAccordingPage() {
    int currentPageIndex = stackedWidget->currentIndex();
    switch (currentPageIndex) {
    case 0: // Firmware - no apply action needed
        break;
    case 1:
        mcpPage->applyMcpSettings();
        break;
    default:
        break;
    }
}

void AdvancedSettingsDialog::handleOkButton() {
    mcpPage->applyMcpSettings();
    accept();
}

McpPage* AdvancedSettingsDialog::getMcpPage() {
    return mcpPage;
}

FirmwarePage* AdvancedSettingsDialog::getFirmwarePage() {
    return firmwarePage;
}
