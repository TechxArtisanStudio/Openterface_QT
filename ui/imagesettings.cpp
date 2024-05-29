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
