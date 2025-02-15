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

#include "videopage.h"
#include "host/cameramanager.h"
#include <QDebug>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QVariant>
#include <QMediaFormat>


VideoPage::VideoPage(CameraManager *cameraManager, QWidget *parent) : QWidget(parent)
    , m_cameraManager(cameraManager)
{
    setupUI();
}

void VideoPage::setupUI()
{
    // UI setup implementation
    QLabel *videoLabel = new QLabel(
        "<span style=' font-weight: bold;'>General video setting</span>");
    videoLabel->setStyleSheet(bigLabelFontSize);
    videoLabel->setTextFormat(Qt::RichText);

    QLabel *resolutionsLabel = new QLabel("Capture resolutions: ");
    resolutionsLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *videoFormatBox = new QComboBox();
    videoFormatBox->setObjectName("videoFormatBox");

    QLabel *framerateLabel = new QLabel("Framerate: ");
    framerateLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *fpsComboBox = new QComboBox();
    fpsComboBox->setObjectName("fpsComboBox");

    QHBoxLayout *hBoxLayout = new QHBoxLayout();
    hBoxLayout->addWidget(fpsComboBox);

    QLabel *formatLabel = new QLabel("Pixel format: ");
    formatLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *pixelFormatBox = new QComboBox();
    pixelFormatBox->setObjectName("pixelFormatBox");

    QVBoxLayout *videoLayout = new QVBoxLayout(this);
    videoLayout->addWidget(videoLabel);
    videoLayout->addWidget(resolutionsLabel);
    videoLayout->addWidget(videoFormatBox);
    videoLayout->addWidget(framerateLabel);
    videoLayout->addLayout(hBoxLayout);
    videoLayout->addWidget(formatLabel);
    videoLayout->addWidget(pixelFormatBox);
    videoLayout->addStretch();

    if (m_cameraManager && m_cameraManager->getCamera()) {
        const QList<QCameraFormat> videoFormats = m_cameraManager->getCameraFormats();
        populateResolutionBox(videoFormats);
        connect(videoFormatBox, &QComboBox::currentIndexChanged, [this, videoFormatBox](int /*index*/){
            this->setFpsRange(boxValue(videoFormatBox).value<std::set<int>>());

            QString resolutionText = videoFormatBox->currentText();
            QStringList resolutionParts = resolutionText.split(' ').first().split('x');
            m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());
        });

        const std::set<int> fpsValues = boxValue(videoFormatBox).value<std::set<int>>();

        setFpsRange(fpsValues);
        QString resolutionText = videoFormatBox->currentText();
        QStringList resolutionParts = resolutionText.split(' ').first().split('x');
        m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());

        updatePixelFormats();
        connect(pixelFormatBox, &QComboBox::currentIndexChanged, this,
                &VideoPage::updatePixelFormats);
    } else {
        qWarning() << "CameraManager or Camera is not valid.";
    }
}

void VideoPage::populateResolutionBox(const QList<QCameraFormat> &videoFormats) {
    std::map<QSize, std::set<int>, QSizeComparator> resolutionSampleRates;

    // Process videoFormats to fill resolutionSampleRates and videoFormatMap
    for (const QCameraFormat &format : videoFormats) {
        QSize resolution = format.resolution();
        int minFrameRate = format.minFrameRate();
        int maxFrameRate = format.maxFrameRate();
        QVideoFrameFormat::PixelFormat pixelFormat = format.pixelFormat();

        VideoFormatKey key = {resolution, minFrameRate, maxFrameRate, pixelFormat};
        // Use const_cast here to avoid the const issue
        const_cast<std::map<VideoFormatKey, QCameraFormat>&>(videoFormatMap)[key] = format;

        resolutionSampleRates[resolution].insert(minFrameRate);
        resolutionSampleRates[resolution].insert(maxFrameRate);
    }

    // Populate videoFormatBox with consolidated information
    for (const auto &entry : resolutionSampleRates) {
        const QSize &resolution = entry.first;
        const std::set<int> &sampleRates = entry.second;

        // Convert sampleRates to QStringList for printing
        QStringList sampleRatesList;
        for (int rate : sampleRates) {
            sampleRatesList << QString::number(rate);
        }

        // Print all sampleRates
        qDebug() << "Resolution:" << resolution << "Sample Rates:" << sampleRatesList.join(", ");

        if (!sampleRates.empty()) {
            int minSampleRate = *std::begin(sampleRates); // First element is the smallest
            int maxSampleRate = *std::rbegin(sampleRates); // Last element is the largest
            QString itemText = QString("%1x%2 [%3 - %4 Hz]").arg(resolution.width()).arg(resolution.height()).arg(minSampleRate).arg(maxSampleRate);

            // Convert the entire set to QVariant
            QVariant sampleRatesVariant = QVariant::fromValue<std::set<int>>(sampleRates);
            
            QComboBox *videoFormatBox = this->findChild<QComboBox*>("videoFormatBox");
            videoFormatBox->addItem(itemText, sampleRatesVariant);
        }
    }
}

void VideoPage::setFpsRange(const std::set<int> &fpsValues) {
    qDebug() << "setFpsRange";
    if (!fpsValues.empty()) {
        QComboBox *fpsComboBox = this->findChild<QComboBox*>("fpsComboBox");
        fpsComboBox->clear();
        int largestFps = *fpsValues.rbegin(); // Get the largest FPS value
        for (int fps : fpsValues) {
            fpsComboBox->addItem(QString::number(fps), fps);
            if (fps == largestFps) {
                fpsComboBox->setCurrentIndex(fpsComboBox->count() - 1);
            }
        }
    }
}
    
void VideoPage::updatePixelFormats()
{
    qDebug() << "update pixel formats";
    if (m_updatingFormats)
        return;
    m_updatingFormats = true;

    QMediaFormat format;
    QComboBox *pixelFormatBox = this->findChild<QComboBox*>("pixelFormatBox");
    if (pixelFormatBox->count())
        format.setVideoCodec(boxValue(pixelFormatBox).value<QMediaFormat::VideoCodec>());

    int currentIndex = 0;
    pixelFormatBox->clear();
    pixelFormatBox->addItem(tr("Default pixel format"),
                            QVariant::fromValue(QMediaFormat::VideoCodec::Unspecified));
    for (auto codec : format.supportedVideoCodecs(QMediaFormat::Encode)) {
        if (codec == format.videoCodec())
            currentIndex = pixelFormatBox->count();
        pixelFormatBox->addItem(QMediaFormat::videoCodecDescription(codec),
                                QVariant::fromValue(codec));
    }
    pixelFormatBox->setCurrentIndex(currentIndex);

    m_updatingFormats = false;
}

QVariant VideoPage::boxValue(const QComboBox *box) const
{
    const int idx = box->currentIndex();
    return idx != -1 ? box->itemData(idx) : QVariant{};
}

void VideoPage::applyVideoSettings() {
    QComboBox *fpsComboBox = this->findChild<QComboBox*>("fpsComboBox");
    int fps = fpsComboBox->currentData().toInt();
    qDebug() << "fpsComboBox current data:" << fpsComboBox->currentData();
    QCameraFormat format = getVideoFormat(m_currentResolution, fps, QVideoFrameFormat::PixelFormat::Format_Jpeg);
    qDebug() << "After video format get";
    if (!format.isNull()) {
        qDebug() << "Set Camera Format, resolution:" << format.resolution() << ", FPS:" << fps << format.pixelFormat();
    } else {
        qWarning() << "Invalid camera format!" << m_currentResolution << fps;
        return;
    }

    if (!m_cameraManager) {
        qWarning() << "CameraManager is not valid!";
        return;
    }

    // Stop the camera if it is in an active status
    m_cameraManager->stopCamera();

    // Set the new camera format
    m_cameraManager->setCameraFormat(format);

    qDebug() << "Set global variable to:" << format.resolution().width() << format.resolution().height() << fps;
    GlobalVar::instance().setCaptureWidth(format.resolution().width());
    GlobalVar::instance().setCaptureHeight(format.resolution().height());
    GlobalVar::instance().setCaptureFps(fps);

    qDebug() << "Start the camera";
    // Start the camera with the new settings
    m_cameraManager->startCamera();
    qDebug() << "Camera started";

    // Debug output to confirm settings
    QCameraFormat appliedFormat = m_cameraManager->getCameraFormat();
    qDebug() << "Applied Camera Format, resolution:" << appliedFormat.resolution()
             << ", FPS:" << fps
             << appliedFormat.pixelFormat();

    updatePixelFormats();

    GlobalSetting::instance().setVideoSettings(format.resolution().width(), format.resolution().height(), fps);

    // Emit the signal with the new width and height
    emit videoSettingsChanged(format.resolution().width(), format.resolution().height());
}

void VideoPage::initVideoSettings() {
    QSettings settings("Techxartisan", "Openterface");
    int width = settings.value("video/width", 1920).toInt();
    int height = settings.value("video/height", 1080).toInt();
    int fps = settings.value("video/fps", 30).toInt();

    m_currentResolution = QSize(width, height);

    QComboBox *videoFormatBox = this->findChild<QComboBox*>("videoFormatBox");

    // Set the resolution in the combo box
    for (int i = 0; i < videoFormatBox->count(); ++i) {
        QString resolutionText = videoFormatBox->itemText(i).split(' ').first();
        QStringList resolutionParts = resolutionText.split('x');
        qDebug() << "resolution text: "<< resolutionText;
        qDebug() << resolutionParts[0].toInt()<< width << resolutionParts[1].toInt() << height;
        if (resolutionParts[0].toInt() == width && resolutionParts[1].toInt() == height) {
            videoFormatBox->setCurrentIndex(i);
            break;
        }
    }

    QComboBox *fpsComboBox = this->findChild<QComboBox*>("fpsComboBox");
    int index = fpsComboBox->findData(fps);
    if (index != -1) {
        fpsComboBox->setCurrentIndex(index);
    }
}

QCameraFormat VideoPage::getVideoFormat(const QSize &resolution, int desiredFrameRate, QVideoFrameFormat::PixelFormat pixelFormat) const {
    QCameraFormat bestMatch;
    int closestFrameRate = INT_MAX;

    if (m_cameraManager) {
        for (const QCameraFormat &format : m_cameraManager->getCameraFormats()) {
            QSize formatResolution = format.resolution();
            int minFrameRate = format.minFrameRate();
            int maxFrameRate = format.maxFrameRate();
            QVideoFrameFormat::PixelFormat formatPixelFormat = format.pixelFormat();

            VideoFormatKey key = {formatResolution, minFrameRate, maxFrameRate, formatPixelFormat};
            // Use const_cast here to avoid the const issue
            const_cast<std::map<VideoFormatKey, QCameraFormat>&>(videoFormatMap)[key] = format;

            if (formatResolution == resolution && formatPixelFormat == pixelFormat) {
                if (desiredFrameRate >= minFrameRate && desiredFrameRate <= maxFrameRate) {
                    // If we find an exact match, return it immediately
                    qDebug() << "Exact match found" << format.minFrameRate() << format.maxFrameRate();
                    return format;
                }

                // Find the closest frame rate within the supported range
                int midFrameRate = (minFrameRate + maxFrameRate) / 2;
                int frameDiff = qAbs(midFrameRate - desiredFrameRate);
                if (frameDiff < closestFrameRate) {
                    qDebug() << "Closest match found";
                    closestFrameRate = frameDiff;
                    bestMatch = format;
                }
            }
        }
    }

    return bestMatch;
}
