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

#include "audiopage.h"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>

AudioPage::AudioPage(QWidget *parent) : QWidget(parent)
{
    setupUI();
}

void AudioPage::setupUI()
{
    QLabel *audioLabel = new QLabel(
        QString("<span style='  font-weight: bold;'>%1</span>").arg(tr("General audio setting")));
    audioLabel->setStyleSheet(bigLabelFontSize);

    QLabel *audioCodecLabel = new QLabel(tr("Audio Codec: "));
    audioCodecLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *audioCodecBox = new QComboBox();
    audioCodecBox->setObjectName("audioCodecBox");

    QLabel *audioSampleRateLabel = new QLabel(tr("Sample Rate: "));
    audioSampleRateLabel->setStyleSheet(smallLabelFontSize);
    QSpinBox *audioSampleRateBox = new QSpinBox();
    audioSampleRateBox->setObjectName("audioSampleRateBox");
    audioSampleRateBox->setEnabled(false);

    QLabel *qualityLabel = new QLabel(tr("Quality: "));
    qualityLabel->setStyleSheet(smallLabelFontSize);

    QSlider *qualitySlider = new QSlider();
    qualitySlider->setObjectName("qualitySlider");
    qualitySlider->setOrientation(Qt::Horizontal);

    QLabel *fileFormatLabel = new QLabel(tr("File Format: "));
    fileFormatLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *containerFormatBox = new QComboBox();
    containerFormatBox->setObjectName("containerFormatBox");

    QVBoxLayout *audioLayout = new QVBoxLayout(this);
    audioLayout->addWidget(audioLabel);
    audioLayout->addWidget(audioCodecLabel);
    audioLayout->addWidget(audioCodecBox);
    audioLayout->addWidget(audioSampleRateLabel);
    audioLayout->addWidget(audioSampleRateBox);
    audioLayout->addWidget(qualityLabel);
    audioLayout->addWidget(qualitySlider);
    audioLayout->addWidget(fileFormatLabel);
    audioLayout->addWidget(containerFormatBox);
    audioLayout->addStretch();
}
