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

#include "hardwarepage.h"
#include "globalsetting.h"
#include "serial/SerialPortManager.h"
#include <QSettings>

HardwarePage::HardwarePage(QWidget *parent) : QWidget(parent)
{
    setupUI();
}

void HardwarePage::setupUI()
{
    // UI setup implementation
    hardwareLabel = new QLabel(
        QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Target control setting")));
    hardwareLabel->setStyleSheet(bigLabelFontSize);

    QLabel *VIDPIDLabel = new QLabel(tr("Custom target USB Composite Device VID and PID:"));
    QLabel *USBDescriptor = new QLabel(tr("Custom target USB descriptors: "));
    QLabel *VID = new QLabel(tr("VID: "));
    QLabel *PID = new QLabel(tr("PID: "));
    QCheckBox *VIDCheckBox = new QCheckBox(tr("Custom vendor descriptor:"));
    QCheckBox *PIDCheckBox = new QCheckBox(tr("Custom product descriptor:"));
    QCheckBox *USBSerialNumberCheckBox = new QCheckBox(tr("USB serial number:"));
    QCheckBox *USBCustomStringDescriptorCheckBox = new QCheckBox(tr("Enable custom USB flag"));
    VIDCheckBox->setObjectName("VIDCheckBox");
    PIDCheckBox->setObjectName("PIDCheckBox");
    USBSerialNumberCheckBox->setObjectName("USBSerialNumberCheckBox");
    USBCustomStringDescriptorCheckBox->setObjectName("USBCustomStringDescriptorCheckBox");

    QLineEdit *VIDLineEdit = new QLineEdit(this);
    QLineEdit *PIDLineEdit = new QLineEdit(this);
    QLineEdit *VIDDescriptorLineEdit = new QLineEdit(this);
    QLineEdit *PIDDescriptorLineEdit = new QLineEdit(this);
    QLineEdit *serialNumberLineEdit = new QLineEdit(this);

    VIDDescriptorLineEdit->setMaximumWidth(120);
    PIDDescriptorLineEdit->setMaximumWidth(120);
    serialNumberLineEdit->setMaximumWidth(120);
    VIDLineEdit->setMaximumWidth(120);
    PIDLineEdit->setMaximumWidth(120);

    VIDLineEdit->setObjectName("VIDLineEdit");
    PIDLineEdit->setObjectName("PIDLineEdit");
    VIDDescriptorLineEdit->setObjectName("VIDDescriptorLineEdit");
    PIDDescriptorLineEdit->setObjectName("PIDDescriptorLineEdit");
    serialNumberLineEdit->setObjectName("serialNumberLineEdit");

    // Create a horizontal layout for VID and PID
    QHBoxLayout *vidPidLayout = new QHBoxLayout();
    vidPidLayout->addWidget(VID);
    vidPidLayout->addWidget(VIDLineEdit);
    vidPidLayout->addWidget(PID);
    vidPidLayout->addWidget(PIDLineEdit);
    vidPidLayout->addStretch();

    // Create a horizontal line separator
    QFrame *hLine = new QFrame();
    hLine->setFrameShape(QFrame::HLine);
    hLine->setFrameShadow(QFrame::Sunken);

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->addLayout(vidPidLayout, 0, 0, 1, 2);  // Span across both columns
    gridLayout->addWidget(hLine, 1, 0, 1, 2);        // Add horizontal line spanning both columns
    gridLayout->addWidget(USBDescriptor, 2, 0, Qt::AlignLeft);
    gridLayout->addWidget(USBCustomStringDescriptorCheckBox, 3, 0, Qt::AlignLeft);
    gridLayout->addWidget(VIDCheckBox, 4, 0, Qt::AlignLeft);
    gridLayout->addWidget(VIDDescriptorLineEdit, 4, 1, Qt::AlignLeft);
    gridLayout->addWidget(PIDCheckBox, 5, 0, Qt::AlignLeft);
    gridLayout->addWidget(PIDDescriptorLineEdit, 5, 1, Qt::AlignLeft);
    gridLayout->addWidget(USBSerialNumberCheckBox, 6, 0, Qt::AlignLeft);
    gridLayout->addWidget(serialNumberLineEdit, 6, 1, Qt::AlignLeft);

    QVBoxLayout *hardwareLayout = new QVBoxLayout(this);
    hardwareLayout->addWidget(hardwareLabel);
    hardwareLayout->addWidget(VIDPIDLabel);
    hardwareLayout->addLayout(gridLayout);
    hardwareLayout->addStretch();

    connect(USBCustomStringDescriptorCheckBox, &QCheckBox::stateChanged, this, &HardwarePage::onCheckBoxStateChanged);
    addCheckBoxLineEditPair(VIDCheckBox, VIDDescriptorLineEdit);
    addCheckBoxLineEditPair(PIDCheckBox, PIDDescriptorLineEdit);
    addCheckBoxLineEditPair(USBSerialNumberCheckBox, serialNumberLineEdit);
}

void HardwarePage::addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit){
    USBCheckBoxEditMap.insert(checkBox,lineEdit);
    connect(checkBox, &QCheckBox::stateChanged, this, &HardwarePage::onCheckBoxStateChanged);
}

void HardwarePage::onCheckBoxStateChanged(int state) {
    QCheckBox *checkBox = qobject_cast<QCheckBox*>(sender());
    QLineEdit *lineEdit = USBCheckBoxEditMap.value(checkBox);
    
    // Special handling for USBCustomStringDescriptorCheckBox
    if (checkBox->objectName() == "USBCustomStringDescriptorCheckBox") {
        QCheckBox *VIDCheckBox = this->findChild<QCheckBox*>("VIDCheckBox");
        QCheckBox *PIDCheckBox = this->findChild<QCheckBox*>("PIDCheckBox");
        QCheckBox *USBSerialNumberCheckBox = this->findChild<QCheckBox*>("USBSerialNumberCheckBox");
        
        if (state == Qt::Checked) {
            // Enable all descriptor checkboxes
            VIDCheckBox->setEnabled(true);
            PIDCheckBox->setEnabled(true);
            USBSerialNumberCheckBox->setEnabled(true);
            
            // Line edits remain controlled by their respective checkboxes
        } else {
            // Disable all descriptor checkboxes and their line edits
            VIDCheckBox->setEnabled(false);
            VIDCheckBox->setChecked(false);
            
            PIDCheckBox->setEnabled(false);
            PIDCheckBox->setChecked(false);
            
            USBSerialNumberCheckBox->setEnabled(false);
            USBSerialNumberCheckBox->setChecked(false);
        }
    }
    
    // Original functionality for individual checkbox-lineEdit pairs
    if (lineEdit) {
        lineEdit->setEnabled(state == Qt::Checked);
    }
}

void HardwarePage::applyHardwareSetting()
{
    QSettings settings("Techxartisan", "Openterface");
    QLineEdit *VIDLineEdit = this->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = this->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = this->findChild<QLineEdit*>("VIDDescriptorLineEdit");
    QLineEdit *PIDDescriptorLineEdit = this->findChild<QLineEdit*>("PIDDescriptorLineEdit");
    QLineEdit *serialNumberLineEdit = this->findChild<QLineEdit*>("serialNumberLineEdit");

    QByteArray EnableFlag = convertCheckBoxValueToBytes();

    GlobalSetting::instance().setVID(VIDLineEdit->text());
    GlobalSetting::instance().setPID(PIDLineEdit->text());
    GlobalSetting::instance().setCustomVIDDescriptor(VIDDescriptorLineEdit->text());
    GlobalSetting::instance().setCustomPIDDescriptor(PIDDescriptorLineEdit->text());
    GlobalSetting::instance().setSerialNumber(serialNumberLineEdit->text());
    GlobalSetting::instance().setUSBEnabelFlag(QString(EnableFlag.toHex()));

    SerialPortManager::getInstance().changeUSBDescriptor();
    QThread::msleep(10);
    SerialPortManager::getInstance().setUSBconfiguration();
}

QByteArray HardwarePage::convertCheckBoxValueToBytes(){
    QCheckBox *VIDCheckBox = this->findChild<QCheckBox *>("VIDCheckBox");
    QCheckBox *PIDCheckBox = this->findChild<QCheckBox *>("PIDCheckBox");
    QCheckBox *USBSerialNumberCheckBox = this->findChild<QCheckBox *>("USBSerialNumberCheckBox");
    QCheckBox *USBCustomStringDescriptorCheckBox = this->findChild<QCheckBox *>("USBCustomStringDescriptorCheckBox");

    bool bit0 = USBSerialNumberCheckBox->isChecked();
    bool bit1 = PIDCheckBox->isChecked();
    bool bit2 = VIDCheckBox->isChecked();
    bool bit7 = USBCustomStringDescriptorCheckBox->isChecked();

    quint8 byteValue = (bit7 << 7) | (bit2 << 2) | (bit1 << 1) | bit0;
    QByteArray hexValue;
    hexValue.append(byteValue);

    return hexValue;
}

void HardwarePage::initHardwareSetting()
{
    QSettings settings("Techxartisan", "Openterface");

    QCheckBox *VIDCheckBox = this->findChild<QCheckBox*>("VIDCheckBox");
    QCheckBox *PIDCheckBox = this->findChild<QCheckBox*>("PIDCheckBox");
    QCheckBox *USBSerialNumberCheckBox = this->findChild<QCheckBox*>("USBSerialNumberCheckBox");
    QCheckBox *USBCustomStringDescriptorCheckBox = this->findChild<QCheckBox*>("USBCustomStringDescriptorCheckBox");

    QLineEdit *VIDLineEdit = this->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = this->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = USBCheckBoxEditMap.value(VIDCheckBox);
    QLineEdit *PIDDescriptorLineEdit = USBCheckBoxEditMap.value(PIDCheckBox);
    QLineEdit *serialNumberLineEdit = USBCheckBoxEditMap.value(USBSerialNumberCheckBox);
    
    QString USBFlag = settings.value("serial/enableflag", "87").toString();
    std::array<bool, 4> enableFlagArray = extractBits(USBFlag);

    for(uint i = 0; i < enableFlagArray.size(); i++){
        qDebug() << "enable flag array: " <<enableFlagArray[i];
    }

    VIDCheckBox->setChecked(enableFlagArray[2]);
    PIDCheckBox->setChecked(enableFlagArray[1]);
    USBSerialNumberCheckBox->setChecked(enableFlagArray[0]);
    USBCustomStringDescriptorCheckBox->setChecked(enableFlagArray[3]);
    
    
    // Make the descriptor checkboxes enabled/disabled based on the master toggle
    VIDCheckBox->setEnabled(enableFlagArray[3]);
    PIDCheckBox->setEnabled(enableFlagArray[3]);
    USBSerialNumberCheckBox->setEnabled(enableFlagArray[3]);

    VIDDescriptorLineEdit->setText(settings.value("serial/customVIDDescriptor", "").toString());
    VIDDescriptorLineEdit->setToolTip("Product descriptor");
    PIDDescriptorLineEdit->setText(settings.value("serial/customPIDDescriptor", "").toString());
    PIDDescriptorLineEdit->setToolTip("Vendor descriptor");
    VIDLineEdit->setText(settings.value("serial/vid", "861A").toString());
    PIDLineEdit->setText(settings.value("serial/pid", "29E1").toString());
    serialNumberLineEdit->setText(settings.value("serial/serialnumber" , "").toString());
    serialNumberLineEdit->setToolTip("Serial number");
    if(USBCustomStringDescriptorCheckBox->isChecked()){
        VIDDescriptorLineEdit->setEnabled(enableFlagArray[2]);
        PIDDescriptorLineEdit->setEnabled(enableFlagArray[1]);
        serialNumberLineEdit->setEnabled(enableFlagArray[0]);
    }else{
        VIDDescriptorLineEdit->setEnabled(false);
        PIDDescriptorLineEdit->setEnabled(false);
        serialNumberLineEdit->setEnabled(false);
    }
}

std::array<bool, 4> HardwarePage::extractBits(QString hexString) {
    // convert hex string to bool array
    bool ok;
    int hexValue = hexString.toInt(&ok, 16);

    qDebug() << "extractBits: " << hexValue;

    if (!ok) {
        qDebug() << "Convert failed";
        return {}; // return empty array
    }

    // get the bit
    std::array<bool, 4> bits = {
        static_cast<bool>((hexValue >> 0) & 1),
        static_cast<bool>((hexValue >> 1) & 1),
        static_cast<bool>((hexValue >> 2) & 1),
        static_cast<bool>((hexValue >> 7) & 1)
    };

    return bits;
}