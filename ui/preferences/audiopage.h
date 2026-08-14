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
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include "fontstyle.h"
#include "preferencepagebase.h"

class AudioPage : public PreferencePageBase
{
    Q_OBJECT
public:
    explicit AudioPage(QWidget *parent = nullptr);
    void setupUI();

    void applySettings() override;
    void captureSnapshot() override;
    bool valuesMatchSnapshot() const override;
    void revertToSnapshot() override;

private slots:
    void loadSettings();
    void connectSignals();
    void refreshAudioDevices();
    void onAudioDeviceChanged(int index);

private:
    // Original audio settings widgets
    QLabel *audioLabel;
    QLabel *audioCodecLabel;
    QComboBox *audioCodecBox;
    QLabel *audioSampleRateLabel;
    QSpinBox *audioSampleRateBox;
    QLabel *qualityLabel;
    QSlider *qualitySlider;
    QLabel *fileFormatLabel;
    QComboBox *containerFormatBox;

    // Audio device management widgets
    QComboBox *audioDeviceComboBox;
    QLabel *currentDeviceLabel;
    QSpinBox *audioBitrateBox;
    QLabel *qualityValueLabel;
    QCheckBox *enableAudioCheckBox;
    QSlider *volumeSlider;
    QLabel *volumeValueLabel;

    // Snapshot members
    int m_snap_audioCodecIndex;
    int m_snap_sampleRate;
    int m_snap_quality;
    int m_snap_containerFormatIndex;
    int m_snap_audioDeviceIndex;
    int m_snap_audioBitrate;
    bool m_snap_enableAudio;
    int m_snap_volume;
};

#endif // AUDIOPAGE_H
