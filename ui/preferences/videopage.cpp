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
#include "fontstyle.h"
#include "ui/globalsetting.h"
#include "host/cameramanager.h"
#include "host/multimediabackend.h"
#include "host/backend/ffmpegbackendhandler.h"
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
#include <QWidget>
#include <QThread>
#include <QApplication>
#include <QEventLoop>
#include <QTimer>


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

    videoLayout->addWidget(overrideSettingsCheckBox);
    videoLayout->addWidget(customInputResolutionWidget);

    // Add a horizontal line separator
    QFrame *separatorLine = new QFrame();
    separatorLine->setFrameShape(QFrame::HLine);
    separatorLine->setFrameShadow(QFrame::Sunken);
    videoLayout->addWidget(separatorLine);

    // Capture Resolution Setting Section
    videoLabel->setText(QString("<span style=' font-weight: bold;'>%1</span>").arg(tr("General video setting")));
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

    QLabel *hintLabel = new QLabel(tr("Note: On linx the video may go black after OK or Apply. Please unplug and re-plug the host cable."));

    // Add another separator
    QFrame *separatorLine2 = new QFrame();
    separatorLine2->setFrameShape(QFrame::HLine);
    separatorLine2->setFrameShadow(QFrame::Sunken);

    // Media Backend Setting Section
    QLabel *backendLabel = new QLabel(tr("Media Backend: "));
    backendLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *mediaBackendBox = new QComboBox();
    mediaBackendBox->setObjectName("mediaBackendBox");
    mediaBackendBox->addItem("FFmpeg", "ffmpeg");
    mediaBackendBox->addItem("GStreamer", "gstreamer");

    // Set current backend from settings
    QString currentBackend = GlobalSetting::instance().getMediaBackend();
    int backendIndex = mediaBackendBox->findData(currentBackend);
    if (backendIndex != -1) {
        mediaBackendBox->setCurrentIndex(backendIndex);
    }

    QLabel *backendHintLabel = new QLabel(tr("Note: Changing media backend requires application restart to take effect."));
    backendHintLabel->setStyleSheet("color: #666666; font-style: italic;");

    // Connect the media backend change signal
    connect(mediaBackendBox, &QComboBox::currentIndexChanged, this, &VideoPage::onMediaBackendChanged);

    // Hardware Acceleration Setting Section
    QLabel *hwAccelLabel = new QLabel(tr("Hardware Acceleration: "));
    hwAccelLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *hwAccelBox = new QComboBox();
    hwAccelBox->setObjectName("hwAccelBox");

    QLabel *hwAccelHintLabel = new QLabel(tr("Note: Hardware acceleration improves performance but may not be available on all systems. Changing this setting requires application restart to take effect."));
    hwAccelHintLabel->setStyleSheet("color: #666666; font-style: italic;");

    // Populate hardware acceleration options
    if (m_cameraManager) {
        MultimediaBackendHandler* backend = m_cameraManager->getBackendHandler();
        if (backend) {
            QStringList availableHwAccel = backend->getAvailableHardwareAccelerations();
            hwAccelBox->clear();
            for (const QString& hw : availableHwAccel) {
                QString displayName;
                if (hw == "auto") {
                    displayName = tr("Auto (Recommended)");
                } else if (hw == "cuda") {
                    displayName = tr("NVIDIA CUDA");
                } else if (hw == "qsv") {
                    displayName = tr("Intel Quick Sync Video");
                } else if (hw == "none") {
                    displayName = tr("CPU");
                } else {
                    displayName = hw;
                }
                hwAccelBox->addItem(displayName, hw);
            }

            // Set current hardware acceleration from settings
            QString currentHwAccel = GlobalSetting::instance().getHardwareAcceleration();
            int hwIndex = hwAccelBox->findData(currentHwAccel);
            if (hwIndex != -1) {
                hwAccelBox->setCurrentIndex(hwIndex);
            }
        }
    }

    // Scaling Quality Setting Section
    QLabel *scalingQualityLabel = new QLabel(tr("Image Quality: "));
    scalingQualityLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *scalingQualityBox = new QComboBox();
    scalingQualityBox->setObjectName("scalingQualityBox");
    scalingQualityBox->addItem(tr("Fastest (Lower quality)"), "fast");
    scalingQualityBox->addItem(tr("Balanced (Good quality)"), "balanced");
    scalingQualityBox->addItem(tr("High Quality (Recommended)"), "quality");
    scalingQualityBox->addItem(tr("Best Quality (Slower)"), "best");

    // Set current scaling quality from settings
    QString currentQuality = GlobalSetting::instance().getScalingQuality();
    int qualityIndex = scalingQualityBox->findData(currentQuality);
    if (qualityIndex != -1) {
        scalingQualityBox->setCurrentIndex(qualityIndex);
    } else {
        // Default to "quality" (High Quality)
        qualityIndex = scalingQualityBox->findData("quality");
        if (qualityIndex != -1) {
            scalingQualityBox->setCurrentIndex(qualityIndex);
        }
    }

    QLabel *scalingQualityHintLabel = new QLabel(tr("Note: Higher quality settings provide sharper images but may use slightly more CPU."));
    scalingQualityHintLabel->setStyleSheet("color: #666666; font-style: italic;");

    // Add Capture Resolution elements to the layout
    videoLayout->addWidget(hintLabel);
    videoLayout->addWidget(resolutionsLabel);
    videoLayout->addWidget(videoFormatBox);
    videoLayout->addWidget(framerateLabel);
    videoLayout->addLayout(hBoxLayout);
    videoLayout->addWidget(formatLabel);
    videoLayout->addWidget(pixelFormatBox);
    videoLayout->addWidget(scalingQualityLabel);
    videoLayout->addWidget(scalingQualityBox);
    videoLayout->addWidget(scalingQualityHintLabel);
    videoLayout->addWidget(separatorLine2);
    videoLayout->addWidget(backendLabel);
    videoLayout->addWidget(mediaBackendBox);
    videoLayout->addWidget(backendHintLabel);
    videoLayout->addWidget(hwAccelLabel);
    videoLayout->addWidget(hwAccelBox);
    videoLayout->addWidget(hwAccelHintLabel);
    videoLayout->addStretch();

    // Connect the checkbox state change to the slot
    connect(overrideSettingsCheckBox, &QCheckBox::toggled, this, &VideoPage::toggleCustomResolutionInputs);

    // Initialize the state of the custom resolution inputs
    toggleCustomResolutionInputs(overrideSettingsCheckBox->isChecked());

    // Note: Camera format enumeration removed with FFmpeg backend
    // FFmpeg uses DirectShow/V4L2 format negotiation
    if (m_cameraManager) {
        // Populate with empty list (user can use custom resolution)
        populateResolutionBox(QList<QCameraFormat>());
        
        // Add default resolution options for FFmpeg backend
        if (videoFormatBox->count() == 0) {
            // Add common resolutions as defaults when no camera formats available
            std::set<int> defaultFps = {30, 60};
            QVariant fpsVariant = QVariant::fromValue<std::set<int>>(defaultFps);
            
            videoFormatBox->addItem("1920x1080 [30 - 60 Hz]", fpsVariant);
            videoFormatBox->addItem("1280x720 [30 - 60 Hz]", fpsVariant);
            videoFormatBox->addItem("640x480 [30 - 60 Hz]", fpsVariant);
            
            // Set default resolution
            m_currentResolution = QSize(1920, 1080);
        }
        
        connect(videoFormatBox, &QComboBox::currentIndexChanged, [this, videoFormatBox](int /*index*/){
            if (videoFormatBox->count() > 0) {
                QString resolutionText = videoFormatBox->currentText();
                QStringList resolutionParts = resolutionText.split(' ').first().split('x');
                if (resolutionParts.size() >= 2) {
                    m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());
                }
            }
        });

        // Only process if combobox has items
        if (videoFormatBox->count() > 0) {
            const std::set<int> fpsValues = boxValue(videoFormatBox).value<std::set<int>>();
            setFpsRange(fpsValues);
            
            QString resolutionText = videoFormatBox->currentText();
            QStringList resolutionParts = resolutionText.split(' ').first().split('x');
            if (resolutionParts.size() >= 2) {
                m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());
            }
        }
        
        updatePixelFormats();
        // connect(pixelFormatBox, &QComboBox::currentIndexChanged, this,
        //         &VideoPage::updatePixelFormats);
    } else {
        qWarning() << "CameraManager or Camera is not valid.";
    }
}



void VideoPage::populateResolutionBox(const QList<QCameraFormat> &videoFormats) {
    std::map<QSize, std::set<int>, QSizeComparator> resolutionSampleRates;

    // Check if we're using GStreamer for special handling
    QString mediaBackend = GlobalSetting::instance().getMediaBackend();
    bool isGStreamer = (mediaBackend == "gstreamer");

    // Process videoFormats to fill resolutionSampleRates and videoFormatMap
    for (const QCameraFormat &format : videoFormats) {
        QSize resolution = format.resolution();
        int minFrameRate = format.minFrameRate();
        int maxFrameRate = format.maxFrameRate();
        
        if (isGStreamer) {
            // For GStreamer, be very conservative - only use safe standard frame rates
            std::vector<int> safeFrameRates = {5, 10, 15, 20, 24, 25, 30, 50, 60};
            
            qDebug() << "GStreamer mode: Using safe frame rates for" << resolution 
                     << "range" << minFrameRate << "-" << maxFrameRate;
            
            for (int safeRate : safeFrameRates) {
                if (safeRate >= minFrameRate && safeRate <= maxFrameRate) {
                    resolutionSampleRates[resolution].insert(safeRate);
                }
            }
            
            // For GStreamer, DO NOT include actual min/max if they're not standard
            // This prevents the step assertion error
        } else {
            // For other backends, use the original approach with standard rates
            std::vector<int> standardFrameRates = {5, 10, 15, 20, 24, 25, 30, 50, 60};
            
            for (int stdRate : standardFrameRates) {
                if (stdRate >= minFrameRate && stdRate <= maxFrameRate) {
                    resolutionSampleRates[resolution].insert(stdRate);
                }
            }
            
            // Always include the actual min and max if they're not standard values
            resolutionSampleRates[resolution].insert(minFrameRate);
            resolutionSampleRates[resolution].insert(maxFrameRate);
        }
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

    // Note: Pixel format enumeration removed with FFmpeg backend
    // FFmpeg handles pixel format selection automatically
    pixelFormatBox->addItem("Auto (FFmpeg)", QVariant::fromValue(0));
    pixelFormatBox->setEnabled(false); // Disable as FFmpeg handles this

    m_updatingFormats = false;
}

QVariant VideoPage::boxValue(const QComboBox *box) const
{
    const int idx = box->currentIndex();
    return idx != -1 ? box->itemData(idx) : QVariant{};
}

void VideoPage::applyVideoSettings() {
    QComboBox *fpsComboBox = this->findChild<QComboBox*>("fpsComboBox");
    if (!fpsComboBox) {
        qWarning() << "fpsComboBox not found!";
        return;
    }
    int fps = fpsComboBox->currentData().toInt();
    qDebug() << "fpsComboBox current data:" << fpsComboBox->currentData();
    
    // Check if we're using GStreamer
    QString mediaBackend = GlobalSetting::instance().getMediaBackend();
    bool isGStreamer = (mediaBackend == "gstreamer");
    
    if (isGStreamer) {
        qDebug() << "Applying video settings with GStreamer backend - using conservative approach";
    }
    
    // Ensure pixelFormatBox is found
    QComboBox *pixelFormatBox = this->findChild<QComboBox*>("pixelFormatBox");
    if (!pixelFormatBox) {
        qWarning() << "pixelFormatBox not found!";
        return; // Early exit if pixelFormatBox is not found
    }

    // Extract the pixel format from the QVariant
    QVariant pixelFormatVariant = boxValue(pixelFormatBox);
    // Note: Camera format selection removed with FFmpeg backend
    // FFmpeg negotiates formats directly with DirectShow/V4L2
    
    // Save hardware acceleration setting
    QComboBox *hwAccelBox = this->findChild<QComboBox*>("hwAccelBox");
    if (hwAccelBox) {
        QString hwAccel = hwAccelBox->currentData().toString();
        GlobalSetting::instance().setHardwareAcceleration(hwAccel);
    }
    
    // Save scaling quality setting
    QComboBox *scalingQualityBox = this->findChild<QComboBox*>("scalingQualityBox");
    if (scalingQualityBox) {
        QString quality = scalingQualityBox->currentData().toString();
        GlobalSetting::instance().setScalingQuality(quality);
    }

    if (!m_cameraManager) {
        qWarning() << "CameraManager is not valid!";
        return;
    }

    // Save current device settings before stopping
    // This prevents device path from being cleared during stop
    QString savedPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    qDebug() << "Saving current device port chain before restart:" << savedPortChain;

    // Stop the camera if it is in an active status
    try {
        m_cameraManager->stopCamera();
        qDebug() << "Camera stopped successfully";
    } catch (const std::exception& e) {
        qCritical() << "Error stopping camera:" << e.what();
        return;
    }

    // CRITICAL FIX: Wait for capture thread to fully terminate
    // This prevents crash when FFmpeg resources are accessed during cleanup
    qDebug() << "Waiting for capture thread to terminate...";
    
    // Process events to ensure stop signal is handled
    QApplication::processEvents();
    
    // Reduced wait time since capture manager now handles proper thread termination
    // Wait 200ms for thread to gracefully exit
    QEventLoop loop;
    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();
    
    qDebug() << "Capture thread should be terminated, proceeding with restart";

    // Restore device settings before starting camera again
    if (!savedPortChain.isEmpty()) {
        GlobalSetting::instance().setOpenterfacePortChain(savedPortChain);
        qDebug() << "Restored device port chain:" << savedPortChain;
    }

    // Store settings for FFmpeg backend
    handleResolutionSettings();

    qDebug() << "Set global variable to:" << m_currentResolution.width() << m_currentResolution.height() << fps;
    GlobalVar::instance().setCaptureWidth(m_currentResolution.width());
    GlobalVar::instance().setCaptureHeight(m_currentResolution.height());
    GlobalVar::instance().setCaptureFps(fps);

    qDebug() << "Start the camera";
    // Start the camera with the new settings
    try{
        m_cameraManager->startCamera();
        qDebug() << "Camera started successfully with new settings";
    } catch (const std::exception& e){
        qCritical() << "Error starting camera: " << e.what();
    }
    

    qDebug() << "Applied settings: resolution:" << m_currentResolution << ", FPS:" << fps;

    updatePixelFormats();

    GlobalSetting::instance().setVideoSettings(m_currentResolution.width(), m_currentResolution.height(), fps);
    // Emit the signal with the new width and height
    emit videoSettingsChanged();
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
        if (resolutionParts.size() >= 2) {
            qDebug() << "resolution text: "<< resolutionText;
            qDebug() << resolutionParts[0].toInt()<< width << resolutionParts[1].toInt() << height;
            if (resolutionParts[0].toInt() == width && resolutionParts[1].toInt() == height) {
                videoFormatBox->setCurrentIndex(i);
                break;
            }
        }
    }

    QComboBox *fpsComboBox = this->findChild<QComboBox*>("fpsComboBox");
    int indexFps = fpsComboBox->findData(fps);
    if (indexFps != -1) {
        fpsComboBox->setCurrentIndex(indexFps);
    }

    // Set the media backend in the combo box
    QComboBox *mediaBackendBox = this->findChild<QComboBox*>("mediaBackendBox");
    if (mediaBackendBox) {
        QString currentBackend = GlobalSetting::instance().getMediaBackend();
        int backendIndex = mediaBackendBox->findData(currentBackend);
        if (backendIndex != -1) {
            mediaBackendBox->setCurrentIndex(backendIndex);
        }
    }

    // Set the hardware acceleration in the combo box
    QComboBox *hwAccelBox = this->findChild<QComboBox*>("hwAccelBox");
    if (hwAccelBox) {
        QString currentHwAccel = GlobalSetting::instance().getHardwareAcceleration();
        int hwAccelIndex = hwAccelBox->findData(currentHwAccel);
        if (hwAccelIndex != -1) {
            hwAccelBox->setCurrentIndex(hwAccelIndex);
        }
    }
    
    // Set the scaling quality in the combo box
    QComboBox *scalingQualityBox = this->findChild<QComboBox*>("scalingQualityBox");
    if (scalingQualityBox) {
        QString currentQuality = GlobalSetting::instance().getScalingQuality();
        int qualityIndex = scalingQualityBox->findData(currentQuality);
        if (qualityIndex != -1) {
            scalingQualityBox->setCurrentIndex(qualityIndex);
        }
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

void VideoPage::onMediaBackendChanged() {
    QComboBox *mediaBackendBox = this->findChild<QComboBox*>("mediaBackendBox");
    if (mediaBackendBox) {
        QString selectedBackend = mediaBackendBox->currentData().toString();
        GlobalSetting::instance().setMediaBackend(selectedBackend);
        qDebug() << "Media backend changed to:" << selectedBackend;
        
        // Show/hide GStreamer options based on selected backend
        if (selectedBackend == "gstreamer") {
            qDebug() << "GStreamer backend selected - using conservative frame rate handling";
            qDebug() << "Note: GStreamer may require specific frame rate ranges to avoid assertion errors";
        }
    }
}
