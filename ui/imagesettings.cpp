// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "imagesettings.h"
#include "ui_imagesettings.h"

#include <QCamera>
#include <QImageCapture>
#include <QMediaCaptureSession>

#include <QComboBox>

#include <QDebug>

using namespace Qt::StringLiterals;

ImageSettings::ImageSettings(QImageCapture *imageCapture, QWidget *parent)
    : QDialog(parent), ui(new Ui::ImageSettingsUi), imagecapture(imageCapture)
{
    ui->setupUi(this);

    //image codecs
    ui->imageCodecBox->addItem(tr("Default image format"), QVariant(QString()));
    const auto supportedImageFormats = QImageCapture::supportedFormats();
    for (const auto &f : supportedImageFormats) {
        QString description = QImageCapture::fileFormatDescription(f);
        ui->imageCodecBox->addItem(QImageCapture::fileFormatName(f) + ": " + description,
                                   QVariant::fromValue(f));
    }

    ui->imageQualitySlider->setRange(0, int(QImageCapture::VeryHighQuality));

    ui->imageResolutionBox->addItem(tr("Default Resolution"));
    const QList<QSize> supportedResolutions =
            imagecapture->captureSession()->camera()->cameraDevice().photoResolutions();
    for (const QSize &resolution : supportedResolutions) {
        ui->imageResolutionBox->addItem(
                u"%1x%2"_s.arg(resolution.width()).arg(resolution.height()),
                QVariant(resolution));
    }

    selectComboBoxItem(ui->imageCodecBox, QVariant::fromValue(imagecapture->fileFormat()));
    selectComboBoxItem(ui->imageResolutionBox, QVariant(imagecapture->resolution()));
    ui->imageQualitySlider->setValue(imagecapture->quality());
}

ImageSettings::~ImageSettings()
{
    delete ui;
}

void ImageSettings::changeEvent(QEvent *e)
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

void ImageSettings::applyImageSettings() const
{
    imagecapture->setFileFormat(boxValue(ui->imageCodecBox).value<QImageCapture::FileFormat>());
    imagecapture->setQuality(QImageCapture::Quality(ui->imageQualitySlider->value()));
    imagecapture->setResolution(boxValue(ui->imageResolutionBox).toSize());
}

QVariant ImageSettings::boxValue(const QComboBox *box) const
{
    const int idx = box->currentIndex();
    return idx != -1 ? box->itemData(idx) : QVariant{};
}

void ImageSettings::selectComboBoxItem(QComboBox *box, const QVariant &value)
{
    const int idx = box->findData(value);
    if (idx != -1)
        box->setCurrentIndex(idx);
}

#include "moc_imagesettings.cpp"
