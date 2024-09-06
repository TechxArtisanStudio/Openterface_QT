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

#include "settingdialog.h"
#include "ui_settingdialog.h"
#include "ui/fpsspinbox.h"
#include "global.h"
#include "globalsetting.h"
#include "serial/SerialPortManager.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRegularExpression>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <QDebug>
#include <QLoggingCategory>
#include <QSettings>
#include <QElapsedTimer>
#include <qtimer.h>
#include <QList>
#include <QSerialPortInfo>
#include <QLineEdit>
#include <QByteArray>
#include <array>

SettingDialog::SettingDialog(QCamera *_camera, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingDialog)
    , settingTree(new QTreeWidget(this))
    , stackedWidget(new QStackedWidget(this))
    , buttonWidget(new QWidget(this))
    , logPage(nullptr)
    , videoPage(nullptr)
    , audioPage(nullptr)
    , hardwarePage(nullptr)
    , camera(_camera)
    
{


    ui->setupUi(this);
    createSettingTree();
    createPages();
    createButtons();
    createLayout();
    setWindowTitle(tr("Preferences"));
    // loadLogSettings();
    initLogSettings();
    initVideoSettings();
    initHardwareSetting();
    // Connect the tree widget's currentItemChanged signal to a slot
    connect(settingTree, &QTreeWidget::currentItemChanged, this, &SettingDialog::changePage);
}

SettingDialog::~SettingDialog()
{
    delete ui;
    // Ensure all dynamically allocated memory is freed
    qDeleteAll(settingTree->invisibleRootItem()->takeChildren());
}

void SettingDialog::createSettingTree() {
    // qDebug() << "creating setting Tree";
    settingTree->setColumnCount(1);
    // settingTree->setHeaderLabels(QStringList(tr("general")));
    settingTree->setHeaderHidden(true);
    settingTree->setSelectionMode(QAbstractItemView::SingleSelection);
    
    settingTree->setMaximumSize(QSize(120, 1000));
    settingTree->setRootIsDecorated(false);
    
    // QStringList names = {"Log"};
    QStringList names = {"General", "Video", "Audio", "Hardware"};
    for (const QString &name : names) {     // add item to setting tree
        QTreeWidgetItem *item = new QTreeWidgetItem(settingTree);
        item->setText(0, name);
    }
}

void SettingDialog::createLogPage() {
    logPage = new QWidget();

    // Create checkbox for log
    QCheckBox *coreCheckBox = new QCheckBox("Core");
    QCheckBox *serialCheckBox = new QCheckBox("Serial");
    QCheckBox *uiCheckBox = new QCheckBox("User Interface");
    QCheckBox *hostCheckBox = new QCheckBox("Host");
    coreCheckBox->setObjectName("core");
    serialCheckBox->setObjectName("serial");
    uiCheckBox->setObjectName("ui");
    hostCheckBox->setObjectName("host");

    QHBoxLayout *logCheckboxLayout = new QHBoxLayout();
    logCheckboxLayout->addWidget(coreCheckBox);
    logCheckboxLayout->addWidget(serialCheckBox);
    logCheckboxLayout->addWidget(uiCheckBox);
    logCheckboxLayout->addWidget(hostCheckBox);

    QLabel *logLabel = new QLabel(
        "<span style=' color: black; font-weight: bold;'>General log setting</span>");
    logLabel->setTextFormat(Qt::RichText);
    logLabel->setStyleSheet(bigLabelFontSize);
    QLabel *logDescription = new QLabel(
        "Check the check box to see the corresponding log in the QT console.");
    logDescription->setStyleSheet(commentsFontSize);

    QVBoxLayout *logLayout = new QVBoxLayout(logPage);
    logLayout->addWidget(logLabel);
    logLayout->addWidget(logDescription);
    logLayout->addLayout(logCheckboxLayout);
    logLayout->addStretch();
}

void SettingDialog::createVideoPage() {
    videoPage = new QWidget();

    QLabel *videoLabel = new QLabel(
        "<span style=' color: black; font-weight: bold;'>General video setting</span>");
    videoLabel->setStyleSheet(bigLabelFontSize);
    videoLabel->setTextFormat(Qt::RichText);

    QLabel *resolutionsLabel = new QLabel("Capture resolutions: ");
    resolutionsLabel->setStyleSheet(smallLabelFontSize);
     
    QComboBox *videoFormatBox = new QComboBox();
    videoFormatBox->setObjectName("videoFormatBox");

    QLabel *framerateLabel = new QLabel("Framerate: ");
    framerateLabel->setStyleSheet(smallLabelFontSize);

    FpsSpinBox *fpsSpinBox = new FpsSpinBox();
    fpsSpinBox->setObjectName("fpsSpinBox");
    QSlider *fpsSlider = new QSlider();
    fpsSlider->setObjectName("fpsSlider");
    fpsSlider->setOrientation(Qt::Horizontal);

    QHBoxLayout *hBoxLayout = new QHBoxLayout();
    hBoxLayout->addWidget(fpsSpinBox);
    hBoxLayout->addWidget(fpsSlider);

    QLabel *formatLabel = new QLabel("Pixel format: ");
    formatLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *pixelFormatBox = new QComboBox();
    pixelFormatBox->setObjectName("pixelFormatBox");

    QVBoxLayout *videoLayout = new QVBoxLayout(videoPage);
    videoLayout->addWidget(videoLabel);
    videoLayout->addWidget(resolutionsLabel);
    videoLayout->addWidget(videoFormatBox);
    videoLayout->addWidget(framerateLabel);
    videoLayout->addLayout(hBoxLayout);
    videoLayout->addWidget(formatLabel);
    videoLayout->addWidget(pixelFormatBox);
    videoLayout->addStretch();
   
    if (camera  != nullptr && !camera->cameraDevice().isNull() ){
        const QList<QCameraFormat> videoFormats = camera->cameraDevice().videoFormats();
        populateResolutionBox(videoFormats);
        connect(videoFormatBox, &QComboBox::currentIndexChanged, [this, videoFormatBox](int /*index*/){
            this->setFpsRange(boxValue(videoFormatBox).value<std::set<int>>());

            QString resolutionText = videoFormatBox->currentText();
            QStringList resolutionParts = resolutionText.split(' ').first().split('x');
            m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());
        });
        connect(fpsSlider, &QSlider::valueChanged, fpsSpinBox, &QSpinBox::setValue);
        connect(fpsSpinBox, &QSpinBox::valueChanged, fpsSlider, &QSlider::setValue);
        const std::set<int> fpsValues = boxValue(videoFormatBox).value<std::set<int>>();

        setFpsRange(fpsValues);
        QString resolutionText = videoFormatBox->currentText();
        QStringList resolutionParts = resolutionText.split(' ').first().split('x');
        m_currentResolution = QSize(resolutionParts[0].toInt(), resolutionParts[1].toInt());

        updatePixelFormats();
        connect(pixelFormatBox, &QComboBox::currentIndexChanged, this,
                &SettingDialog::updatePixelFormats);
    }else {
        qWarning() << "Camera or CameraDevice is not valid.";
    }
}

QVariant SettingDialog::boxValue(const QComboBox *box) const
{
    const int idx = box->currentIndex();
    return idx != -1 ? box->itemData(idx) : QVariant{};
}

void SettingDialog::updatePixelFormats()
{   
    qDebug() << "update pixel formats";
    if (m_updatingFormats)
        return;
    m_updatingFormats = true;
    
    QMediaFormat format;
    QComboBox *pixelFormatBox = videoPage->findChild<QComboBox*>("pixelFormatBox");
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



void SettingDialog::applyVideoSettings(){
    QSlider *fpsSlider = videoPage->findChild<QSlider*>("fpsSlider");
    qDebug() << "Apply video setting";
    QCameraFormat format = getVideoFormat(m_currentResolution, fpsSlider->value(), QVideoFrameFormat::PixelFormat::Format_Jpeg);
    qDebug() << "After video format get";
    if(!format.isNull()){
        qDebug() << "Set Camera Format, resolution:"<< format.resolution() << ",FPS:"<< format.minFrameRate() << format.pixelFormat();
    } else {
        qWarning() << "Invalid camera format!" << m_currentResolution << fpsSlider->value();
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

    updatePixelFormats();
    
    GlobalSetting::instance().setVideoSettings(format.resolution().width(), format.resolution().height(),format.minFrameRate());
}

void SettingDialog::initVideoSettings() {
    QSettings settings("Techxartisan", "Openterface");
    int width = settings.value("video/width", 1920).toInt();
    int height = settings.value("video/height", 1080).toInt();
    int fps = settings.value("video/fps", 30).toInt();

    m_currentResolution = QSize(width, height);

    QComboBox *videoFormatBox = videoPage->findChild<QComboBox*>("videoFormatBox");
    QSlider *fpsSlider = videoPage->findChild<QSlider*>("fpsSlider");

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

    // Set the FPS in the slider
    fpsSlider->setValue(fps);
}

QCameraFormat SettingDialog::getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const{
    qDebug() << "getVideoFormat";
    VideoFormatKey key = {resolution, frameRate, pixelFormat};
    auto it = videoFormatMap.find(key);
    if (it != videoFormatMap.end()) {
        return it->second;
    }
    // Handle the case where the format is not found
    return QCameraFormat();
}

void SettingDialog::setFpsRange(const std::set<int> &fpsValues){
    if (!fpsValues.empty()) {
        int minFps = *fpsValues.begin(); // First element is the minimum
        int maxFps = *fpsValues.rbegin(); // Last element is the maximum

        // Set the range for the slider and spin box
        QSlider *fpsSlider = videoPage->findChild<QSlider*>("fpsSlider");
        FpsSpinBox *fpsSpinBox = videoPage->findChild<FpsSpinBox*>("fpsSpinBox");
        fpsSlider->setRange(minFps, maxFps);
        fpsSlider->setValue(maxFps);
        fpsSpinBox->setRange(minFps, maxFps);
        fpsSpinBox->setValidValues(fpsValues);

         // Adjust the current value of the slider if it's out of the new range
        int currentSliderValue = fpsSlider->value();
        qDebug() << "Set fps current value" << currentSliderValue;
        if (fpsValues.find(currentSliderValue) == fpsValues.end()) {
            // If current value is not in set, set to the maximum value
            int maxFps = *fpsValues.rbegin(); // Get the maximum value from the set
            fpsSlider->setValue(maxFps);
        }
        connect(fpsSlider, &QSlider::valueChanged, this, &SettingDialog::onFpsSliderValueChanged);
    }
}

void SettingDialog::onFpsSliderValueChanged(int value) {
    static bool isUpdating = false; // prevent recursion when the slider have long pressed

    if (isUpdating) return;

    isUpdating = true;

    QComboBox *videoFormatBox = videoPage->findChild<QComboBox*>("videoFormatBox");
    QSlider *fpsSlider = videoPage->findChild<QSlider*>("fpsSpinBox");

    if (!videoFormatBox || !fpsSlider) {
        qWarning() << "Failed to find videoFormatBox or fpsSlider";
        isUpdating = false;
        return;
    }

    const std::set<int> fpsValues = boxValue(videoFormatBox).value<std::set<int>>();

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

        fpsSlider->setValue(nearestValue);
    }

    isUpdating = false;
}

void SettingDialog::populateResolutionBox(const QList<QCameraFormat> &videoFormats){
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

            
            QComboBox *videoFormatBox = videoPage->findChild<QComboBox*>("videoFormatBox");
            videoFormatBox->addItem(itemText, sampleRatesVariant);
        }
    }
}


void SettingDialog::createAudioPage() {
    audioPage = new QWidget();

    QLabel *audioLabel = new QLabel(
        "<span style=' color: black; font-weight: bold;'>General audio setting</span>");
    audioLabel->setStyleSheet(bigLabelFontSize);

    QLabel *audioCodecLabel = new QLabel("Audio Codec: ");
    audioCodecLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *audioCodecBox = new QComboBox();
    audioCodecBox->setObjectName("audioCodecBox");

    QLabel *audioSampleRateLabel = new QLabel("Sample Rate: ");
    audioSampleRateLabel->setStyleSheet(smallLabelFontSize);
    QSpinBox *audioSampleRateBox = new QSpinBox();
    audioSampleRateBox->setObjectName("audioSampleRateBox");
    audioSampleRateBox->setEnabled(false);

    QLabel *qualityLabel = new QLabel("Quality: ");
    qualityLabel->setStyleSheet(smallLabelFontSize);

    QSlider *qualitySlider = new QSlider();
    qualitySlider->setObjectName("qualitySlider");
    qualitySlider->setOrientation(Qt::Horizontal);

    QLabel *fileFormatLabel = new QLabel("File Format: ");
    fileFormatLabel->setStyleSheet(smallLabelFontSize);

    QComboBox *containerFormatBox = new QComboBox();
    containerFormatBox->setObjectName("containerFormatBox");

    

    QVBoxLayout *audioLayout = new QVBoxLayout(audioPage);
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



void SettingDialog::createHardwarePage(){
    hardwarePage = new QWidget();
    QLabel *hardwareLabel = new QLabel(
        "<span style=' color: black; font-weight: bold;'>General hardware setting</span>");
    hardwareLabel->setStyleSheet(bigLabelFontSize);

    QLabel *uvcCamLabel = new QLabel("UVC Camera resource: ");
    uvcCamLabel->setStyleSheet(smallLabelFontSize);
    QComboBox *uvcCamBox = new QComboBox();
    uvcCamBox->setObjectName("uvcCamBox");
    

    QLabel *VIDPIDLabel = new QLabel(
        "Change target VID&PID: ");
    QLabel *USBDescriptor = new QLabel("Change USB descriptor: ");
    QLabel *VID = new QLabel("VID: ");
    QLabel *PID = new QLabel("PID: ");
    QCheckBox *VIDCheckBox = new QCheckBox("Custom vendor descriptor:");
    QCheckBox *PIDCheckBox = new QCheckBox("Custom product descriptor:");
    QCheckBox *USBSerialNumberCheckBox = new QCheckBox("USB serial number:");
    QCheckBox *USBCustomStringDescriptorCheckBox = new QCheckBox("Enable USB flag");
    VIDCheckBox->setObjectName("VIDCheckBox");
    PIDCheckBox->setObjectName("PIDCheckBox");
    USBSerialNumberCheckBox->setObjectName("USBSerialNumberCheckBox");
    USBCustomStringDescriptorCheckBox->setObjectName("USBCustomStringDescriptorCheckBox");

    QLineEdit *VIDLineEdit = new QLineEdit(hardwarePage);
    QLineEdit *PIDLineEdit = new QLineEdit(hardwarePage);
    QLineEdit *VIDDescriptorLineEdit = new QLineEdit(hardwarePage);
    QLineEdit *PIDDescriptorLineEdit = new QLineEdit(hardwarePage);
    QLineEdit *serialNumberLineEdit = new QLineEdit(hardwarePage);
    // QLineEdit *customStringDescriptorLineEdit = new QLineEdit(hardwarePage);
    VIDDescriptorLineEdit->setMaximumWidth(120);
    PIDDescriptorLineEdit->setMaximumWidth(120);
    serialNumberLineEdit->setMaximumWidth(120);
    VIDLineEdit->setMaximumWidth(120);
    PIDLineEdit->setMaximumWidth(120);
    // customStringDescriptorLineEdit->setMaximumWidth(120);
    VIDLineEdit->setObjectName("VIDLineEdit");
    PIDLineEdit->setObjectName("PIDLineEdit");
    VIDDescriptorLineEdit->setObjectName("VIDDescriptorLineEdit");
    PIDDescriptorLineEdit->setObjectName("PIDDescriptorLineEdit");    
    serialNumberLineEdit->setObjectName("serialNumberLineEdit");
    // customStringDescriptorLineEdit->setObjectName("customStringDescriptorLineEdit");

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->addWidget(VID, 0,0, Qt::AlignLeft);
    gridLayout->addWidget(VIDLineEdit, 0,1, Qt::AlignLeft);
    gridLayout->addWidget(PID, 1,0, Qt::AlignLeft);
    gridLayout->addWidget(PIDLineEdit, 1,1, Qt::AlignLeft);
    gridLayout->addWidget(USBDescriptor, 2,0, Qt::AlignLeft);
    gridLayout->addWidget(USBCustomStringDescriptorCheckBox, 3,0, Qt::AlignLeft);
    gridLayout->addWidget(VIDCheckBox, 4,0, Qt::AlignLeft);
    gridLayout->addWidget(VIDDescriptorLineEdit, 4,1, Qt::AlignLeft);
    gridLayout->addWidget(PIDCheckBox, 5,0, Qt::AlignLeft);
    gridLayout->addWidget(PIDDescriptorLineEdit, 5,1, Qt::AlignLeft);
    gridLayout->addWidget(USBSerialNumberCheckBox, 6,0, Qt::AlignLeft);
    gridLayout->addWidget(serialNumberLineEdit, 6,1, Qt::AlignLeft);
    
    // gridLayout->addWidget(customStringDescriptorLineEdit, 6,1, Qt::AlignLeft);
    

    QVBoxLayout *hardwareLayout = new QVBoxLayout(hardwarePage);
    hardwareLayout->addWidget(hardwareLabel);
    hardwareLayout->addWidget(uvcCamLabel);
    hardwareLayout->addWidget(uvcCamBox);
    hardwareLayout->addWidget(VIDPIDLabel);
    hardwareLayout->addLayout(gridLayout);
    hardwareLayout->addStretch();
    // add the 
    addCheckBoxLineEditPair(VIDCheckBox, VIDDescriptorLineEdit);
    addCheckBoxLineEditPair(PIDCheckBox, PIDDescriptorLineEdit);
    addCheckBoxLineEditPair(USBSerialNumberCheckBox, serialNumberLineEdit);
    // addCheckBoxLineEditPair(USBCustomStringDescriptorCheckBox, customStringDescriptorLineEdit);


    findUvcCameraDevices();

}

void SettingDialog::addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit){
    USBCheckBoxEditMap.insert(checkBox,lineEdit);
    connect(checkBox, &QCheckBox::stateChanged, this, &SettingDialog::onCheckBoxStateChanged);
}

void SettingDialog::onCheckBoxStateChanged(int state) {
    QCheckBox *checkBox = qobject_cast<QCheckBox*>(sender());
    QLineEdit *lineEdit = USBCheckBoxEditMap.value(checkBox);
    if (state == Qt::Checked) {
        lineEdit->setEnabled(true);
    }
    else {
        lineEdit->setEnabled(false);
    }


}

std::array<bool, 4> SettingDialog::extractBits(QString hexString) {
    // convert hex string to bool array 
    bool ok;    
    int hexValue = hexString.toInt(&ok, 16);

    qDebug() << "extractBits: " << hexValue;

    if (!ok) {
        qDebug() << "Convert failed";
        return {}; // return empty array
    }

    // get the bit
    std::array<bool, 4> bits = {
        (hexValue >> 0) & 1,
        (hexValue >> 1) & 1,
        (hexValue >> 2) & 1,
        (hexValue >> 7) & 1
    };

    return bits;
}

void SettingDialog::findUvcCameraDevices(){
    
    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    QComboBox *uvcCamBox = hardwarePage->findChild<QComboBox *>("uvcCamBox");

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

QByteArray SettingDialog::convertCheckBoxValueToBytes(){
    QCheckBox *VIDCheckBox = hardwarePage->findChild<QCheckBox *>("VIDCheckBox");
    QCheckBox *PIDCheckBox = hardwarePage->findChild<QCheckBox *>("PIDCheckBox");
    QCheckBox *USBSerialNumberCheckBox = hardwarePage->findChild<QCheckBox *>("USBSerialNumberCheckBox");
    QCheckBox *USBCustomStringDescriptorCheckBox = hardwarePage->findChild<QCheckBox *>("USBCustomStringDescriptorCheckBox");

    bool bit0 = USBSerialNumberCheckBox->isChecked();
    bool bit1 = PIDCheckBox->isChecked();
    bool bit2 = VIDCheckBox->isChecked();
    bool bit7 = USBCustomStringDescriptorCheckBox->isChecked();

    quint8 byteValue = (bit7 << 7) | (bit2 << 2) | (bit1 << 1) | bit0;
    QByteArray hexValue;
    hexValue.append(byteValue);

    return hexValue;
}


void SettingDialog::applyHardwareSetting(){
    QSettings settings("Techxartisan", "Openterface");
    QString cameraDescription = settings.value("camera/device", "Openterface").toString();
    

    QComboBox *uvcCamBox = hardwarePage->findChild<QComboBox*>("uvcCamBox");
    QLineEdit *VIDLineEdit = hardwarePage->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = hardwarePage->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = hardwarePage->findChild<QLineEdit*>("VIDDescriptorLineEdit");
    QLineEdit *PIDDescriptorLineEdit = hardwarePage->findChild<QLineEdit*>("PIDDescriptorLineEdit");
    QLineEdit *serialNumberLineEdit = hardwarePage->findChild<QLineEdit*>("serialNumberLineEdit");

    QByteArray EnableFlag = convertCheckBoxValueToBytes();

    if (cameraDescription != uvcCamBox->currentText()){
        GlobalSetting::instance().setCameraDeviceSetting(uvcCamBox->currentText());
        emit cameraSettingsApplied();  // emit the hardware setting signal to change the camera device
    }

    GlobalSetting::instance().setVID(VIDLineEdit->text());
    GlobalSetting::instance().setPID(PIDLineEdit->text());
    GlobalSetting::instance().setCustomVIDDescriptor(VIDDescriptorLineEdit->text());
    GlobalSetting::instance().setCustomPIDDescriptor(PIDDescriptorLineEdit->text());
    GlobalSetting::instance().setSerialNumber(serialNumberLineEdit->text());
    GlobalSetting::instance().setUSBEnabelFlag(QString(EnableFlag.toHex()));
    
    SerialPortManager::getInstance().setUSBconfiguration();
    SerialPortManager::getInstance().changeUSBDescriptor();
}



void SettingDialog::initHardwareSetting(){
    QSettings settings("Techxartisan", "Openterface");

    QCheckBox *VIDCheckBox = hardwarePage->findChild<QCheckBox*>("VIDCheckBox");
    QCheckBox *PIDCheckBox = hardwarePage->findChild<QCheckBox*>("PIDCheckBox");
    QCheckBox *USBSerialNumberCheckBox = hardwarePage->findChild<QCheckBox*>("USBSerialNumberCheckBox");
    QCheckBox *USBCustomStringDescriptorCheckBox = hardwarePage->findChild<QCheckBox*>("USBCustomStringDescriptorCheckBox");

    QComboBox *uvcCamBox = hardwarePage->findChild<QComboBox*>("uvcCamBox");

    QLineEdit *VIDLineEdit = hardwarePage->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = hardwarePage->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = USBCheckBoxEditMap.value(VIDCheckBox);
    QLineEdit *PIDDescriptorLineEdit = USBCheckBoxEditMap.value(PIDCheckBox);
    QLineEdit *serialNumberLineEdit = USBCheckBoxEditMap.value(USBSerialNumberCheckBox);
    // QLineEdit *customStringDescriptorLineEdit = USBCheckBoxEditMap.value(USBCustomStringDescriptorCheckBox);

    QString USBFlag = settings.value("serial/enableflag", "87").toString();
    std::array<bool, 4> enableFlagArray = extractBits(USBFlag);

    for(int i = 0; i < enableFlagArray.size(); i++){
        qDebug() << "enable flag array: " <<enableFlagArray[i];
    }

    VIDCheckBox->setChecked(enableFlagArray[2]);
    PIDCheckBox->setChecked(enableFlagArray[1]);
    USBSerialNumberCheckBox->setChecked(enableFlagArray[0]);
    USBCustomStringDescriptorCheckBox->setChecked(enableFlagArray[3]);

    uvcCamBox->setCurrentText(settings.value("camera/device", "Openterface").toString());
    VIDDescriptorLineEdit->setText(settings.value("serial/customVIDDescriptor", "product").toString());
    PIDDescriptorLineEdit->setText(settings.value("serial/customPIDDescriptor", "vendor").toString());
    VIDLineEdit->setText(settings.value("serial/vid", "861A").toString());
    PIDLineEdit->setText(settings.value("serial/pid", "29E1").toString());
    serialNumberLineEdit->setText(settings.value("serial/serialnumber" , "serial number").toString());
    // customStringDescriptorLineEdit->setText(settings.value("serial/customstringdescriptor", "custom string").toString());
    

    VIDDescriptorLineEdit->setEnabled(enableFlagArray[2]);
    PIDDescriptorLineEdit->setEnabled(enableFlagArray[1]);
    serialNumberLineEdit->setEnabled(enableFlagArray[0]);
    // customStringDescriptorLineEdit->setEnabled(enableFlagArray[3]);
}

void SettingDialog::createPages() {
    createLogPage();
    createVideoPage();
    createAudioPage();
    createHardwarePage();
    
    // Add pages to the stacked widget
    stackedWidget->addWidget(logPage);
    stackedWidget->addWidget(videoPage);
    stackedWidget->addWidget(audioPage);
    stackedWidget->addWidget(hardwarePage);
}

void SettingDialog::createButtons(){
    QPushButton *okButton = new QPushButton("OK");
    QPushButton *applyButton = new QPushButton("Apply");
    QPushButton *cancelButton = new QPushButton("Cancel");

    okButton->setFixedSize(80, 30);
    applyButton->setFixedSize(80, 30);
    cancelButton->setFixedSize(80, 30);

    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);

    connect(okButton, &QPushButton::clicked, this, &SettingDialog::handleOkButton);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &SettingDialog::applyAccrodingPage);
}

void SettingDialog::createLayout() {

    QHBoxLayout *selectLayout = new QHBoxLayout;
    selectLayout->addWidget(settingTree);
    selectLayout->addWidget(stackedWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(selectLayout);
    mainLayout->addWidget(buttonWidget);
    
    setLayout(mainLayout);
}



void SettingDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    static QElapsedTimer timer;
    static bool isChanging = false;

    if (isChanging)
        return;

    isChanging = true;
    if (!current)
        current = previous;

    QString itemText = current->text(0);
    qDebug() << "Selected item:" << itemText;

    if (itemText == "General") {
        stackedWidget->setCurrentIndex(0);
    } else if (itemText == "Video") {
        stackedWidget->setCurrentIndex(1);
    } else if (itemText == "Audio") {
        stackedWidget->setCurrentIndex(2);
    } else if (itemText == "Hardware") {
        stackedWidget->setCurrentIndex(3);
    }

    QTimer::singleShot(100, this, [this]() {
        isChanging = false;
    });
}

void SettingDialog::setLogCheckBox(){
    QCheckBox *coreCheckBox = findChild<QCheckBox*>("core");
    QCheckBox *serialCheckBox = findChild<QCheckBox*>("serial");
    QCheckBox *uiCheckBox = findChild<QCheckBox*>("ui");
    QCheckBox *hostCheckBox = findChild<QCheckBox*>("host");
    
    coreCheckBox->setChecked(true);
    serialCheckBox->setChecked(true);
    uiCheckBox->setChecked(true);
    hostCheckBox->setChecked(true);
}

void SettingDialog::applyLogsettings() {

    // QSettings settings("Techxartisan", "Openterface");
    
    QCheckBox *coreCheckBox = findChild<QCheckBox*>("core");
    QCheckBox *serialCheckBox = findChild<QCheckBox*>("serial");
    QCheckBox *uiCheckBox = findChild<QCheckBox*>("ui");
    QCheckBox *hostCheckBox = findChild<QCheckBox*>("host");
    bool core =  coreCheckBox->isChecked();
    bool host = hostCheckBox->isChecked();
    bool serial = serialCheckBox->isChecked();
    bool ui = uiCheckBox->isChecked();
    // set the log filter value by check box
    QString logFilter = "";

    logFilter += core ? "opf.core.*=true\n" : "opf.core.*=false\n";
    logFilter += ui ? "opf.ui.*=true\n" : "opf.ui.*=false\n";
    logFilter += host ? "opf.host.*=true\n" : "opf.host.*=false\n";
    logFilter += serial ? "opf.core.serial=true\n" : "opf.core.serial=false\n";

    QLoggingCategory::setFilterRules(logFilter);
    // save the filter settings
    GlobalSetting::instance().setLogSettings(core, serial, ui, host);
}

void SettingDialog::initLogSettings(){

    QSettings settings("Techxartisan", "Openterface");
    QCheckBox *coreCheckBox = findChild<QCheckBox*>("core");
    QCheckBox *serialCheckBox = findChild<QCheckBox*>("serial");
    QCheckBox *uiCheckBox = findChild<QCheckBox*>("ui");
    QCheckBox *hostCheckBox = findChild<QCheckBox*>("host");

    coreCheckBox->setChecked(settings.value("log/core", true).toBool());
    serialCheckBox->setChecked(settings.value("log/serial", true).toBool());
    uiCheckBox->setChecked(settings.value("log/ui", true).toBool());
    hostCheckBox->setChecked(settings.value("log/host", true).toBool());
}

void SettingDialog::applyAccrodingPage(){
    int currentPageIndex = stackedWidget->currentIndex();
    switch (currentPageIndex)
    {
        // sequence Log Video Audio
        case 0:
            applyLogsettings();
            break;
        case 1:
            applyVideoSettings();
            break;
        case 2:

            break;
        case 3:
            applyHardwareSetting();
            break;
        default:
            break;
    }
}

void SettingDialog::handleOkButton() {
    applyLogsettings();
    applyVideoSettings();
    applyHardwareSetting();
    accept();
}
