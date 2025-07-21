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

#ifndef LOGPAGE_H
#define LOGPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include "fontstyle.h"

class LogPage : public QWidget
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);
    void setupUI();
    void browseLogPath();
    void initLogSettings();
    void applyLogsettings();

signals:
    void ScreenSaverInhibitedChanged(bool inhibited);

private:

    QCheckBox *coreCheckBox;
    QCheckBox *serialCheckBox;
    QCheckBox *uiCheckBox;
    QCheckBox *hostCheckBox;
    QCheckBox *storeLogCheckBox;
    QLineEdit *logFilePathLineEdit;
    QPushButton *browseButton;
    QCheckBox *screenSaverCheckBox;

    QHBoxLayout *logCheckboxLayout;
    QHBoxLayout *logFilePathLayout;
    QLabel *logLabel;
    QLabel *logDescription;
    QVBoxLayout *logLayout;
};

#endif // LOGPAGE_H
