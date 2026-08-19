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
#include "../../host/audiomanager.h"
#include "../globalsetting.h"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QLoggingCategory>
#include "../../log/opflogging.h"

OPF_LOGGING_CATEGORY(log_ui_audio_page, "opf.ui.audio.page")

AudioPage::AudioPage(QWidget *parent) : PreferencePageBase(parent)
{
    setupUI();
    loadSettings();
    connectSignals();
    refreshAudioDevices();
}

void AudioPage::setupUI()
{
    setLayout(new QVBoxLayout());

    // Audio Device Selection Group
    QGroupBox *deviceGroup = new QGroupBox(tr("Audio Device Selection"));
    deviceGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QGridLayout *deviceLayout = new QGridLayout(deviceGroup);

    // Audio device selection
    QLabel *audioDeviceLabel = new QLabel(tr("Audio Input Device:"));
    audioDeviceLabel->setStyleSheet(smallLabelFontSize);
    audioDeviceComboBox = new QComboBox();
    audioDeviceComboBox->setObjectName("audioDeviceComboBox");
    audioDeviceComboBox->setToolTip(tr("Select the audio input device for capturing audio"));

    QPushButton *refreshDevicesBtn = new QPushButton(tr("Refresh"));
    refreshDevicesBtn->setObjectName("refreshDevicesBtn");
    refreshDevicesBtn->setToolTip(tr("Refresh the list of available audio devices"));

    deviceLayout->addWidget(audioDeviceLabel, 0, 0);
    deviceLayout->addWidget(audioDeviceComboBox, 0, 1);
    deviceLayout->addWidget(refreshDevicesBtn, 0, 2);

    // Current device info
    currentDeviceLabel = new QLabel(tr("Current Device: None"));
    currentDeviceLabel->setStyleSheet("color: #666; font-style: italic;");
    deviceLayout->addWidget(currentDeviceLabel, 1, 0, 1, 3);

    // Audio Recording Settings Group
    QGroupBox *recordingGroup = new QGroupBox(tr("Audio Recording Settings"));
    recordingGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QGridLayout *recordingLayout = new QGridLayout(recordingGroup);

    // Audio codec
    QLabel *audioCodecLabel = new QLabel(tr("Audio Codec:"));
    audioCodecLabel->setStyleSheet(smallLabelFontSize);
    audioCodecBox = new QComboBox();
    audioCodecBox->setObjectName("audioCodecBox");
    audioCodecBox->addItems({"AAC", "MP3", "PCM", "FLAC"});
    audioCodecBox->setToolTip(tr("Select the audio codec for recording"));

    recordingLayout->addWidget(audioCodecLabel, 0, 0);
    recordingLayout->addWidget(audioCodecBox, 0, 1);

    // Sample rate
    QLabel *audioSampleRateLabel = new QLabel(tr("Sample Rate:"));
    audioSampleRateLabel->setStyleSheet(smallLabelFontSize);
    audioSampleRateBox = new QSpinBox();
    audioSampleRateBox->setObjectName("audioSampleRateBox");
    audioSampleRateBox->setMinimum(8000);
    audioSampleRateBox->setMaximum(192000);
    audioSampleRateBox->setValue(44100);
    audioSampleRateBox->setSuffix(" Hz");
    audioSampleRateBox->setToolTip(tr("Set the audio sample rate (Hz)"));

    recordingLayout->addWidget(audioSampleRateLabel, 1, 0);
    recordingLayout->addWidget(audioSampleRateBox, 1, 1);

    // Bitrate
    QLabel *bitrateLabel = new QLabel(tr("Bitrate:"));
    bitrateLabel->setStyleSheet(smallLabelFontSize);
    audioBitrateBox = new QSpinBox();
    audioBitrateBox->setObjectName("audioBitrateBox");
    audioBitrateBox->setMinimum(32);
    audioBitrateBox->setMaximum(320);
    audioBitrateBox->setValue(128);
    audioBitrateBox->setSuffix(" kbps");
    audioBitrateBox->setToolTip(tr("Set the audio bitrate (kbps)"));

    recordingLayout->addWidget(bitrateLabel, 2, 0);
    recordingLayout->addWidget(audioBitrateBox, 2, 1);

    // Audio quality
    QLabel *qualityLabel = new QLabel(tr("Quality:"));
    qualityLabel->setStyleSheet(smallLabelFontSize);
    qualitySlider = new QSlider(Qt::Horizontal);
    qualitySlider->setObjectName("qualitySlider");
    qualitySlider->setMinimum(1);
    qualitySlider->setMaximum(10);
    qualitySlider->setValue(7);
    qualitySlider->setTickPosition(QSlider::TicksBelow);
    qualitySlider->setTickInterval(1);
    qualitySlider->setToolTip(tr("Adjust audio quality (1=lowest, 10=highest)"));

    qualityValueLabel = new QLabel("7");
    qualityValueLabel->setStyleSheet("color: #666;");

    QHBoxLayout *qualityLayout = new QHBoxLayout();
    qualityLayout->addWidget(qualitySlider);
    qualityLayout->addWidget(qualityValueLabel);

    recordingLayout->addWidget(qualityLabel, 3, 0);
    recordingLayout->addLayout(qualityLayout, 3, 1);

    // Container format
    QLabel *fileFormatLabel = new QLabel(tr("Container Format:"));
    fileFormatLabel->setStyleSheet(smallLabelFontSize);
    containerFormatBox = new QComboBox();
    containerFormatBox->setObjectName("containerFormatBox");
    containerFormatBox->addItems({"MP4", "AVI", "MOV", "MKV", "WAV"});
    containerFormatBox->setToolTip(tr("Select the container format for recordings"));

    recordingLayout->addWidget(fileFormatLabel, 4, 0);
    recordingLayout->addWidget(containerFormatBox, 4, 1);

    // Live Audio Settings Group
    QGroupBox *liveGroup = new QGroupBox(tr("Live Audio Settings"));
    liveGroup->setStyleSheet("QGroupBox { font-weight: bold; }");
    QGridLayout *liveLayout = new QGridLayout(liveGroup);

    // Enable audio
    enableAudioCheckBox = new QCheckBox(tr("Enable Audio Pass-through"));
    enableAudioCheckBox->setObjectName("enableAudioCheckBox");
    enableAudioCheckBox->setChecked(true);
    enableAudioCheckBox->setToolTip(tr("Enable real-time audio pass-through from input to output"));

    liveLayout->addWidget(enableAudioCheckBox, 0, 0, 1, 2);

    // Volume control
    QLabel *volumeLabel = new QLabel(tr("Volume:"));
    volumeLabel->setStyleSheet(smallLabelFontSize);
    volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setObjectName("volumeSlider");
    volumeSlider->setMinimum(0);
    volumeSlider->setMaximum(100);
    volumeSlider->setValue(80);
    volumeSlider->setTickPosition(QSlider::TicksBelow);
    volumeSlider->setTickInterval(10);
    volumeSlider->setToolTip(tr("Adjust audio volume (0-100%)"));

    volumeValueLabel = new QLabel("80%");
    volumeValueLabel->setStyleSheet("color: #666;");

    QHBoxLayout *volumeLayout = new QHBoxLayout();
    volumeLayout->addWidget(volumeSlider);
    volumeLayout->addWidget(volumeValueLabel);

    liveLayout->addWidget(volumeLabel, 1, 0);
    liveLayout->addLayout(volumeLayout, 1, 1);

    // Main layout
    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    mainLayout->addWidget(deviceGroup);
    mainLayout->addWidget(recordingGroup);
    mainLayout->addWidget(liveGroup);
    mainLayout->addStretch();

    // Connect refresh button
    connect(refreshDevicesBtn, &QPushButton::clicked, this, &AudioPage::refreshAudioDevices);

    // Button bar: Apply / Revert / Cancel (via base class)
    createButtonBar(mainLayout);
}

void AudioPage::loadSettings()
{
    GlobalSetting& settings = GlobalSetting::instance();

    // Load recording settings
    audioCodecBox->setCurrentText(settings.getRecordingAudioCodec());
    audioSampleRateBox->setValue(settings.getRecordingAudioSampleRate());
    audioBitrateBox->setValue(settings.getRecordingAudioBitrate());
    containerFormatBox->setCurrentText(settings.getRecordingOutputFormat());

    qCDebug(log_ui_audio_page) << "Loaded audio settings from GlobalSetting";
    captureSnapshot();
    clearDirty();
}

void AudioPage::applySettings()
{
    GlobalSetting& settings = GlobalSetting::instance();

    // Save recording settings
    settings.setRecordingAudioCodec(audioCodecBox->currentText());
    settings.setRecordingAudioSampleRate(audioSampleRateBox->value());
    settings.setRecordingAudioBitrate(audioBitrateBox->value());
    settings.setRecordingOutputFormat(containerFormatBox->currentText());

    // Save selected audio device
    if (audioDeviceComboBox->currentIndex() >= 0) {
        QString deviceId = audioDeviceComboBox->currentData().toString();
        if (!deviceId.isEmpty()) {
            AudioManager& audioManager = AudioManager::getInstance();
            // Find port chain for the selected device
            QAudioDevice selectedDevice = audioManager.findAudioDeviceById(deviceId);
            if (!selectedDevice.isNull()) {
                // This would require implementing a method to get port chain from device
                // For now, we'll store the device ID directly in settings
                // You might want to add a method to store preferred audio device ID
                qCDebug(log_ui_audio_page) << "Selected audio device:" << selectedDevice.description();
            }
        }
    }

    qCDebug(log_ui_audio_page) << "Saved audio settings to GlobalSetting";
}

void AudioPage::connectSignals()
{
    // Connect recording settings changes to markDirty
    connect(audioCodecBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ markDirty(); });
    connect(audioSampleRateBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int){ markDirty(); });
    connect(audioBitrateBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int){ markDirty(); });
    connect(containerFormatBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ markDirty(); });

    // Connect audio device selection
    connect(audioDeviceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioPage::onAudioDeviceChanged);

    // Connect volume and quality sliders
    connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        volumeValueLabel->setText(QString("%1%").arg(value));
        AudioManager& audioManager = AudioManager::getInstance();
        audioManager.setVolume(value / 100.0);
        markDirty();
    });

    connect(qualitySlider, &QSlider::valueChanged, this, [this](int value) {
        qualityValueLabel->setText(QString::number(value));
        markDirty();
    });

    // Connect enable audio checkbox
    connect(enableAudioCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        volumeSlider->setEnabled(enabled);
        if (!enabled) {
            AudioManager& audioManager = AudioManager::getInstance();
            audioManager.stop();
        } else {
            AudioManager& audioManager = AudioManager::getInstance();
            audioManager.start();
        }
        markDirty();
    });
}

void AudioPage::refreshAudioDevices()
{
    qCDebug(log_ui_audio_page) << "Refreshing audio devices list";

    audioDeviceComboBox->clear();

    AudioManager& audioManager = AudioManager::getInstance();
    QList<QAudioDevice> devices = audioManager.getAvailableAudioDevices();

    if (devices.isEmpty()) {
        audioDeviceComboBox->addItem(tr("No audio devices found"), QString());
        audioDeviceComboBox->setEnabled(false);
        currentDeviceLabel->setText(tr("Current Device: None"));
        return;
    }

    audioDeviceComboBox->setEnabled(true);

    // Add devices to combo box
    QAudioDevice currentDevice = audioManager.getCurrentAudioDevice();
    int currentIndex = -1;

    for (int i = 0; i < devices.size(); ++i) {
        const QAudioDevice& device = devices[i];
        QString deviceId = QString::fromUtf8(device.id());
        QString description = device.description();

        // Add device info
        if (device.isDefault()) {
            description += tr(" (Default)");
        }

        audioDeviceComboBox->addItem(description, deviceId);

        // Check if this is the current device
        if (!currentDevice.isNull() &&
            QString::fromUtf8(currentDevice.id()) == deviceId) {
            currentIndex = i;
        }
    }

    // Set current device
    if (currentIndex >= 0) {
        audioDeviceComboBox->setCurrentIndex(currentIndex);
        currentDeviceLabel->setText(tr("Current Device: %1").arg(currentDevice.description()));
    } else {
        currentDeviceLabel->setText(tr("Current Device: None"));
    }

    qCDebug(log_ui_audio_page) << "Found" << devices.size() << "audio devices";
}

void AudioPage::onAudioDeviceChanged(int index)
{
    if (index < 0) return;

    QString deviceId = audioDeviceComboBox->itemData(index).toString();
    if (deviceId.isEmpty()) return;

    qCDebug(log_ui_audio_page) << "Audio device changed to:" << audioDeviceComboBox->itemText(index);

    AudioManager& audioManager = AudioManager::getInstance();
    QAudioDevice selectedDevice = audioManager.findAudioDeviceById(deviceId);

    if (!selectedDevice.isNull()) {
        // Switch to the selected device
        // Note: This would require implementing device switching in AudioManager
        // For now, we'll just update the current device label
        currentDeviceLabel->setText(tr("Current Device: %1").arg(selectedDevice.description()));

        // You might want to implement:
        // audioManager.switchToDevice(selectedDevice);

        markDirty();
    }
}

void AudioPage::captureSnapshot()
{
    m_snap_audioCodecIndex = audioCodecBox->currentIndex();
    m_snap_sampleRate = audioSampleRateBox->value();
    m_snap_quality = qualitySlider->value();
    m_snap_containerFormatIndex = containerFormatBox->currentIndex();
    m_snap_audioDeviceIndex = audioDeviceComboBox->currentIndex();
    m_snap_audioBitrate = audioBitrateBox->value();
    m_snap_enableAudio = enableAudioCheckBox->isChecked();
    m_snap_volume = volumeSlider->value();
}

void AudioPage::revertToSnapshot()
{
    audioCodecBox->setCurrentIndex(m_snap_audioCodecIndex);
    audioSampleRateBox->setValue(m_snap_sampleRate);
    qualitySlider->setValue(m_snap_quality);
    containerFormatBox->setCurrentIndex(m_snap_containerFormatIndex);
    audioDeviceComboBox->setCurrentIndex(m_snap_audioDeviceIndex);
    audioBitrateBox->setValue(m_snap_audioBitrate);
    enableAudioCheckBox->setChecked(m_snap_enableAudio);
    volumeSlider->setValue(m_snap_volume);
}

bool AudioPage::valuesMatchSnapshot() const
{
    return audioCodecBox->currentIndex() == m_snap_audioCodecIndex
        && audioSampleRateBox->value() == m_snap_sampleRate
        && qualitySlider->value() == m_snap_quality
        && containerFormatBox->currentIndex() == m_snap_containerFormatIndex
        && audioDeviceComboBox->currentIndex() == m_snap_audioDeviceIndex
        && audioBitrateBox->value() == m_snap_audioBitrate
        && enableAudioCheckBox->isChecked() == m_snap_enableAudio
        && volumeSlider->value() == m_snap_volume;
}
