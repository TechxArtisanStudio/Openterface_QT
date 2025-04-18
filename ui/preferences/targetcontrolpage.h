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

#ifndef TARGETCONTROL_H
#define TARGETCONTROL_H

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
#include <QRadioButton>
#include <QButtonGroup>
#include "host/cameramanager.h"
#include "fontstyle.h"

class TargetControlPage : public QWidget
{
    Q_OBJECT
public:
    explicit TargetControlPage(QWidget *parent = nullptr);
    void setupUI();
    void applyHardwareSetting();
    void initHardwareSetting();

private:
    QLabel *hardwareLabel;
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

    void addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit);
    void onCheckBoxStateChanged(int state);
    
    std::array<bool, 4> extractBits(QString hexString);
    QByteArray convertCheckBoxValueToBytes();
    QMap<QCheckBox *, QLineEdit *> USBCheckBoxEditMap; // map of checkboxes to line edit about VID PID etc.

    // Operating mode widgets
    QButtonGroup *operatingModeGroup;
    QRadioButton *fullModeRadio;
    QRadioButton *keyboardOnlyRadio;
    QRadioButton *keyboardMouseRadio;
    QRadioButton *customHIDRadio;

    const QString bigLabelFontSize = "font-size: 16px;";

    int originalOperatingMode;
};

#endif // TARGETCONTROL_H

