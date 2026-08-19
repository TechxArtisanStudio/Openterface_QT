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

#include "targetcontrolpage.h"
#include "ui/globalsetting.h"
#include "serial/SerialPortManager.h"
#include <QSettings>
#include <QMessageBox>
#include <QLoggingCategory>
#include <QApplication>
#include "../../log/opflogging.h"

// Define logging category for Target Control page
OPF_LOGGING_CATEGORY(log_target_control, "opf.targetcontrol")

TargetControlPage::TargetControlPage(QWidget *parent) : PreferencePageBase(parent)
{
    setupUI();
}

void TargetControlPage::setupUI()
{
    // UI setup implementation for existing controls
    hardwareLabel = new QLabel(
        QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Target control setting")));
    hardwareLabel->setStyleSheet(bigLabelFontSize);

    // Operating Mode Section
    QLabel *operatingModeLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Target Control Operating Mode:")));

    // Create radio buttons for operating modes
    fullModeRadio = new QRadioButton(tr("[Performance] Standard USB keyboard + USB mouse device + USB custom HID device"));
    fullModeRadio->setToolTip(tr("The target USB port is a multi-functional composite device supporting a keyboard, mouse, and custom HID device. It performs best, though the mouse may have compatibility issues with Mac OS and Linux."));
    keyboardOnlyRadio = new QRadioButton(tr("[Keyboard Only] Standard USB keyboard device"));
    keyboardOnlyRadio->setToolTip(tr("The target USB port is a standard keyboard device without multimedia keys, supporting full keyboard mode and suitable for systems that don't support composite devices."));
    keyboardMouseRadio = new QRadioButton(tr("[Compatibility] Standard USB keyboard + USB mouse device"));
    keyboardMouseRadio->setToolTip(tr("The target USB port is a muti-functional composite device for keyboard and mouse. Best competibility with Mac OS, Andriod and Linux."));
    customHIDRadio = new QRadioButton(tr("[Custom HID] Standard USB custom HID device"));
    customHIDRadio->setToolTip(tr("The target USB port is a custom HID device supporting data transmission between host serial and target HID ."));

    // Group the radio buttons
    operatingModeGroup = new QButtonGroup(this);
    operatingModeGroup->addButton(fullModeRadio, 0);
    operatingModeGroup->addButton(keyboardOnlyRadio, 1);
    operatingModeGroup->addButton(keyboardMouseRadio, 2);
    operatingModeGroup->addButton(customHIDRadio, 3);

    // Create layout for operating mode section
    QVBoxLayout *operatingModeLayout = new QVBoxLayout();
    operatingModeLayout->addWidget(operatingModeLabel);
    operatingModeLayout->addWidget(fullModeRadio);
    operatingModeLayout->addWidget(keyboardOnlyRadio);
    operatingModeLayout->addWidget(keyboardMouseRadio);
    operatingModeLayout->addWidget(customHIDRadio);

    // Create a horizontal line separator
    QFrame *operatingModeSeparator = new QFrame();
    operatingModeSeparator->setFrameShape(QFrame::HLine);
    operatingModeSeparator->setFrameShadow(QFrame::Sunken);
    operatingModeLayout->addWidget(operatingModeSeparator);
    operatingModeLayout->addSpacing(10);

    QLabel *VIDPIDLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Custom target USB Composite Device VID and PID:")));
    QLabel *USBDescriptor = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Custom target USB descriptors: ")));
    VID = new QLabel(tr("VID: "));
    PID = new QLabel(tr("PID: "));
    VIDCheckBox = new QCheckBox(tr("Custom vendor descriptor:"));
    PIDCheckBox = new QCheckBox(tr("Custom product descriptor:"));
    USBSerialNumberCheckBox = new QCheckBox(tr("USB serial number:"));
    USBCustomStringDescriptorCheckBox = new QCheckBox(tr("Enable custom USB flag"));
    VIDCheckBox->setObjectName("VIDCheckBox");
    PIDCheckBox->setObjectName("PIDCheckBox");
    USBSerialNumberCheckBox->setObjectName("USBSerialNumberCheckBox");
    USBCustomStringDescriptorCheckBox->setObjectName("USBCustomStringDescriptorCheckBox");

    VIDLineEdit = new QLineEdit(this);
    PIDLineEdit = new QLineEdit(this);
    VIDDescriptorLineEdit = new QLineEdit(this);
    PIDDescriptorLineEdit = new QLineEdit(this);
    serialNumberLineEdit = new QLineEdit(this);

    VIDDescriptorLineEdit->setMaximumWidth(120);
    PIDDescriptorLineEdit->setMaximumWidth(120);
    serialNumberLineEdit->setMaximumWidth(120);
    VIDLineEdit->setMaximumWidth(120);
    PIDLineEdit->setMaximumWidth(120);

    VIDLineEdit->setObjectName("VIDLineEdit");
    PIDLineEdit->setObjectName("PIDLineEdit");
    VIDDescriptorLineEdit->setObjectName("VIDDescriptorLineEdit");
    PIDDescriptorLineEdit->setObjectName("PIDDescriptorLineEdit");
    serialNumberLineEdit->setObjectName("serialNumberLineEdit");

    // Create a horizontal layout for VID and PID
    QHBoxLayout *vidPidLayout = new QHBoxLayout();
    vidPidLayout->addWidget(VID);
    vidPidLayout->addWidget(VIDLineEdit);
    vidPidLayout->addWidget(PID);
    vidPidLayout->addWidget(PIDLineEdit);
    vidPidLayout->addStretch();

    // Create a horizontal line separator
    QFrame *hLine = new QFrame();
    hLine->setFrameShape(QFrame::HLine);
    hLine->setFrameShadow(QFrame::Sunken);

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->addLayout(vidPidLayout, 0, 0, 1, 2);  // Span across both columns
    gridLayout->addWidget(hLine, 1, 0, 1, 2);        // Add horizontal line spanning both columns
    gridLayout->addWidget(USBDescriptor, 2, 0, Qt::AlignLeft);
    gridLayout->addWidget(USBCustomStringDescriptorCheckBox, 3, 0, Qt::AlignLeft);
    gridLayout->addWidget(VIDCheckBox, 4, 0, Qt::AlignLeft);
    gridLayout->addWidget(VIDDescriptorLineEdit, 4, 1, Qt::AlignLeft);
    gridLayout->addWidget(PIDCheckBox, 5, 0, Qt::AlignLeft);
    gridLayout->addWidget(PIDDescriptorLineEdit, 5, 1, Qt::AlignLeft);
    gridLayout->addWidget(USBSerialNumberCheckBox, 6, 0, Qt::AlignLeft);
    gridLayout->addWidget(serialNumberLineEdit, 6, 1, Qt::AlignLeft);

    QVBoxLayout *hardwareLayout = new QVBoxLayout(this);
    hardwareLayout->addWidget(hardwareLabel);
    hardwareLayout->addLayout(operatingModeLayout);
    hardwareLayout->addWidget(VIDPIDLabel);
    hardwareLayout->addLayout(gridLayout);
    hardwareLayout->addStretch();

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(USBCustomStringDescriptorCheckBox, &QCheckBox::checkStateChanged, this, &TargetControlPage::onCheckBoxStateChanged);
#else
    connect(USBCustomStringDescriptorCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        onCheckBoxStateChanged(static_cast<Qt::CheckState>(state));
    });
#endif
    addCheckBoxLineEditPair(VIDCheckBox, VIDDescriptorLineEdit);
    addCheckBoxLineEditPair(PIDCheckBox, PIDDescriptorLineEdit);
    addCheckBoxLineEditPair(USBSerialNumberCheckBox, serialNumberLineEdit);

    // Button bar from base class
    createButtonBar(hardwareLayout);

    // Connect settings widgets to markDirty()
    connect(operatingModeGroup, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton *) { markDirty(); });

    connect(VIDLineEdit, &QLineEdit::textChanged, this, [this](const QString &) { markDirty(); });
    connect(PIDLineEdit, &QLineEdit::textChanged, this, [this](const QString &) { markDirty(); });
    connect(VIDDescriptorLineEdit, &QLineEdit::textChanged, this, [this](const QString &) { markDirty(); });
    connect(PIDDescriptorLineEdit, &QLineEdit::textChanged, this, [this](const QString &) { markDirty(); });
    connect(serialNumberLineEdit, &QLineEdit::textChanged, this, [this](const QString &) { markDirty(); });
}

void TargetControlPage::addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit){
    USBCheckBoxEditMap.insert(checkBox,lineEdit);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(checkBox, &QCheckBox::checkStateChanged, this, &TargetControlPage::onCheckBoxStateChanged);
#else
    connect(checkBox, &QCheckBox::stateChanged, this, [this](int state) {
        onCheckBoxStateChanged(static_cast<Qt::CheckState>(state));
    });
#endif
}

void TargetControlPage::onCheckBoxStateChanged(Qt::CheckState state) {
    QCheckBox *checkBox = qobject_cast<QCheckBox*>(sender());
    QLineEdit *lineEdit = USBCheckBoxEditMap.value(checkBox);

    // Special handling for USBCustomStringDescriptorCheckBox
    if (checkBox->objectName() == "USBCustomStringDescriptorCheckBox") {
        QCheckBox *VIDCheckBox = this->findChild<QCheckBox*>("VIDCheckBox");
        QCheckBox *PIDCheckBox = this->findChild<QCheckBox*>("PIDCheckBox");
        QCheckBox *USBSerialNumberCheckBox = this->findChild<QCheckBox*>("USBSerialNumberCheckBox");

        if (state == Qt::Checked) {
            // Enable all descriptor checkboxes
            VIDCheckBox->setEnabled(true);
            PIDCheckBox->setEnabled(true);
            USBSerialNumberCheckBox->setEnabled(true);

            // Line edits remain controlled by their respective checkboxes
        } else {
            // Disable all descriptor checkboxes and their line edits
            VIDCheckBox->setEnabled(false);
            VIDCheckBox->setChecked(false);

            PIDCheckBox->setEnabled(false);
            PIDCheckBox->setChecked(false);

            USBSerialNumberCheckBox->setEnabled(false);
            USBSerialNumberCheckBox->setChecked(false);
        }
    }

    // Original functionality for individual checkbox-lineEdit pairs
    if (lineEdit) {
        lineEdit->setEnabled(state == Qt::Checked);
    }

    // Any checkbox state change marks the page dirty
    markDirty();
}

void TargetControlPage::applySettings()
{
    qCDebug(log_target_control) << "[TargetControlPage] applySettings() called";
    qCDebug(log_target_control) << "[TargetControlPage] Current thread:" << QThread::currentThread();
    qCDebug(log_target_control) << "[TargetControlPage] QApplication thread:" << qApp->thread();

    QSettings settings("Techxartisan", "Openterface");

    // Save the selected operating mode
    int selectedMode = operatingModeGroup->checkedId();
    qCDebug(log_target_control) << "[TargetControlPage] Selected operating mode:" << selectedMode;

    settings.setValue("hardware/operatingMode", selectedMode);
    GlobalSetting::instance().setOperatingMode(selectedMode);

    QLineEdit *VIDLineEdit = this->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = this->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = this->findChild<QLineEdit*>("VIDDescriptorLineEdit");
    QLineEdit *PIDDescriptorLineEdit = this->findChild<QLineEdit*>("PIDDescriptorLineEdit");
    QLineEdit *serialNumberLineEdit = this->findChild<QLineEdit*>("serialNumberLineEdit");

    QByteArray EnableFlag = convertCheckBoxValueToBytes();

    GlobalSetting::instance().setVID(VIDLineEdit->text());
    GlobalSetting::instance().setPID(PIDLineEdit->text());
    GlobalSetting::instance().setCustomVIDDescriptor(VIDDescriptorLineEdit->text());
    GlobalSetting::instance().setCustomPIDDescriptor(PIDDescriptorLineEdit->text());
    GlobalSetting::instance().setSerialNumber(serialNumberLineEdit->text());
    GlobalSetting::instance().setUSBEnabelFlag(QString(EnableFlag.toHex()));

    // Store current baudrate for use in worker thread
    int currentBaudrate = SerialPortManager::getInstance().getCurrentBaudrate();
    qCDebug(log_target_control) << "[TargetControlPage] Current baudrate from SerialPortManager:" << currentBaudrate;

    bool needFactoryReset = (selectedMode != originalOperatingMode);
    qCDebug(log_target_control) << "[TargetControlPage] Mode changed:" << (selectedMode != originalOperatingMode);
    qCDebug(log_target_control) << "[TargetControlPage] Need factory reset:" << needFactoryReset;

    // Emit signal to worker thread for thread-safe serial port operations
    //Qt::QueuedConnection ensures the slot runs in the worker thread
    qCDebug(log_target_control) << "[TargetControlPage] Emitting requestApplyHardwareSetting signal...";
    emit SerialPortManager::getInstance().requestApplyHardwareSetting(currentBaudrate, selectedMode, needFactoryReset);
    qCDebug(log_target_control) << "[TargetControlPage] Signal emitted successfully";

    originalOperatingMode = selectedMode;
    captureSnapshot();
    clearDirty();
    qCDebug(log_target_control) << "[TargetControlPage] applySettings() completed";
}

QByteArray TargetControlPage::convertCheckBoxValueToBytes(){
    QCheckBox *VIDCheckBox = this->findChild<QCheckBox *>("VIDCheckBox");
    QCheckBox *PIDCheckBox = this->findChild<QCheckBox *>("PIDCheckBox");
    QCheckBox *USBSerialNumberCheckBox = this->findChild<QCheckBox *>("USBSerialNumberCheckBox");
    QCheckBox *USBCustomStringDescriptorCheckBox = this->findChild<QCheckBox *>("USBCustomStringDescriptorCheckBox");

    bool bit0 = USBSerialNumberCheckBox->isChecked();
    bool bit1 = PIDCheckBox->isChecked();
    bool bit2 = VIDCheckBox->isChecked();
    bool bit7 = USBCustomStringDescriptorCheckBox->isChecked();

    quint8 byteValue = (bit7 << 7) | (bit2 << 2) | (bit1 << 1) | bit0;
    QByteArray hexValue;
    hexValue.append(byteValue);

    return hexValue;
}

void TargetControlPage::initHardwareSetting()
{
    QSettings settings("Techxartisan", "Openterface");

    // Initialize operating mode
    int operatingMode = settings.value("hardware/operatingMode", 2).toInt(); // Default to keyboard+mouse mode (2)
    QAbstractButton *button = operatingModeGroup->button(operatingMode);
    if (button) {
        button->setChecked(true);
    } else {
        keyboardMouseRadio->setChecked(true); // Default to keyboard+mouse mode
        operatingMode = 2; // Set default value if button not found
    }

    // Store the original operating mode for change detection
    originalOperatingMode = operatingMode;

    QCheckBox *VIDCheckBox = this->findChild<QCheckBox*>("VIDCheckBox");
    QCheckBox *PIDCheckBox = this->findChild<QCheckBox*>("PIDCheckBox");
    QCheckBox *USBSerialNumberCheckBox = this->findChild<QCheckBox*>("USBSerialNumberCheckBox");
    QCheckBox *USBCustomStringDescriptorCheckBox = this->findChild<QCheckBox*>("USBCustomStringDescriptorCheckBox");

    QLineEdit *VIDLineEdit = this->findChild<QLineEdit*>("VIDLineEdit");
    QLineEdit *PIDLineEdit = this->findChild<QLineEdit*>("PIDLineEdit");
    QLineEdit *VIDDescriptorLineEdit = USBCheckBoxEditMap.value(VIDCheckBox);
    QLineEdit *PIDDescriptorLineEdit = USBCheckBoxEditMap.value(PIDCheckBox);
    QLineEdit *serialNumberLineEdit = USBCheckBoxEditMap.value(USBSerialNumberCheckBox);

    QString USBFlag = settings.value("serial/enableflag", "87").toString();
    std::array<bool, 4> enableFlagArray = extractBits(USBFlag);

    for(uint i = 0; i < enableFlagArray.size(); i++){
    }

    VIDCheckBox->setChecked(enableFlagArray[2]);
    PIDCheckBox->setChecked(enableFlagArray[1]);
    USBSerialNumberCheckBox->setChecked(enableFlagArray[0]);
    USBCustomStringDescriptorCheckBox->setChecked(enableFlagArray[3]);

    // Make the descriptor checkboxes enabled/disabled based on the master toggle
    VIDCheckBox->setEnabled(enableFlagArray[3]);
    PIDCheckBox->setEnabled(enableFlagArray[3]);
    USBSerialNumberCheckBox->setEnabled(enableFlagArray[3]);

    VIDDescriptorLineEdit->setText(settings.value("serial/customVIDDescriptor", "").toString());
    VIDDescriptorLineEdit->setToolTip("Product descriptor");
    PIDDescriptorLineEdit->setText(settings.value("serial/customPIDDescriptor", "").toString());
    PIDDescriptorLineEdit->setToolTip("Vendor descriptor");
    VIDLineEdit->setText(settings.value("serial/vid", "861A").toString());
    PIDLineEdit->setText(settings.value("serial/pid", "29E1").toString());
    serialNumberLineEdit->setText(settings.value("serial/serialnumber" , "").toString());
    serialNumberLineEdit->setToolTip("Serial number");
    if(USBCustomStringDescriptorCheckBox->isChecked()){
        VIDDescriptorLineEdit->setEnabled(enableFlagArray[2]);
        PIDDescriptorLineEdit->setEnabled(enableFlagArray[1]);
        serialNumberLineEdit->setEnabled(enableFlagArray[0]);
    }else{
        VIDDescriptorLineEdit->setEnabled(false);
        PIDDescriptorLineEdit->setEnabled(false);
        serialNumberLineEdit->setEnabled(false);
    }

    captureSnapshot();
    clearDirty();
}

std::array<bool, 4> TargetControlPage::extractBits(QString hexString) {
    // convert hex string to bool array
    bool ok;
    int hexValue = hexString.toInt(&ok, 16);

    if (!ok) {
        return {}; // return empty array
    }

    // get the bit
    std::array<bool, 4> bits = {
        static_cast<bool>((hexValue >> 0) & 1),
        static_cast<bool>((hexValue >> 1) & 1),
        static_cast<bool>((hexValue >> 2) & 1),
        static_cast<bool>((hexValue >> 7) & 1)
    };

    return bits;
}

void TargetControlPage::captureSnapshot()
{
    m_snap_vidChecked = VIDCheckBox->isChecked();
    m_snap_pidChecked = PIDCheckBox->isChecked();
    m_snap_vidText = VIDLineEdit->text();
    m_snap_pidText = PIDLineEdit->text();
    m_snap_vidDescriptor = VIDDescriptorLineEdit->text();
    m_snap_pidDescriptor = PIDDescriptorLineEdit->text();
    m_snap_usbSerialNumberChecked = USBSerialNumberCheckBox->isChecked();
    m_snap_serialNumberText = serialNumberLineEdit->text();
    m_snap_usbCustomStringChecked = USBCustomStringDescriptorCheckBox->isChecked();
    m_snap_operatingMode = operatingModeGroup->checkedId();
}

void TargetControlPage::revertToSnapshot()
{
    VIDCheckBox->setChecked(m_snap_vidChecked);
    PIDCheckBox->setChecked(m_snap_pidChecked);
    VIDLineEdit->setText(m_snap_vidText);
    PIDLineEdit->setText(m_snap_pidText);
    VIDDescriptorLineEdit->setText(m_snap_vidDescriptor);
    PIDDescriptorLineEdit->setText(m_snap_pidDescriptor);
    USBSerialNumberCheckBox->setChecked(m_snap_usbSerialNumberChecked);
    serialNumberLineEdit->setText(m_snap_serialNumberText);
    USBCustomStringDescriptorCheckBox->setChecked(m_snap_usbCustomStringChecked);

    // Restore operating mode radio button
    QAbstractButton *button = operatingModeGroup->button(m_snap_operatingMode);
    if (button) button->setChecked(true);

    clearDirty();
}

bool TargetControlPage::valuesMatchSnapshot() const
{
    return VIDCheckBox->isChecked() == m_snap_vidChecked
        && PIDCheckBox->isChecked() == m_snap_pidChecked
        && VIDLineEdit->text() == m_snap_vidText
        && PIDLineEdit->text() == m_snap_pidText
        && VIDDescriptorLineEdit->text() == m_snap_vidDescriptor
        && PIDDescriptorLineEdit->text() == m_snap_pidDescriptor
        && USBSerialNumberCheckBox->isChecked() == m_snap_usbSerialNumberChecked
        && serialNumberLineEdit->text() == m_snap_serialNumberText
        && USBCustomStringDescriptorCheckBox->isChecked() == m_snap_usbCustomStringChecked
        && operatingModeGroup->checkedId() == m_snap_operatingMode;
}
