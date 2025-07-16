#include "DeviceSelectorDialog.h"
#include "../../device/DeviceManager.h"
#include "../../device/platform/AbstractPlatformDeviceManager.h"
#ifdef _WIN32
#include "../../device/platform/WindowsDeviceManager.h"
#endif
#include "../../serial/SerialPortManager.h"
#include "../../host/cameramanager.h"
#include "../globalsetting.h"
#include <QLoggingCategory>
#include <QHeaderView>
#include <QSplitter>
#include <QCheckBox>
#include <QPainter>
#include <QMessageBox>

Q_LOGGING_CATEGORY(log_device_selector, "opf.ui.deviceselector")

DeviceSelectorDialog::DeviceSelectorDialog(CameraManager *cameraManager, QWidget *parent)
    : QDialog(parent)
    , m_serialPortManager(nullptr)
    , m_cameraManager(cameraManager)
    , m_autoRefreshTimer(new QTimer(this))
    , m_autoRefreshEnabled(false)
    , m_totalHotplugEvents(0)
{
    setWindowTitle("Openterface Device Selector");
    setMinimumSize(800, 600);
    setupUI();
    
    // Connect to DeviceManager singleton
    DeviceManager& deviceManager = DeviceManager::getInstance();
    connect(&deviceManager, &DeviceManager::devicesChanged,
            this, &DeviceSelectorDialog::refreshDeviceList);
    connect(&deviceManager, &DeviceManager::deviceAdded,
            this, [this](const DeviceInfo&) { refreshDeviceList(); });
    connect(&deviceManager, &DeviceManager::deviceRemoved,
            this, [this](const DeviceInfo&) { refreshDeviceList(); });
    
    // Setup auto-refresh timer
    m_autoRefreshTimer->setSingleShot(false);
    m_autoRefreshTimer->setInterval(3000); // 3 seconds
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &DeviceSelectorDialog::autoRefreshDevices);
    
    qCDebug(log_device_selector) << "Device Selector Dialog created";
}

DeviceSelectorDialog::~DeviceSelectorDialog()
{
    if (m_autoRefreshTimer->isActive()) {
        m_autoRefreshTimer->stop();
    }
}

void DeviceSelectorDialog::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    
    // Create main splitter
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    
    // Device list group
    m_deviceListGroup = new QGroupBox("Available Openterface Devices", this);
    QVBoxLayout* listLayout = new QVBoxLayout(m_deviceListGroup);
    
    m_deviceList = new QListWidget(this);
    m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_deviceList, &QListWidget::itemSelectionChanged, 
            this, &DeviceSelectorDialog::onDeviceSelectionChanged);
    listLayout->addWidget(m_deviceList);
    
    // List control buttons
    QHBoxLayout* listButtonLayout = new QHBoxLayout();
    m_refreshButton = new QPushButton("Refresh", this);
    m_autoRefreshButton = new QPushButton("Auto Refresh", this);
    m_autoRefreshButton->setCheckable(true);
    QPushButton* testHotplugButton = new QPushButton("Test Hotplug", this);
    QPushButton* clearCacheButton = new QPushButton("Clear Cache", this);
    QPushButton* debugUSBButton = new QPushButton("Debug USB", this);
    
    connect(m_refreshButton, &QPushButton::clicked, this, &DeviceSelectorDialog::onRefreshClicked);
    connect(m_autoRefreshButton, &QPushButton::toggled, this, &DeviceSelectorDialog::onAutoRefreshToggled);
    connect(testHotplugButton, &QPushButton::clicked, this, [this]() {
        qCDebug(log_device_selector) << "Manual hotplug test triggered";
        // Use DeviceManager singleton instead of SerialPortManager
        DeviceManager& deviceManager = DeviceManager::getInstance();
        deviceManager.checkForChanges();
    });
    connect(clearCacheButton, &QPushButton::clicked, this, [this]() {
        qCDebug(log_device_selector) << "Clearing device cache";
        DeviceManager& deviceManager = DeviceManager::getInstance();
        if (deviceManager.getPlatformManager()) {
            deviceManager.getPlatformManager()->clearCache();
            qCDebug(log_device_selector) << "Cache cleared, refreshing device list";
        } else {
            qCWarning(log_device_selector) << "No platform manager available";
        }
        refreshDeviceList();
    });
    connect(debugUSBButton, &QPushButton::clicked, this, [this]() {
        qCDebug(log_device_selector) << "Debug USB devices triggered";
        DeviceManager& deviceManager = DeviceManager::getInstance();
        if (deviceManager.getPlatformManager()) {
            auto* platformManager = deviceManager.getPlatformManager();
#ifdef _WIN32
            // Cast to Windows manager and call debug method
            auto* windowsManager = dynamic_cast<WindowsDeviceManager*>(platformManager);
            if (windowsManager) {
                windowsManager->debugListAllUSBDevices();
            } else {
                qCWarning(log_device_selector) << "Platform manager is not WindowsDeviceManager";
            }
#endif
        } else {
            qCWarning(log_device_selector) << "No device manager available for USB debug";
        }
    });
    
    listButtonLayout->addWidget(m_refreshButton);
    listButtonLayout->addWidget(m_autoRefreshButton);
    listButtonLayout->addWidget(testHotplugButton);
    listButtonLayout->addWidget(clearCacheButton);
    listButtonLayout->addWidget(debugUSBButton);
    listButtonLayout->addStretch();
    listLayout->addLayout(listButtonLayout);
    
    mainSplitter->addWidget(m_deviceListGroup);
    
    // Right panel for details and status
    QWidget* rightPanel = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    
    // Device details group
    m_deviceDetailsGroup = new QGroupBox("Device Details", this);
    QVBoxLayout* detailsLayout = new QVBoxLayout(m_deviceDetailsGroup);
    
    m_deviceDetails = new QTextEdit(this);
    m_deviceDetails->setReadOnly(true);
    m_deviceDetails->setMaximumHeight(300);
    detailsLayout->addWidget(m_deviceDetails);
    
    rightLayout->addWidget(m_deviceDetailsGroup);
    
    // Status group
    m_statusGroup = new QGroupBox("Status", this);
    QVBoxLayout* statusLayout = new QVBoxLayout(m_statusGroup);
    
    m_statusLabel = new QLabel("No devices detected", this);
    m_statusLabel->setWordWrap(true);
    statusLayout->addWidget(m_statusLabel);
    
    m_hotplugStatsLabel = new QLabel("Hotplug events: 0", this);
    statusLayout->addWidget(m_hotplugStatsLabel);
    
    rightLayout->addWidget(m_statusGroup);
    rightLayout->addStretch();
    
    mainSplitter->addWidget(rightPanel);
    mainSplitter->setSizes({400, 400});
    
    m_mainLayout->addWidget(mainSplitter);
    
    // Dialog buttons
    m_buttonLayout = new QHBoxLayout();
    m_selectButton = new QPushButton("Select Device", this);
    m_selectButton->setEnabled(false);
    m_switchButton = new QPushButton("Switch to Device", this);
    m_switchButton->setEnabled(false);
    m_deactivateButton = new QPushButton("Deactivate Current", this);
    m_showInterfacesButton = new QPushButton("Show Active Interfaces", this);
    m_closeButton = new QPushButton("Close", this);
    
    connect(m_selectButton, &QPushButton::clicked, this, &DeviceSelectorDialog::onSelectDevice);
    connect(m_switchButton, &QPushButton::clicked, this, &DeviceSelectorDialog::onSwitchToDevice);
    connect(m_deactivateButton, &QPushButton::clicked, this, &DeviceSelectorDialog::onDeactivateDevice);
    connect(m_showInterfacesButton, &QPushButton::clicked, this, &DeviceSelectorDialog::onShowActiveInterfaces);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    
    m_buttonLayout->addStretch();
    m_buttonLayout->addWidget(m_selectButton);
    m_buttonLayout->addWidget(m_switchButton);
    m_buttonLayout->addWidget(m_deactivateButton);
    m_buttonLayout->addWidget(m_showInterfacesButton);
    m_buttonLayout->addWidget(m_closeButton);
    
    m_mainLayout->addLayout(m_buttonLayout);
}

void DeviceSelectorDialog::refreshDeviceList()
{
    qCDebug(log_device_selector) << "Refreshing device list";
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    m_currentDevices = deviceManager.discoverDevices();
    populateDeviceList();
    updateStatusInfo();
}

void DeviceSelectorDialog::populateDeviceList()
{
    m_deviceList->clear();
    
    qCDebug(log_device_selector) << "Populating list with" << m_currentDevices.size() << "devices";
    
    // Additional safeguard: ensure only one device per port chain is displayed
    QMap<QString, DeviceInfo> uniqueDevicesByPortChain;
    for (const auto& device : m_currentDevices) {
        if (!device.portChain.isEmpty()) {
            // If we already have a device for this port chain, choose the one with more interfaces
            if (uniqueDevicesByPortChain.contains(device.portChain)) {
                const DeviceInfo& existing = uniqueDevicesByPortChain[device.portChain];
                if (device.getInterfaceCount() > existing.getInterfaceCount()) {
                    uniqueDevicesByPortChain[device.portChain] = device;
                    qCDebug(log_device_selector) << "Replaced device for port chain" << device.portChain 
                                                << "with more complete device (" << device.getInterfaceCount() << "vs" << existing.getInterfaceCount() << "interfaces)";
                } else {
                    qCDebug(log_device_selector) << "Keeping existing device for port chain" << device.portChain 
                                                << "(" << existing.getInterfaceCount() << "vs" << device.getInterfaceCount() << "interfaces)";
                }
            } else {
                uniqueDevicesByPortChain[device.portChain] = device;
            }
        }
    }
    
    // Update m_currentDevices to only contain the deduplicated devices
    m_currentDevices.clear();
    for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
        m_currentDevices.append(it.value());
    }
    
    qCDebug(log_device_selector) << "After deduplication:" << m_currentDevices.size() << "unique devices";
    
    for (const auto& device : m_currentDevices) {
        QString itemText = formatCompleteDeviceListItem(device);
        QListWidgetItem* item = new QListWidgetItem(itemText, m_deviceList);
        item->setData(Qt::UserRole, device.getUniqueKey());
        
        // Add device status icons
        QIcon deviceIcon = createDeviceStatusIcon(device);
        item->setIcon(deviceIcon);
        
        // Highlight if this is the currently selected device
        DeviceManager& deviceManager = DeviceManager::getInstance();
        DeviceInfo currentDevice = deviceManager.getCurrentSelectedDevice();
        if (currentDevice.isValid() && device.getUniqueKey() == currentDevice.getUniqueKey()) {
            item->setBackground(QColor(200, 255, 200)); // Light green
            item->setText(itemText + " [CURRENT]");
        }
    }
    
    // Update device details if we have a selection
    if (m_deviceList->count() > 0 && m_deviceList->currentRow() == -1) {
        m_deviceList->setCurrentRow(0);
    }
}

QString DeviceSelectorDialog::formatDeviceListItem(const DeviceInfo& device)
{
    return formatCompleteDeviceListItem(device);
}

QString DeviceSelectorDialog::formatCompleteDeviceListItem(const DeviceInfo& device)
{
    QStringList parts;
    
    // Make port chain more prominent at the start with cleaner format
    parts << QString("🔌 Port %1").arg(device.portChain);
    parts << QString("- Openterface Mini KVM");
    
    // Show all available interfaces
    QStringList interfaces;
    if (device.hasSerialPort()) {
        QString portName = device.serialPortPath;
        if (portName.startsWith("COM")) {
            interfaces << QString("Serial(%1)").arg(portName);
        } else {
            interfaces << QString("Serial(%1)").arg(portName);
        }
    }
    if (device.hasHidDevice()) interfaces << "HID";
    if (device.hasCameraDevice()) interfaces << "Camera";
    if (device.hasAudioDevice()) interfaces << "Audio";
    
    if (!interfaces.isEmpty()) {
        parts << QString("[%1]").arg(interfaces.join(" | "));
    }
    
    // Add device status
    QString status = getDeviceStatusText(device);
    if (!status.isEmpty()) {
        parts << QString("- %1").arg(status);
    }
    
    return parts.join(" ");
}

QIcon DeviceSelectorDialog::createDeviceStatusIcon(const DeviceInfo& device)
{
    // Create a composite icon showing device status
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    
    // Base device icon
    painter.fillRect(2, 2, 28, 28, QColor(100, 150, 255, 100));
    painter.setPen(QPen(QColor(50, 100, 200), 2));
    painter.drawRect(2, 2, 28, 28);
    
    // Add status indicators for each sub-device
    int xPos = 4;
    if (device.hasSerialPort()) {
        painter.fillRect(xPos, 26, 4, 4, Qt::green);
        xPos += 6;
    }
    if (device.hasHidDevice()) {
        painter.fillRect(xPos, 26, 4, 4, Qt::blue);
        xPos += 6;
    }
    if (device.hasCameraDevice()) {
        painter.fillRect(xPos, 26, 4, 4, Qt::magenta);
        xPos += 6;
    }
    if (device.hasAudioDevice()) {
        painter.fillRect(xPos, 26, 4, 4, Qt::yellow);
        xPos += 6;
    }
    
    return QIcon(pixmap);
}

QString DeviceSelectorDialog::getDeviceStatusText(const DeviceInfo& device)
{
    QStringList statusParts;
    
    int subDeviceCount = device.getInterfaceCount();
    statusParts << QString("%1/4 interfaces").arg(subDeviceCount);
    
    // Check if device is currently in use
    DeviceManager& deviceManager = DeviceManager::getInstance();
    DeviceInfo currentDevice = deviceManager.getCurrentSelectedDevice();
    if (currentDevice.isValid() && device.getUniqueKey() == currentDevice.getUniqueKey()) {
        statusParts << "ACTIVE";
    } else {
        statusParts << "Available";
    }
    
    return statusParts.join(" | ");
}

QString DeviceSelectorDialog::formatDeviceDetails(const DeviceInfo& device)
{
    if (!device.isValid()) {
        return "No device selected";
    }
    
    QStringList details;
    details << QString("<h3>🔌 Openterface Mini KVM Device</h3>");
    details << QString("<h4>USB Port: %1</h4>").arg(device.portChain);
    details << QString("<b>Device Instance ID:</b> %1").arg(device.deviceInstanceId);
    details << QString("<b>Last Seen:</b> %1").arg(device.lastSeen.toString("yyyy-MM-dd hh:mm:ss"));
    details << "";
    
    // Physical device overview
    details << "<h4>Physical Device Overview:</h4>";
    int availableInterfaces = device.getInterfaceCount();
    details << QString("<b>Available Interfaces:</b> %1/4").arg(availableInterfaces);
    details << QString("<b>Device Status:</b> %1").arg(getDeviceStatusText(device));
    details << QString("<b>Physical Location:</b> USB Port %1").arg(device.portChain);
    details << "";
    
    // Interface details
    details << "<h4>Interface Details:</h4>";
    
    if (device.hasSerialPort()) {
        details << QString("🔌 <b>Serial Interface:</b> %1").arg(device.serialPortPath);
        details << QString("   Device ID: %1").arg(device.serialPortId);
        details << QString("   Function: Control and communication");
    } else {
        details << "❌ <b>Serial Interface:</b> Not available";
    }
    
    if (device.hasHidDevice()) {
        details << QString("🖱️ <b>HID Interface:</b> Available");
        details << QString("   Device ID: %1").arg(device.hidDeviceId);
        details << QString("   Function: Keyboard/mouse control");
    } else {
        details << "❌ <b>HID Interface:</b> Not available";
    }
    
    if (device.hasCameraDevice()) {
        details << QString("📹 <b>Camera Interface:</b> Available");
        details << QString("   Device ID: %1").arg(device.cameraDeviceId);
        details << QString("   Function: Video capture");
    } else {
        details << "❌ <b>Camera Interface:</b> Not available";
    }
    
    if (device.hasAudioDevice()) {
        details << QString("🔊 <b>Audio Interface:</b> Available");
        details << QString("   Device ID: %1").arg(device.audioDeviceId);
        details << QString("   Function: Audio capture/playback");
    } else {
        details << "❌ <b>Audio Interface:</b> Not available";
    }
    
    details << "";
    
    // Usage instructions
    details << "<h4>Usage Instructions:</h4>";
    details << "• Select this device to use all available interfaces";
    details << "• The device will be activated for serial communication";
    details << "• HID interface will be available for keyboard/mouse control";
    details << "• Camera and audio interfaces will be available for capture";
    
    return details.join("<br>");
}

void DeviceSelectorDialog::updateDeviceDetails(const DeviceInfo& device)
{
    QString detailsHtml = formatDeviceDetails(device);
    m_deviceDetails->setHtml(detailsHtml);
}

void DeviceSelectorDialog::updateStatusInfo()
{
    QString status;
    if (m_currentDevices.isEmpty()) {
        status = "No Openterface devices detected";
    } else {
        // Since m_currentDevices now contains deduplicated devices, we can use its size directly
        status = QString("Found %1 physical Openterface device(s)")
                    .arg(m_currentDevices.size());
        
        // Use DeviceManager to get current selected device instead of SerialPortManager
        DeviceManager& deviceManager = DeviceManager::getInstance();
        DeviceInfo current = deviceManager.getCurrentSelectedDevice();
        if (current.isValid()) {
            status += QString("<br>Currently active: USB Port %1").arg(current.portChain);
            status += QString("<br>Active interfaces: %1").arg(current.getInterfaceSummary());
        } else {
            status += "<br>No device currently active";
        }
    }
    
    m_statusLabel->setText(status);
    
    // Update hotplug stats
    QString statsText = QString("Hotplug events: %1").arg(m_totalHotplugEvents);
    if (m_lastEventTime.isValid()) {
        statsText += QString("<br>Last event: %1").arg(m_lastEventTime.toString("hh:mm:ss"));
    }
    m_hotplugStatsLabel->setText(statsText);
}

void DeviceSelectorDialog::onDeviceSelectionChanged()
{
    QListWidgetItem* currentItem = m_deviceList->currentItem();
    if (!currentItem) {
        m_selectButton->setEnabled(false);
        m_switchButton->setEnabled(false);
        updateDeviceDetails(DeviceInfo());
        return;
    }
    
    QString deviceKey = currentItem->data(Qt::UserRole).toString();
    
    // Find the device with this key
    DeviceInfo selectedDevice;
    for (const auto& device : m_currentDevices) {
        if (device.getUniqueKey() == deviceKey) {
            selectedDevice = device;
            break;
        }
    }
    
    m_selectedDevice = selectedDevice;
    bool deviceValid = selectedDevice.isValid();
    bool isCurrentDevice = false;
    
    if (deviceValid) {
        DeviceManager& deviceManager = DeviceManager::getInstance();
        DeviceInfo currentDevice = deviceManager.getCurrentSelectedDevice();
        isCurrentDevice = (currentDevice.isValid() && 
                          currentDevice.getUniqueKey() == selectedDevice.getUniqueKey());
    }
    
    m_selectButton->setEnabled(deviceValid && !isCurrentDevice);
    m_switchButton->setEnabled(deviceValid && !isCurrentDevice);
    updateDeviceDetails(selectedDevice);
}

void DeviceSelectorDialog::onSelectDevice()
{
    if (!m_selectedDevice.isValid() || !m_serialPortManager) {
        qCWarning(log_device_selector) << "Cannot select device - invalid device or no serial port manager";
        return;
    }
    
    qCInfo(log_device_selector) << "Selecting complete physical device:" << m_selectedDevice.portChain;
    
    // Show device selection confirmation
    QMessageBox::StandardButton reply = QMessageBox::question(this, 
        "Select Physical Device", 
        QString("Select Openterface device at port %1?\n\n"
                "This will activate:\n"
                "• Serial communication: %2\n"
                "• HID interface: %3\n"
                "• Camera interface: %4\n"
                "• Audio interface: %5")
        .arg(m_selectedDevice.portChain)
        .arg(m_selectedDevice.hasSerialPort() ? m_selectedDevice.serialPortPath : "Not available")
        .arg(m_selectedDevice.hasHidDevice() ? "Available" : "Not available")
        .arg(m_selectedDevice.hasCameraDevice() ? "Available" : "Not available")
        .arg(m_selectedDevice.hasAudioDevice() ? "Available" : "Not available"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // bool success = m_serialPortManager->selectCompleteDevice(m_selectedDevice.portChain);
    // if (success) {
    //     qCInfo(log_device_selector) << "Complete device selection successful";
        
    //     // Show success message with device capabilities
    //     showDeviceSelectionSuccess();
        
    //     populateDeviceList(); // Refresh to show current selection
    //     updateStatusInfo();
    // } else {
    //     qCWarning(log_device_selector) << "Complete device selection failed";
    //     QMessageBox::warning(this, "Device Selection Failed", 
    //                         "Failed to select the device. Please try again or check device connection.");
    // }
}

void DeviceSelectorDialog::onSwitchToDevice()
{
    if (!m_selectedDevice.isValid()) {
        qCWarning(log_device_selector) << "Cannot switch device - invalid device";
        return;
    }
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    DeviceInfo currentDevice = deviceManager.getCurrentSelectedDevice();
    if (!currentDevice.isValid()) {
        // No current device, just select the new one
        onSelectDevice();
        return;
    }
    
    qCInfo(log_device_selector) << "Switching physical device from" << currentDevice.portChain 
                               << "to" << m_selectedDevice.portChain;
    
    // Show device switching confirmation
    QMessageBox::StandardButton reply = QMessageBox::question(this, 
        "Switch Physical Device", 
        QString("Switch from device at USB Port %1 to USB Port %2?\n\n"
                "Current device interfaces will be deactivated and\n"
                "new device interfaces will be activated.")
        .arg(currentDevice.portChain)
        .arg(m_selectedDevice.portChain),
        QMessageBox::Yes | QMessageBox::No);
    GlobalSetting::instance().setOpenterfacePortChain(m_selectedDevice.portChain);
    
    bool camearSwitchSuccess = false;
    if (m_cameraManager) {
        camearSwitchSuccess = m_cameraManager->switchToCameraDeviceByPortChain(m_selectedDevice.portChain);
    } else {
        qCWarning(log_device_selector) << "CameraManager is null, cannot switch camera device";
    }
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    
}

void DeviceSelectorDialog::onDeactivateDevice()
{
    DeviceManager& deviceManager = DeviceManager::getInstance();
    DeviceInfo currentDevice = deviceManager.getCurrentSelectedDevice();
    if (!currentDevice.isValid()) {
        QMessageBox::information(this, "No Active Device", "No device is currently active.");
        return;
    }
    
    // Show deactivation confirmation
    QMessageBox::StandardButton reply = QMessageBox::question(this, 
        "Deactivate Device", 
        QString("Deactivate current device at port %1?\n\n"
                "All device interfaces will be released.")
        .arg(currentDevice.portChain),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // m_serialPortManager->deactivateCurrentDevice();
    
    QMessageBox::information(this, "Device Deactivated",
                           QString("Device at port %1 has been deactivated.")
                           .arg(currentDevice.portChain));
    
    populateDeviceList(); // Refresh to show current selection
    updateStatusInfo();
}

void DeviceSelectorDialog::onShowActiveInterfaces()
{
    showActiveInterfaces();
}

void DeviceSelectorDialog::showDeviceSelectionSuccess()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Device Selected Successfully");
    msgBox.setIcon(QMessageBox::Information);
    
    QString message = QString("Openterface device at port %1 is now active!\n\n").arg(m_selectedDevice.portChain);
    
    QStringList activeInterfaces;
    if (m_selectedDevice.hasSerialPort()) activeInterfaces << QString("✓ Serial: %1").arg(m_selectedDevice.serialPortPath);
    if (m_selectedDevice.hasHidDevice()) activeInterfaces << "✓ HID: Available for keyboard/mouse";
    if (m_selectedDevice.hasCameraDevice()) activeInterfaces << "✓ Camera: Available for video capture";
    if (m_selectedDevice.hasAudioDevice()) activeInterfaces << "✓ Audio: Available for audio capture";
    
    if (!activeInterfaces.isEmpty()) {
        message += "Active interfaces:\n" + activeInterfaces.join("\n");
    }
    
    msgBox.setText(message);
    msgBox.exec();
}

void DeviceSelectorDialog::showActiveInterfaces()
{
    DeviceManager& deviceManager = DeviceManager::getInstance();
    DeviceInfo currentDevice = deviceManager.getCurrentSelectedDevice();
    if (!currentDevice.isValid()) {
        QMessageBox::information(this, "Active Interfaces", "No device is currently selected.");
        return;
    }
    
    // Show device interface information directly from DeviceInfo
    QString interfaceInfo = QString("Active Device: %1\n\nInterfaces:\n%2")
                           .arg(currentDevice.getDeviceDisplayName())
                           .arg(currentDevice.getInterfaceSummary());
    
    QMessageBox::information(this, "Active Interfaces", interfaceInfo);
}

void DeviceSelectorDialog::onRefreshClicked()
{
    refreshDeviceList();
}

void DeviceSelectorDialog::onAutoRefreshToggled(bool enabled)
{
    m_autoRefreshEnabled = enabled;
    
    if (enabled) {
        m_autoRefreshTimer->start();
        m_autoRefreshButton->setText("Auto Refresh ON");
        qCDebug(log_device_selector) << "Auto refresh enabled";
    } else {
        m_autoRefreshTimer->stop();
        m_autoRefreshButton->setText("Auto Refresh OFF");
        qCDebug(log_device_selector) << "Auto refresh disabled";
    }
}

void DeviceSelectorDialog::autoRefreshDevices()
{
    if (m_autoRefreshEnabled) {
        refreshDeviceList();
    }
}

void DeviceSelectorDialog::onHotplugEvent(const DeviceChangeEvent& event)
{
    m_totalHotplugEvents++;
    m_lastEventTime = event.timestamp;
    
    qCInfo(log_device_selector) << "Hotplug event received in DeviceSelectorDialog:" 
                                << "Added:" << event.addedDevices.size()
                                << "Removed:" << event.removedDevices.size()
                                << "Modified:" << event.modifiedDevices.size();
    
    // Log device details
    for (const auto& device : event.addedDevices) {
        qCDebug(log_device_selector) << "  + Added device:" << device.portChain << device.deviceInstanceId;
    }
    for (const auto& device : event.removedDevices) {
        qCDebug(log_device_selector) << "  - Removed device:" << device.portChain << device.deviceInstanceId;
    }
    for (const auto& pair : event.modifiedDevices) {
        qCDebug(log_device_selector) << "  * Modified device:" << pair.second.portChain << pair.second.deviceInstanceId;
    }
    
    // Auto-refresh the list when devices change
    refreshDeviceList();
}
