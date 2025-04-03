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
#include <QDebug>
#include <QLoggingCategory>
#include <QSettings>
#include <QElapsedTimer>
#include <qtimer.h>
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
    , targetControlPage(new TargetControlPage(this))
    , buttonWidget(new QWidget(this))

{
    ui->setupUi(this);
    createSettingTree();
    createPages();
    createButtons();
    createLayout();

    setWindowTitle(tr("Preferences"));
    // loadLogSettings();
    logPage->initLogSettings();
    videoPage->initVideoSettings();
    targetControlPage->initHardwareSetting();
    // Connect the tree widget's currentItemChanged signal to a slot
    connect(settingTree, &QTreeWidget::currentItemChanged, this, &SettingDialog::changePage);
}

SettingDialog::~SettingDialog()
{
    delete ui;
}

void SettingDialog::createSettingTree() {
    // qDebug() << "creating setting Tree";
    settingTree->setColumnCount(1);
    // settingTree->setHeaderLabels(QStringList(tr("general")));
    settingTree->setHeaderHidden(true);
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);

    settingTree->setMaximumSize(QSize(120, 1000));
    settingTree->setRootIsDecorated(false);

    // QStringList names = {"Log"};
    QStringList names = {tr("General"), tr("Video"), tr("Audio"), tr("Target Control")};
    for (const QString &name : names) {     // add item to setting tree
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
}


void SettingDialog::createPages() {
    // Add pages to the stacked widget
    stackedWidget->addWidget(logPage);
    stackedWidget->addWidget(videoPage);
    stackedWidget->addWidget(audioPage);
    stackedWidget->addWidget(targetControlPage);
}

void SettingDialog::createButtons(){
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

    connect(okButton, &QPushButton::clicked, this, &SettingDialog::handleOkButton);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &SettingDialog::applyAccrodingPage);
}

void SettingDialog::createLayout() {
    qDebug() << "createLayout";
    QHBoxLayout *selectLayout = new QHBoxLayout;
    selectLayout->addWidget(settingTree);
    selectLayout->addWidget(stackedWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(selectLayout);
    mainLayout->addWidget(buttonWidget);

    setLayout(mainLayout);
}

void SettingDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous) {

    if (!current) {
        current = previous;
        if (!current) return;
    }
    
    QString itemText = current->text(0);

    if (itemText == tr("General")) {
        stackedWidget->setCurrentIndex(0);
    } else if (itemText == tr("Video")) {
        stackedWidget->setCurrentIndex(1);
    } else if (itemText == tr("Audio")) {
        stackedWidget->setCurrentIndex(2);
    } else if (itemText == tr("Target Control")) {
        stackedWidget->setCurrentIndex(3);
    }

}

void SettingDialog::applyAccrodingPage(){
    int currentPageIndex = stackedWidget->currentIndex();
    switch (currentPageIndex)
    {
    // sequence Log Video Audio
    case 0:
        logPage->applyLogsettings();
        break;
    case 1:
        videoPage->applyVideoSettings();
        break;
    case 2:
        break;
    case 3:
        targetControlPage->applyHardwareSetting();
        break;
    default:
        break;
    }
}

void SettingDialog::handleOkButton() {
    logPage->applyLogsettings();
    videoPage->applyVideoSettings();
    targetControlPage->applyHardwareSetting();
    accept();
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
