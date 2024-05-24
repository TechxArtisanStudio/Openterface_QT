// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "videosettings.h"
#include "ui_videosettings.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>

#include <QComboBox>
#include <QSpinBox>

#include <QDebug>
#include <QTextStream>

static QString toFormattedString(const QCameraFormat &cameraFormat)
{
    QString string;
    QTextStream str(&string);
    str << QVideoFrameFormat::pixelFormatToString(cameraFormat.pixelFormat())
        << ' ' << cameraFormat.resolution().width()
        << 'x' << cameraFormat.resolution().height()
        << ' ' << cameraFormat.minFrameRate()
        << '-' << cameraFormat.maxFrameRate() << "FPS";
    return string;
}

VideoSettings::VideoSettings(QMediaRecorder *mediaRecorder, QWidget *parent)
    : QDialog(parent), ui(new Ui::VideoSettingsUi), mediaRecorder(mediaRecorder)
{
    ui->setupUi(this);

    // sample rate:
    auto audioDevice = mediaRecorder->captureSession()->audioInput()->device();
    ui->audioSampleRateBox->setRange(audioDevice.minimumSampleRate(),
                                     audioDevice.maximumSampleRate());

    // camera format
    ui->videoFormatBox->addItem(tr("Default camera format"));

    auto *camera = mediaRecorder->captureSession()->camera();
    const QList<QCameraFormat> videoFormats = camera->cameraDevice().videoFormats();

    for (const QCameraFormat &format : videoFormats)
        ui->videoFormatBox->addItem(toFormattedString(format), QVariant::fromValue(format));

    connect(ui->videoFormatBox, &QComboBox::currentIndexChanged, [this](int /*index*/) {
        this->setFpsRange(boxValue(ui->videoFormatBox).value<QCameraFormat>());
    });

    setFpsRange(camera->cameraFormat());

    connect(ui->fpsSlider, &QSlider::valueChanged, ui->fpsSpinBox, &QSpinBox::setValue);
    connect(ui->fpsSpinBox, &QSpinBox::valueChanged, ui->fpsSlider, &QSlider::setValue);

    updateFormatsAndCodecs();
    connect(ui->audioCodecBox, &QComboBox::currentIndexChanged, this,
            &VideoSettings::updateFormatsAndCodecs);
    connect(ui->videoCodecBox, &QComboBox::currentIndexChanged, this,
            &VideoSettings::updateFormatsAndCodecs);
    connect(ui->containerFormatBox, &QComboBox::currentIndexChanged, this,
            &VideoSettings::updateFormatsAndCodecs);

    ui->qualitySlider->setRange(0, int(QMediaRecorder::VeryHighQuality));

    QMediaFormat format = mediaRecorder->mediaFormat();
    selectComboBoxItem(ui->containerFormatBox, QVariant::fromValue(format.fileFormat()));
    selectComboBoxItem(ui->audioCodecBox, QVariant::fromValue(format.audioCodec()));
    selectComboBoxItem(ui->videoCodecBox, QVariant::fromValue(format.videoCodec()));

    ui->qualitySlider->setValue(mediaRecorder->quality());
    ui->audioSampleRateBox->setValue(mediaRecorder->audioSampleRate());
    selectComboBoxItem(ui->videoFormatBox,
                       QVariant::fromValue(camera->cameraFormat()));

    ui->fpsSlider->setValue(mediaRecorder->videoFrameRate());
    ui->fpsSpinBox->setValue(mediaRecorder->videoFrameRate());
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

void VideoSettings::setFpsRange(const QCameraFormat &format)
{
    ui->fpsSlider->setRange(format.minFrameRate(), format.maxFrameRate());
    ui->fpsSpinBox->setRange(format.minFrameRate(), format.maxFrameRate());
}

void VideoSettings::applySettings()
{
    QMediaFormat format;
    format.setFileFormat(boxValue(ui->containerFormatBox).value<QMediaFormat::FileFormat>());
    format.setAudioCodec(boxValue(ui->audioCodecBox).value<QMediaFormat::AudioCodec>());
    format.setVideoCodec(boxValue(ui->videoCodecBox).value<QMediaFormat::VideoCodec>());

    mediaRecorder->setMediaFormat(format);
    mediaRecorder->setQuality(QMediaRecorder::Quality(ui->qualitySlider->value()));
    mediaRecorder->setAudioSampleRate(ui->audioSampleRateBox->value());

    const auto &cameraFormat = boxValue(ui->videoFormatBox).value<QCameraFormat>();
    mediaRecorder->setVideoResolution(cameraFormat.resolution());
    mediaRecorder->setVideoFrameRate(ui->fpsSlider->value());

    mediaRecorder->captureSession()->camera()->setCameraFormat(cameraFormat);
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
    if (ui->videoCodecBox->count())
        format.setVideoCodec(boxValue(ui->videoCodecBox).value<QMediaFormat::VideoCodec>());

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
    ui->videoCodecBox->clear();
    ui->videoCodecBox->addItem(tr("Default video codec"),
                               QVariant::fromValue(QMediaFormat::VideoCodec::Unspecified));
    for (auto codec : format.supportedVideoCodecs(QMediaFormat::Encode)) {
        if (codec == format.videoCodec())
            currentIndex = ui->videoCodecBox->count();
        ui->videoCodecBox->addItem(QMediaFormat::videoCodecDescription(codec),
                                   QVariant::fromValue(codec));
    }
    ui->videoCodecBox->setCurrentIndex(currentIndex);

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

#include "moc_videosettings.cpp"
