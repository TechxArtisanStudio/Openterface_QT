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

HardwarePage::~HardwarePage()
{
    delete this;
}

void HardwarePage::setupUI()
{
    // UI setup implementation
    QLabel *hardwareLabel = new QLabel(
        "<span style='  font-weight: bold;'>General hardware setting</span>");
    hardwareLabel->setStyleSheet(bigLabelFontSize);

    QLabel *uvcCamLabel = new QLabel("UVC Camera resource: ");
    uvcCamLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *uvcCamBox = new QComboBox();
    uvcCamBox->setObjectName("uvcCamBox");

    QLabel *VIDPIDLabel = new QLabel(
        "Change target VID&PID: ");
    QLabel *USBDescriptor = new QLabel("Change USB descriptor: ");
    QLabel *VID = new QLabel("VID: ");
    QLabel *PID = new QLabel("PID: ");
    QCheckBox *VIDCheckBox = new QCheckBox("Custom vendor descriptor:");
    QCheckBox *PIDCheckBox = new QCheckBox("Custom product descriptor:");
    QCheckBox *USBSerialNumberCheckBox = new QCheckBox("USB serial number:");
    QCheckBox *USBCustomStringDescriptorCheckBox = new QCheckBox("Enable USB flag");
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

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->addWidget(VID, 0,0, Qt::AlignLeft);
    gridLayout->addWidget(VIDLineEdit, 0,1, Qt::AlignLeft);
    gridLayout->addWidget(PID, 1,0, Qt::AlignLeft);
    gridLayout->addWidget(PIDLineEdit, 1,1, Qt::AlignLeft);
    gridLayout->addWidget(USBDescriptor, 2,0, Qt::AlignLeft);
    gridLayout->addWidget(USBCustomStringDescriptorCheckBox, 3,0, Qt::AlignLeft);
    gridLayout->addWidget(VIDCheckBox, 4,0, Qt::AlignLeft);
    gridLayout->addWidget(VIDDescriptorLineEdit, 4,1, Qt::AlignLeft);
    gridLayout->addWidget(PIDCheckBox, 5,0, Qt::AlignLeft);
    gridLayout->addWidget(PIDDescriptorLineEdit, 5,1, Qt::AlignLeft);
    gridLayout->addWidget(USBSerialNumberCheckBox, 6,0, Qt::AlignLeft);
    gridLayout->addWidget(serialNumberLineEdit, 6,1, Qt::AlignLeft);

    QVBoxLayout *hardwareLayout = new QVBoxLayout(this);
    hardwareLayout->addWidget(hardwareLabel);
    hardwareLayout->addWidget(uvcCamLabel);
    hardwareLayout->addWidget(uvcCamBox);
    hardwareLayout->addWidget(VIDPIDLabel);
    hardwareLayout->addLayout(gridLayout);
    hardwareLayout->addStretch();
    // add the
    addCheckBoxLineEditPair(VIDCheckBox, VIDDescriptorLineEdit);
    addCheckBoxLineEditPair(PIDCheckBox, PIDDescriptorLineEdit);
    addCheckBoxLineEditPair(USBSerialNumberCheckBox, serialNumberLineEdit);

    findUvcCameraDevices();
}

void HardwarePage::findUvcCameraDevices()
{
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    QComboBox *uvcCamBox = findChild<QComboBox *>("uvcCamBox");

    if (devices.isEmpty()) {
        qDebug() << "No video input devices found.";
    } else {
        for (const QCameraDevice &cameraDevice : devices) {
            uvcCamBox->addItem(cameraDevice.description());
        }
    }

    // set default "Openterface"
    int index = uvcCamBox->findText("Openterface");
    if (index != -1) {
        uvcCamBox->setCurrentIndex(index);
    } else {
        qDebug() << "Openterface device not found.";
    }
}

void HardwarePage::addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit){
    USBCheckBoxEditMap.insert(checkBox,lineEdit);
    connect(checkBox, &QCheckBox::stateChanged, this, &HardwarePage::onCheckBoxStateChanged);
}

void HardwarePage::onCheckBoxStateChanged(int state) {
    QCheckBox *checkBox = qobject_cast<QCheckBox*>(sender());
    QLineEdit *lineEdit = USBCheckBoxEditMap.value(checkBox);
    if (state == Qt::Checked) {
        lineEdit->setEnabled(true);
    }
    else {
        lineEdit->setEnabled(false);
    }
}

void HardwarePage::applyHardwareSetting()
{
    QSettings settings("Techxartisan", "Openterface");
    QString cameraDescription = settings.value("camera/device", "Openterface").toString();
    

    QComboBox *uvcCamBox = this->findChild<QComboBox*>("uvcCamBox");
    QLineEdit *VIDLineEdit = this->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = this->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = this->findChild<QLineEdit*>("VIDDescriptorLineEdit");
    QLineEdit *PIDDescriptorLineEdit = this->findChild<QLineEdit*>("PIDDescriptorLineEdit");
    QLineEdit *serialNumberLineEdit = this->findChild<QLineEdit*>("serialNumberLineEdit");

    QByteArray EnableFlag = convertCheckBoxValueToBytes();

    if (cameraDescription != uvcCamBox->currentText()){
        GlobalSetting::instance().setCameraDeviceSetting(uvcCamBox->currentText());
        emit cameraSettingsApplied();  // emit the hardware setting signal to change the camera device
    }

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

    QComboBox *uvcCamBox = this->findChild<QComboBox*>("uvcCamBox");

    QLineEdit *VIDLineEdit = this->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = this->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = USBCheckBoxEditMap.value(VIDCheckBox);
    QLineEdit *PIDDescriptorLineEdit = USBCheckBoxEditMap.value(PIDCheckBox);
    QLineEdit *serialNumberLineEdit = USBCheckBoxEditMap.value(USBSerialNumberCheckBox);
    // QLineEdit *customStringDescriptorLineEdit = USBCheckBoxEditMap.value(USBCustomStringDescriptorCheckBox);

    QString USBFlag = settings.value("serial/enableflag", "87").toString();
    std::array<bool, 4> enableFlagArray = extractBits(USBFlag);

    for(uint i = 0; i < enableFlagArray.size(); i++){
        qDebug() << "enable flag array: " <<enableFlagArray[i];
    }

    VIDCheckBox->setChecked(enableFlagArray[2]);
    PIDCheckBox->setChecked(enableFlagArray[1]);
    USBSerialNumberCheckBox->setChecked(enableFlagArray[0]);
    USBCustomStringDescriptorCheckBox->setChecked(enableFlagArray[3]);

    uvcCamBox->setCurrentText(settings.value("camera/device", "Openterface").toString());
    VIDDescriptorLineEdit->setText(settings.value("serial/customVIDDescriptor", "product").toString());
    PIDDescriptorLineEdit->setText(settings.value("serial/customPIDDescriptor", "vendor").toString());
    VIDLineEdit->setText(settings.value("serial/vid", "861A").toString());
    PIDLineEdit->setText(settings.value("serial/pid", "29E1").toString());
    serialNumberLineEdit->setText(settings.value("serial/serialnumber" , "serial number").toString());
    // customStringDescriptorLineEdit->setText(settings.value("serial/customstringdescriptor", "custom string").toString());


    VIDDescriptorLineEdit->setEnabled(enableFlagArray[2]);
    PIDDescriptorLineEdit->setEnabled(enableFlagArray[1]);
    serialNumberLineEdit->setEnabled(enableFlagArray[0]);
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