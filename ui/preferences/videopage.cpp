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
#include "ui/globalsetting.h"
#include "host/cameramanager.h"
#include <QDebug>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QVariant>
#include <QMediaFormat>
#include <QLineEdit>
#include <QCheckBox>
#include <QFrame>
#include <QMediaDevices>


VideoPage::VideoPage(CameraManager *cameraManager, QWidget *parent) : QWidget(parent)
    , m_cameraManager(cameraManager)
{
    setupUI();
}

void VideoPage::setupUI()
{
    // UI setup implementation
    QLabel *videoLabel = new QLabel(
        "<span style=' font-weight: bold;'>Video setting</span>");
    videoLabel->setStyleSheet(bigLabelFontSize);
    videoLabel->setTextFormat(Qt::RichText);

    // Add UVC Camera selection at the top
    uvcCamLabel = new QLabel(tr("UVC Camera resource: "));
    uvcCamLabel->setStyleSheet(smallLabelFontSize);
    uvcCamBox = new QComboBox();
    uvcCamBox->setObjectName("uvcCamBox");
    findUvcCameraDevices();

    // Input Resolution Setting Section
    QCheckBox *overrideSettingsCheckBox = new QCheckBox("Override HDMI Input Setting");
    overrideSettingsCheckBox->setObjectName("overrideSettingsCheckBox");

    QLabel *inputResolutionLabel = new QLabel("Input Resolution: ");
    inputResolutionLabel->setStyleSheet(bigLabelFontSize);
    QLabel *customResolutionLabel = new QLabel("Resolution: ");

    // Create a QWidget to hold the custom resolution layout
    QWidget *customInputResolutionWidget = new QWidget();
    QHBoxLayout *customResolutionLayout = new QHBoxLayout(customInputResolutionWidget); // Set layout to the widget

    QLineEdit *customInputWidthEdit = new QLineEdit();
    customInputWidthEdit->setPlaceholderText("Enter width");
    customInputWidthEdit->setObjectName("customInputWidthEdit");

    QLineEdit *customInputHeightEdit = new QLineEdit();
    customInputHeightEdit->setPlaceholderText("Enter height");
    customInputHeightEdit->setObjectName("customInputHeightEdit");

    customResolutionLayout->addWidget(customResolutionLabel);
    customResolutionLayout->addWidget(customInputWidthEdit);
    QLabel *xLabel = new QLabel("x");
    customResolutionLayout->addWidget(xLabel);
    customResolutionLayout->addWidget(customInputHeightEdit);

    // Add the Input Resolution section to the layout
    QVBoxLayout *videoLayout = new QVBoxLayout(this);
    videoLayout->addWidget(videoLabel);
    videoLayout->addWidget(uvcCamLabel);
    videoLayout->addWidget(uvcCamBox);

    // Add a horizontal line separator after camera selection
    QFrame *cameraSeparatorLine = new QFrame();
    cameraSeparatorLine->setFrameShape(QFrame::HLine);
    cameraSeparatorLine->setFrameShadow(QFrame::Sunken);
    videoLayout->addWidget(cameraSeparatorLine);

    videoLayout->addWidget(overrideSettingsCheckBox);
    videoLayout->addWidget(customInputResolutionWidget);

    // Add a horizontal line separator
    QFrame *separatorLine = new QFrame();
    separatorLine->setFrameShape(QFrame::HLine);
    separatorLine->setFrameShadow(QFrame::Sunken);
    videoLayout->addWidget(separatorLine);

    // Capture Resolution Setting Section
    QString("<span style=' font-weight: bold;'>%1</span>").arg(tr("General video setting"));
    videoLabel->setStyleSheet(bigLabelFontSize);
    videoLabel->setTextFormat(Qt::RichText);

    QLabel *resolutionsLabel = new QLabel(tr("Capture resolutions: "));
    resolutionsLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *videoFormatBox = new QComboBox();
    videoFormatBox->setObjectName("videoFormatBox");

    QLabel *framerateLabel = new QLabel(tr("Framerate: "));
    framerateLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *fpsComboBox = new QComboBox();
    fpsComboBox->setObjectName("fpsComboBox");

    QHBoxLayout *hBoxLayout = new QHBoxLayout();
    hBoxLayout->addWidget(fpsComboBox);

    QLabel *formatLabel = new QLabel(tr("Pixel format: "));
    formatLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *pixelFormatBox = new QComboBox();
    pixelFormatBox->setObjectName("pixelFormatBox");

    // Add Capture Resolution elements to the layout
    videoLayout->addWidget(resolutionsLabel);
    videoLayout->addWidget(videoFormatBox);
    videoLayout->addWidget(framerateLabel);
    videoLayout->addLayout(hBoxLayout);
    videoLayout->addWidget(formatLabel);
    videoLayout->addWidget(pixelFormatBox);
    videoLayout->addStretch();

    // Connect the checkbox state change to the slot
    connect(overrideSettingsCheckBox, &QCheckBox::toggled, this, &VideoPage::toggleCustomResolutionInputs);

    // Connect the camera selection change
    connect(uvcCamBox, &QComboBox::currentTextChanged, [this](const QString &text) {
        QSettings settings("Techxartisan", "Openterface");
        QString currentCamera = settings.value("camera/device", "").toString();
        if (currentCamera != text) {
            emit cameraDeviceChanged();
        }
    });

    // Initialize the state of the custom resolution inputs
    toggleCustomResolutionInputs(overrideSettingsCheckBox->isChecked());

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
        // connect(pixelFormatBox, &QComboBox::currentIndexChanged, this,
        //         &VideoPage::updatePixelFormats);
    } else {
        qWarning() << "CameraManager or Camera is not valid.";
    }
}

void VideoPage::findUvcCameraDevices()
{
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    
    if (devices.isEmpty()) {
        qDebug() << "No video input devices found.";
    } else {
        for (const QCameraDevice &cameraDevice : devices) {
            uvcCamBox->addItem(cameraDevice.description());
        }
    }

    // set default "Openterface"
    int index = uvcCamBox->findText("Openterface");
    if (index != -1) {
        uvcCamBox->setCurrentIndex(index);
    } else {
        qDebug() << "Openterface device not found.";
    }
}

void VideoPage::populateResolutionBox(const QList<QCameraFormat> &videoFormats) {
    std::map<QSize, std::set<int>, QSizeComparator> resolutionSampleRates;

    // Process videoFormats to fill resolutionSampleRates and videoFormatMap
    for (const QCameraFormat &format : videoFormats) {
        QSize resolution = format.resolution();
        int minFrameRate = format.minFrameRate();
        int maxFrameRate = format.maxFrameRate();
        // QVideoFrameFormat::PixelFormat pixelFormat = format.pixelFormat();

        // VideoFormatKey key = {resolution, minFrameRate, maxFrameRate, pixelFormat};
        // Use const_cast here to avoid the const issue
        // const_cast<std::map<VideoFormatKey, QCameraFormat>&>(videoFormatMap)[key] = format;

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

    QComboBox *pixelFormatBox = this->findChild<QComboBox*>("pixelFormatBox");
    pixelFormatBox->clear();

    // Retrieve supported pixel formats from the camera manager
    if (m_cameraManager) {
        for (const auto &format : m_cameraManager->getSupportedPixelFormats()) {
            QString description;

            // Map pixel formats to their descriptions
            switch (format.pixelFormat()) {
                case QVideoFrameFormat::Format_YUV420P:
                    description = "YUV420P";
                    break;
                case QVideoFrameFormat::Format_Jpeg:
                    description = "JPEG";
                    break;
                default:
                    description = "Unknown Format";
                    break;
            }

            // Add each pixel format to the combo box
            pixelFormatBox->addItem(description, QVariant::fromValue(format.pixelFormat()));
        }
    }

    m_updatingFormats = false;
}

QVariant VideoPage::boxValue(const QComboBox *box) const
{
    const int idx = box->currentIndex();
    return idx != -1 ? box->itemData(idx) : QVariant{};
}

void VideoPage::applyVideoSettings() {
    // Get selected camera device
    QString selectedCamera = uvcCamBox->currentText();
    GlobalSetting::instance().setCameraDeviceSetting(selectedCamera);
    
    QComboBox *fpsComboBox = this->findChild<QComboBox*>("fpsComboBox");
    int fps = fpsComboBox->currentData().toInt();
    qDebug() << "fpsComboBox current data:" << fpsComboBox->currentData();
    
    // Ensure pixelFormatBox is found
    QComboBox *pixelFormatBox = this->findChild<QComboBox*>("pixelFormatBox");
    if (!pixelFormatBox) {
        qWarning() << "pixelFormatBox not found!";
        return; // Early exit if pixelFormatBox is not found
    }

    // Extract the pixel format from the QVariant
    QVariant pixelFormatVariant = boxValue(pixelFormatBox);
    QVideoFrameFormat::PixelFormat pixelFormat = static_cast<QVideoFrameFormat::PixelFormat>(pixelFormatVariant.toInt());
    
    QCameraFormat format = m_cameraManager->getVideoFormat(m_currentResolution, fps, pixelFormat);

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

    handleResolutionSettings();

    qDebug() << "Set global variable to:" << format.resolution().width() << format.resolution().height() << fps;
    GlobalVar::instance().setCaptureWidth(format.resolution().width());
    GlobalVar::instance().setCaptureHeight(format.resolution().height());
    GlobalVar::instance().setCaptureFps(fps);

    qDebug() << "Start the camera";
    // Start the camera with the new settings
    m_cameraManager->startCamera();
    // qDebug() << "Camera started";

    // Debug output to confirm settings
    QCameraFormat appliedFormat = m_cameraManager->getCameraFormat();
    qDebug() << "Applied Camera Format, resolution:" << appliedFormat.resolution()
             << ", FPS:" << fps
             << appliedFormat.pixelFormat();

    updatePixelFormats();

    GlobalSetting::instance().setVideoSettings(format.resolution().width(), format.resolution().height(), fps);
    // Emit the signal with the new width and height
    emit videoSettingsChanged();
}

void VideoPage::initVideoSettings() {
    QSettings settings("Techxartisan", "Openterface");
    
    // Load camera selection
    QString cameraDescription = settings.value("camera/device", "Openterface").toString();
    int index = uvcCamBox->findText(cameraDescription);
    if (index != -1) {
        uvcCamBox->setCurrentIndex(index);
    }
    
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
    int indexFps = fpsComboBox->findData(fps);
    if (indexFps != -1) {
        fpsComboBox->setCurrentIndex(indexFps);
    }
}

void VideoPage::handleResolutionSettings() {
    QLineEdit *customInputWidthEdit = this->findChild<QLineEdit*>("customInputWidthEdit");
    QLineEdit *customInputHeightEdit = this->findChild<QLineEdit*>("customInputHeightEdit");
    QCheckBox *overrideSettingsCheckBox = this->findChild<QCheckBox*>("overrideSettingsCheckBox");

    if (overrideSettingsCheckBox->isChecked()) {
        GlobalVar::instance().setUseCustomInputResolution(true);
        int customWidth = customInputWidthEdit->text().toInt();
        int customHeight = customInputHeightEdit->text().toInt();
        GlobalVar::instance().setInputWidth(customWidth);
        GlobalVar::instance().setInputHeight(customHeight);
    }else{
        GlobalVar::instance().setUseCustomInputResolution(false);
    }
}

void VideoPage::toggleCustomResolutionInputs(bool checked) {
    QLineEdit *customInputWidthEdit = this->findChild<QLineEdit*>("customInputWidthEdit");
    QLineEdit *customInputHeightEdit = this->findChild<QLineEdit*>("customInputHeightEdit");

    // Enable or disable the input fields based on the checkbox state
    customInputWidthEdit->setEnabled(checked);
    customInputHeightEdit->setEnabled(checked);
}
