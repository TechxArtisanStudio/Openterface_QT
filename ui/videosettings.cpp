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

#include "videosettings.h"
#include "ui_videosettings.h"
#include "global.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QCamera>
#include <QVideoSink>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QComboBox>
#include <QSpinBox>

#include <QDebug>
#include <QTextStream>

VideoSettings::VideoSettings(QCamera *_camera, QWidget *parent)
    : QDialog(parent), ui(new Ui::VideoSettingsUi), camera(_camera)
{
    ui->setupUi(this);

    // sample rate:
    // auto audioDevice = _camera->captureSession()->audioInput()->device();
    // ui->audioSampleRateBox->setRange(audioDevice.minimumSampleRate(),
    //                                  audioDevice.maximumSampleRate());

    // // camera format
    // ui->videoFormatBox->addItem(tr("Default camera format"));

    const QList<QCameraFormat> videoFormats = camera->cameraDevice().videoFormats();

    populateResolutionBox(videoFormats);

    connect(ui->videoFormatBox, &QComboBox::currentIndexChanged, [this](int /*index*/) {
        //Update the Fps spiner and slider
        this->setFpsRange(boxValue(ui->videoFormatBox).value<std::set<int>>());

        QString resolutionText = ui->videoFormatBox->currentText();
        QStringList resolutionParts = resolutionText.split(' ').first().split('x');
        m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());

        //Update the pixel format drop down box, TODO
    });

    connect(ui->fpsSlider, &QSlider::valueChanged, ui->fpsSpinBox, &QSpinBox::setValue);
    connect(ui->fpsSpinBox, &QSpinBox::valueChanged, ui->fpsSlider, &QSlider::setValue);

    const std::set<int> fpsValues = boxValue(ui->videoFormatBox).value<std::set<int>>();
    setFpsRange(fpsValues);
    QString resolutionText = ui->videoFormatBox->currentText();
    QStringList resolutionParts = resolutionText.split(' ').first().split('x');
    m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());

    updateFormatsAndCodecs();
    connect(ui->audioCodecBox, &QComboBox::currentIndexChanged, this,
            &VideoSettings::updateFormatsAndCodecs);
    connect(ui->pixelFormatBox, &QComboBox::currentIndexChanged, this,
            &VideoSettings::updateFormatsAndCodecs);
    connect(ui->containerFormatBox, &QComboBox::currentIndexChanged, this,
            &VideoSettings::updateFormatsAndCodecs);

    ui->qualitySlider->setRange(0, int(QMediaRecorder::VeryHighQuality));

    // QCameraViewfinderSettings viewfinderSettings = camera->viewfinderSettings();
    // selectComboBoxItem(ui->containerFormatBox, QVariant::fromValue(viewfinderSettings.fileFormat()));
    // selectComboBoxItem(ui->audioCodecBox, QVariant::fromValue(viewfinderSettings.audioCodec()));
    // selectComboBoxItem(ui->videoCodecBox, QVariant::fromValue(viewfinderSettings.videoCodec()));

    // ui->qualitySlider->setValue(mediaRecorder->quality());
    // ui->audioSampleRateBox->setValue(mediaRecorder->audioSampleRate());
    selectComboBoxItem(ui->videoFormatBox,
                       QVariant::fromValue<std::set<int>>({5, 10, 30}));

    // ui->fpsSlider->setValue(mediaRecorder->videoFrameRate());
    // ui->fpsSpinBox->setValue(mediaRecorder->videoFrameRate());
}

void VideoSettings::populateResolutionBox(const QList<QCameraFormat> &videoFormats) {
    std::map<QSize, std::set<int>, QSizeComparator> resolutionSampleRates;

    // Process videoFormats to fill resolutionSampleRates and videoFormatMap
    for (const QCameraFormat &format : videoFormats) {
        QSize resolution = format.resolution();
        int frameRate = format.minFrameRate();
        QVideoFrameFormat::PixelFormat pixelFormat = format.pixelFormat();

        VideoFormatKey key = {resolution, frameRate, pixelFormat};
        videoFormatMap[key] = format;

        resolutionSampleRates[resolution].insert(frameRate);
    }

    // Populate videoFormatBox with consolidated information
    for (const auto &entry : resolutionSampleRates) {
        const QSize &resolution = entry.first;
        const std::set<int> &sampleRates = entry.second;
        if (!sampleRates.empty()) {
            int minSampleRate = *std::begin(sampleRates); // First element is the smallest
            int maxSampleRate = *std::rbegin(sampleRates); // Last element is the largest
            QString itemText = QString("%1x%2 [%3 - %4 Hz]").arg(resolution.width()).arg(resolution.height()).arg(minSampleRate).arg(maxSampleRate);

            // Convert the entire set to QVariant
            QVariant sampleRatesVariant = QVariant::fromValue<std::set<int>>(sampleRates);

            ui->videoFormatBox->addItem(itemText, sampleRatesVariant);
        }
    }
}

// Method to look up video format by configuration
QCameraFormat VideoSettings::getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const {
    VideoFormatKey key = {resolution, frameRate, pixelFormat};
    auto it = videoFormatMap.find(key);
    if (it != videoFormatMap.end()) {
        return it->second;
    }
    // Handle the case where the format is not found
    return QCameraFormat();
}

VideoSettings::~VideoSettings()
{
    delete ui;
}

void VideoSettings::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void VideoSettings::setFpsRange(const std::set<int> &fpsValues)
{
    if (!fpsValues.empty()) {
        int minFps = *fpsValues.begin(); // First element is the minimum
        int maxFps = *fpsValues.rbegin(); // Last element is the maximum

        // Set the range for the slider and spin box
        ui->fpsSlider->setRange(minFps, maxFps);
        ui->fpsSlider->setValue(maxFps);
        ui->fpsSpinBox->setRange(minFps, maxFps);
        ui->fpsSpinBox->setValidValues(fpsValues);

        // Adjust the current value of the slider if it's out of the new range
        int currentSliderValue = ui->fpsSlider->value();
        qDebug() << "Set fps current value" << currentSliderValue;
        if (fpsValues.find(currentSliderValue) == fpsValues.end()) {
            // If current value is not in set, set to the maximum value
            int maxFps = *fpsValues.rbegin(); // Get the maximum value from the set
            ui->fpsSlider->setValue(maxFps);
        }
        connect(ui->fpsSlider, &QSlider::valueChanged, this, &VideoSettings::onFpsSliderValueChanged);
    }
}

void VideoSettings::onFpsSliderValueChanged(int value)
{
    // This ensures that even if the slider is manually moved, it snaps to valid fpsValues
    const std::set<int> fpsValues = boxValue(ui->videoFormatBox).value<std::set<int>>();
    if (fpsValues.find(value) == fpsValues.end()) {
        auto lower = fpsValues.lower_bound(value);
        auto upper = fpsValues.upper_bound(value);
        int nearestValue = value;

        if (lower != fpsValues.end() && upper != fpsValues.begin()) {
            upper--; // Move one step back to get the closest lesser value
            nearestValue = (value - *upper <= *lower - value) ? *upper : *lower;
        } else if (lower != fpsValues.end()) {
            nearestValue = *lower;
        } else if (upper != fpsValues.begin()) {
            nearestValue = *(--upper);
        }

        ui->fpsSlider->setValue(nearestValue);
    }
}


void VideoSettings::applySettings()
{
    QCameraFormat format = getVideoFormat(m_currentResolution, ui->fpsSlider->value(), QVideoFrameFormat::PixelFormat::Format_Jpeg);
    if(!format.isNull()){
        qDebug() << "Set Camera Format, resolution:"<< format.resolution() << ",FPS:"<< format.minFrameRate() << format.pixelFormat();
    } else {
        qWarning() << "Invalid camera format!" << m_currentResolution << ui->fpsSlider->value();
        return;
    }

    // Check the current status of the camera
    qDebug() << "Current camera status:" << camera;

    // Stop the camera if it is in an active status
    if (camera->isActive()) {
        camera->stop();
    }

    camera->setCameraFormat(format);

    GlobalVar::instance().setCaptureWidth(format.resolution().width());
    GlobalVar::instance().setCaptureHeight(format.resolution().height());
    GlobalVar::instance().setCaptureFps(format.minFrameRate());
    // Start the camera with the new settings
    camera->start();

    // Debug output to confirm settings
    QCameraFormat appliedFormat = camera->cameraFormat();
    qDebug() << "Applied Camera Format, resolution:" << appliedFormat.resolution()
             << ", FPS:" << appliedFormat.minFrameRate()
             << appliedFormat.pixelFormat();
}

void VideoSettings::updateFormatsAndCodecs()
{
    if (m_updatingFormats)
        return;
    m_updatingFormats = true;

    QMediaFormat format;
    if (ui->containerFormatBox->count())
        format.setFileFormat(boxValue(ui->containerFormatBox).value<QMediaFormat::FileFormat>());
    if (ui->audioCodecBox->count())
        format.setAudioCodec(boxValue(ui->audioCodecBox).value<QMediaFormat::AudioCodec>());
    if (ui->pixelFormatBox->count())
        format.setVideoCodec(boxValue(ui->pixelFormatBox).value<QMediaFormat::VideoCodec>());

    int currentIndex = 0;
    ui->audioCodecBox->clear();
    ui->audioCodecBox->addItem(tr("Default audio codec"),
                               QVariant::fromValue(QMediaFormat::AudioCodec::Unspecified));
    for (auto codec : format.supportedAudioCodecs(QMediaFormat::Encode)) {
        if (codec == format.audioCodec())
            currentIndex = ui->audioCodecBox->count();
        ui->audioCodecBox->addItem(QMediaFormat::audioCodecDescription(codec),
                                   QVariant::fromValue(codec));
    }
    ui->audioCodecBox->setCurrentIndex(currentIndex);

    currentIndex = 0;
    ui->pixelFormatBox->clear();
    ui->pixelFormatBox->addItem(tr("Default pixel format"),
                               QVariant::fromValue(QMediaFormat::VideoCodec::Unspecified));
    for (auto codec : format.supportedVideoCodecs(QMediaFormat::Encode)) {
        if (codec == format.videoCodec())
            currentIndex = ui->pixelFormatBox->count();
        ui->pixelFormatBox->addItem(QMediaFormat::videoCodecDescription(codec),
                                   QVariant::fromValue(codec));
    }
    ui->pixelFormatBox->setCurrentIndex(currentIndex);

    currentIndex = 0;
    ui->containerFormatBox->clear();
    ui->containerFormatBox->addItem(tr("Default file format"),
                                    QVariant::fromValue(QMediaFormat::UnspecifiedFormat));
    for (auto container : format.supportedFileFormats(QMediaFormat::Encode)) {
        if (container == format.fileFormat())
            currentIndex = ui->containerFormatBox->count();
        ui->containerFormatBox->addItem(QMediaFormat::fileFormatDescription(container),
                                        QVariant::fromValue(container));
    }
    ui->containerFormatBox->setCurrentIndex(currentIndex);

    m_updatingFormats = false;
}


QVariant VideoSettings::boxValue(const QComboBox *box) const
{
    const int idx = box->currentIndex();
    return idx != -1 ? box->itemData(idx) : QVariant{};
}

void VideoSettings::selectComboBoxItem(QComboBox *box, const QVariant &value)
{
    const int idx = box->findData(value);
    if (idx != -1)
        box->setCurrentIndex(idx);
}
