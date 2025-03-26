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


#include "serialportdebugdialog.h"
#include "serial/SerialPortManager.h"
#include "ui/globalsetting.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QByteArray>
#include <QCoreApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QCloseEvent>
#include <QDateTime>
#include <QSettings>
#include <QTextCursor>

// Define filter settings
const SerialPortDebugDialog::FilterSettings SerialPortDebugDialog::FILTERS[] = {
    {"ChipInfoFilter", "Chip info filter", 0x81, 0x01},
    {"keyboardPressFilter", "Keyboard filter", 0x82, 0x02},
    {"mideaKeyboardFilter", "Media keyboard filter", 0x83, 0x03}, 
    {"mouseMoveABSFilter", "Mouse absolute filter", 0x84, 0x04},
    {"mouseMoveRELFilter", "Mouse relative filter", 0x85, 0x05},
    {"HIDFilter", "HID filter", 0x87, 0x06}
};

SerialPortDebugDialog::SerialPortDebugDialog(QWidget *parent)
    : QDialog(parent)
    , textEdit(new QTextEdit(this))
    , debugButtonWidget(new QWidget(this))
    , filterCheckboxWidget(new QWidget(this))
{
    setWindowTitle(tr("Serial Port Debug"));
    resize(640, 480);
    
    createDebugButtonWidget();
    createFilterCheckBox();
    
    if (auto* serialPortManager = &SerialPortManager::getInstance()) {
        connect(serialPortManager, &SerialPortManager::dataSent,
                this, &SerialPortDebugDialog::getSentDataAndInsertText);
        connect(serialPortManager, &SerialPortManager::dataReceived,
                this, &SerialPortDebugDialog::getRecvDataAndInsertText);
    }

    createLayout();
    loadSettings();
}

void SerialPortDebugDialog::createFilterCheckBox()
{
    QGridLayout *gridLayout = new QGridLayout(filterCheckboxWidget);
    
    for (long unsigned int i = 0; i < sizeof(FILTERS)/sizeof(FILTERS[0]); i++) {
        auto* checkbox = new QCheckBox(FILTERS[i].label);
        checkbox->setObjectName(FILTERS[i].name);
        gridLayout->addWidget(checkbox, i/3, i%3, Qt::AlignLeft);
    }
}

void SerialPortDebugDialog::createDebugButtonWidget(){
    QPushButton *clearButton = new QPushButton(tr("Clear"));
    QPushButton *closeButton = new QPushButton(tr("Close"));
    closeButton->setFixedSize(90,30);
    clearButton->setFixedSize(90,30);
    QHBoxLayout *debugButtonLayout = new QHBoxLayout(debugButtonWidget);
    debugButtonLayout->addStretch();
    debugButtonLayout->addWidget(clearButton);
    debugButtonLayout->addWidget(closeButton);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(clearButton, &QPushButton::clicked, textEdit, &QTextEdit::clear);
}

void SerialPortDebugDialog::createLayout(){
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(filterCheckboxWidget);
    mainLayout->addWidget(textEdit);
    mainLayout->addWidget(debugButtonWidget);
    setLayout(mainLayout);
}

void SerialPortDebugDialog::saveSettings()
{
    QSettings settings("Techxartisan", "Openterface");
    for (const auto& filter : FILTERS) {
        if (auto* checkbox = filterCheckboxWidget->findChild<QCheckBox*>(filter.name)) {
            settings.setValue(QString("filter/%1").arg(filter.name), checkbox->isChecked());
        }
    }
}

void SerialPortDebugDialog::loadSettings()
{
    // Get current filter settings from GlobalSetting
    bool chipInfo, keyboardPress, mediaKeyboard, mouseAbs, mouseRel, hid;
    GlobalSetting::instance().getFilterSettings(
        chipInfo, keyboardPress, mediaKeyboard, mouseAbs, mouseRel, hid
    );

    // Map settings to checkboxes
    QMap<QString, bool> filterStates{
        {"ChipInfoFilter", chipInfo},
        {"keyboardPressFilter", keyboardPress},
        {"mideaKeyboardFilter", mediaKeyboard},
        {"mouseMoveABSFilter", mouseAbs},
        {"mouseMoveRELFilter", mouseRel},
        {"HIDFilter", hid}
    };

    // Apply settings to checkboxes
    for (const auto& filter : FILTERS) {
        if (auto* checkbox = filterCheckboxWidget->findChild<QCheckBox*>(filter.name)) {
            checkbox->setChecked(filterStates.value(filter.name, true));
        }
    }
}

void SerialPortDebugDialog::handleSerialData(const QByteArray &data, bool isReceived)
{
    if (data.size() < 4) return;

    // Update GlobalSetting with current checkbox states before processing data
    QMap<QString, bool> currentStates;
    for (const auto& filter : FILTERS) {
        if (auto* checkbox = filterCheckboxWidget->findChild<QCheckBox*>(filter.name)) {
            currentStates[filter.name] = checkbox->isChecked();
        }
    }

    GlobalSetting::instance().setFilterSettings(
        currentStates["ChipInfoFilter"],
        currentStates["keyboardPressFilter"],
        currentStates["mideaKeyboardFilter"],
        currentStates["mouseMoveABSFilter"],
        currentStates["mouseMoveRELFilter"],
        currentStates["HIDFilter"]
    );

    unsigned char code = static_cast<unsigned char>(data[3]);
    if (!shouldShowMessage(code)) return;

    QString dataString = formatHexData(data.toHex().toUpper());
    QString timestamp = QDateTime::currentDateTime().toString("MM-dd hh:mm:ss.zzz");
    QString direction = isReceived ? " << " : " >> ";
    
    textEdit->moveCursor(QTextCursor::End);
    textEdit->insertPlainText(timestamp + " " + getCommandType(code) + direction + dataString + "\n");
    textEdit->ensureCursorVisible();
}

bool SerialPortDebugDialog::shouldShowMessage(unsigned char code) const 
{
    for (const auto& filter : FILTERS) {
        if ((code == filter.recvCode || code == filter.sendCode) && 
            filterCheckboxWidget->findChild<QCheckBox*>(filter.name)->isChecked()) {
            return true;
        }
    }
    return false;
}

QString SerialPortDebugDialog::getCommandType(unsigned char code) const
{
    for (const auto& filter : FILTERS) {
        if (code == filter.recvCode || code == filter.sendCode) {
            return QString(filter.label);
        }
    }
    return "Unknown";
}

QString SerialPortDebugDialog::formatHexData(const QString &hexString)
{
    QString result;
    for (int i = 0; i < hexString.length(); i += 2) {
        if (!result.isEmpty()) result += " ";
        result += hexString.mid(i, 2);
    }
    return result;
}