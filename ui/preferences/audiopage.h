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

#ifndef AUDIOPAGE_H
#define AUDIOPAGE_H

#include <QWidget>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>
#include "fontstyle.h"

class AudioPage : public QWidget
{
    Q_OBJECT
public:
    explicit AudioPage(QWidget *parent = nullptr);
    void setupUI();
private:
    QLabel *audioLabel;
    QLabel *audioCodecLabel;
    QComboBox *audioCodecBox;
    QLabel *audioSampleRateLabel;
    QSpinBox *audioSampleRateBox;
    QLabel *qualityLabel;
    QSlider *qualitySlider;
    QLabel *fileFormatLabel;
    QComboBox *containerFormatBox;
};

#endif // AUDIOPAGE_H