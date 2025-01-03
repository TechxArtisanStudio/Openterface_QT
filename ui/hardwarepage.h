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

#ifndef HARDWAREPAGE_H
#define HARDWAREPAGE_H

#include <QWidget>
#include <QCameraDevice>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QMediaDevices>
#include <QMap>
#include <QCamera>
#include "host/cameramanager.h"
#include "fontstyle.h"
class HardwarePage : public QWidget
{
    Q_OBJECT
public:
    explicit HardwarePage(QWidget *parent = nullptr);
    ~HardwarePage();
    void setupUI();
    void applyHardwareSetting();
    void initHardwareSetting();

signals:
    void cameraSettingsApplied();
    void videoSettingsChanged(int width, int height);
private:
    QLabel *hardwareLabel;
    QLabel *uvcCamLabel;
    QComboBox *uvcCamBox;
    QLabel *VIDPIDLabel;
    QLabel *VID;
    QLabel *PID;
    QCheckBox *VIDCheckBox;
    QCheckBox *PIDCheckBox;
    QCheckBox *USBSerialNumberCheckBox;
    QCheckBox *USBCustomStringDescriptorCheckBox;
    QLineEdit *VIDLineEdit;
    QLineEdit *PIDLineEdit;
    QLineEdit *VIDDescriptorLineEdit;
    QLineEdit *PIDDescriptorLineEdit;
    QLineEdit *serialNumberLineEdit;

    void findUvcCameraDevices();
    void addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit);
    void onCheckBoxStateChanged(int state);
    
    std::array<bool, 4> extractBits(QString hexString);
    QByteArray convertCheckBoxValueToBytes();
        QMap<QCheckBox *, QLineEdit *> USBCheckBoxEditMap; // map of checkboxes to line edit about VID PID etc.

};

#endif // HARDWAREPAGE_H

