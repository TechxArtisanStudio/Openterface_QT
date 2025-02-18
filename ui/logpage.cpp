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

#include "logpage.h"
#include <QFileDialog>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSettings>
#include <QLoggingCategory>
#include "global.h"
#include "globalsetting.h"
#include "loghandler.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>

LogPage::LogPage(QWidget *parent) : QWidget(parent)
{
    // Constructor implementation
    setupUI();
    // initLogSettings();
}

void LogPage::setupUI()
{
    // UI setup implementation
    coreCheckBox = new QCheckBox("Core");
    serialCheckBox = new QCheckBox("Serial");
    uiCheckBox = new QCheckBox("User Interface");
    hostCheckBox = new QCheckBox("Host");
    storeLogCheckBox = new QCheckBox("Enable file logging");
    logFilePathLineEdit = new QLineEdit(this);
    browseButton = new QPushButton("Browse");

    coreCheckBox->setObjectName("core");
    serialCheckBox->setObjectName("serial");
    uiCheckBox->setObjectName("ui");
    hostCheckBox->setObjectName("host");
    logFilePathLineEdit->setObjectName("logFilePathLineEdit");
    browseButton->setObjectName("browseButton");
    storeLogCheckBox->setObjectName("storeLogCheckBox");

    QHBoxLayout *logCheckboxLayout = new QHBoxLayout();
    logCheckboxLayout->addWidget(coreCheckBox);
    logCheckboxLayout->addWidget(serialCheckBox);
    logCheckboxLayout->addWidget(uiCheckBox);
    logCheckboxLayout->addWidget(hostCheckBox);
    
    QHBoxLayout *logFilePathLayout = new QHBoxLayout();
    logFilePathLayout->addWidget(logFilePathLineEdit);
    logFilePathLayout->addWidget(browseButton);
    
    QLabel *logLabel = new QLabel("<span style='font-weight: bold;'>General log setting</span>");
    logLabel->setTextFormat(Qt::RichText);
    logLabel->setStyleSheet(bigLabelFontSize);

    QLabel *logDescription = new QLabel("Check the check box to see the corresponding log in the QT console.");
    logDescription->setStyleSheet(commentsFontSize);

    connect(browseButton, &QPushButton::clicked, this, &LogPage::browseLogPath);

    QVBoxLayout *logLayout = new QVBoxLayout(this);
    logLayout->addWidget(logLabel);
    logLayout->addWidget(logDescription);
    logLayout->addLayout(logCheckboxLayout);
    logLayout->addWidget(storeLogCheckBox);
    logLayout->addLayout(logFilePathLayout);
    logLayout->addStretch();
    
}

void LogPage::browseLogPath()
{
    // Implement the browse log path functionality
    QString exeDir = QCoreApplication::applicationDirPath();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Log Directory"),
                                                    exeDir,
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        QString logPath = dir + "/openterface_log.txt";
        logFilePathLineEdit->setText(dir);
        QFile file(logPath);
        if (!file.exists()) {
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
                qDebug() << "Created new log file:" << logPath;
            } else {
                qWarning() << "Failed to create log file:" << logPath;
            }
        }
        logFilePathLineEdit->setText(logPath);
    }
}

void LogPage::initLogSettings(){
    qDebug() << "initLogSettings";
    QSettings settings("Techxartisan", "Openterface");
    QCheckBox *coreCheckBox = findChild<QCheckBox*>("core");
    QCheckBox *serialCheckBox = findChild<QCheckBox*>("serial");
    QCheckBox *uiCheckBox = findChild<QCheckBox*>("ui");
    QCheckBox *hostCheckBox = findChild<QCheckBox*>("host");
    QCheckBox *storeLogCheckBox = findChild<QCheckBox*>("storeLogCheckBox");
    QLineEdit *logFilePathLineEdit = findChild<QLineEdit*>("logFilePathLineEdit");
    

    coreCheckBox->setChecked(settings.value("log/core", true).toBool());

    serialCheckBox->setChecked(settings.value("log/serial", true).toBool());

    uiCheckBox->setChecked(settings.value("log/ui", true).toBool());

    hostCheckBox->setChecked(settings.value("log/host", true).toBool());

    storeLogCheckBox->setChecked(settings.value("log/storeLog", false).toBool());

    logFilePathLineEdit->setText(settings.value("log/logFilePath", "").toString());

}

void LogPage::applyLogsettings() {

    // QSettings settings("Techxartisan", "Openterface");

    QCheckBox *coreCheckBox = findChild<QCheckBox*>("core");
    QCheckBox *serialCheckBox = findChild<QCheckBox*>("serial");
    QCheckBox *uiCheckBox = findChild<QCheckBox*>("ui");
    QCheckBox *hostCheckBox = findChild<QCheckBox*>("host");
    QCheckBox *storeLogCheckBox = findChild<QCheckBox*>("storeLogCheckBox");
    QLineEdit *logFilePathLineEdit = findChild<QLineEdit*>("logFilePathLineEdit");
    bool core =  coreCheckBox->isChecked();
    bool host = hostCheckBox->isChecked();
    bool serial = serialCheckBox->isChecked();
    bool ui = uiCheckBox->isChecked();
    bool storeLog = storeLogCheckBox->isChecked();
    QString logFilePath = logFilePathLineEdit->text();
    // set the log filter value by check box
    QString logFilter = "";

    logFilter += core ? "opf.core.*=true\n" : "opf.core.*=false\n";
    logFilter += ui ? "opf.ui.*=true\n" : "opf.ui.*=false\n";
    logFilter += host ? "opf.host.*=true\n" : "opf.host.*=false\n";
    logFilter += serial ? "opf.core.serial=true\n" : "opf.core.serial=false\n";

    QLoggingCategory::setFilterRules(logFilter);
    // save the filter settings

    GlobalSetting::instance().setLogSettings(core, serial, ui, host);
    GlobalSetting::instance().setLogStoreSettings(storeLog, logFilePath);
    LogHandler::instance().enableLogStore();
}