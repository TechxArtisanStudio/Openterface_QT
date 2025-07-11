#ifndef DEVICESELECTORDIALOG_H
#define DEVICESELECTORDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QGroupBox>
#include "../device/DeviceInfo.h"
#include "../device/HotplugMonitor.h"

class DeviceManager;
class SerialPortManager;

class DeviceSelectorDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit DeviceSelectorDialog(QWidget *parent = nullptr);
    ~DeviceSelectorDialog();
    
    void setDeviceManager(DeviceManager* deviceManager);
    void setSerialPortManager(SerialPortManager* serialPortManager);
    
public slots:
    void refreshDeviceList();
    void onDeviceSelectionChanged();
    void onSelectDevice();
    void onSwitchToDevice();
    void onDeactivateDevice();
    void onShowActiveInterfaces();
    void onRefreshClicked();
    void onAutoRefreshToggled(bool enabled);
    
private slots:
    void onHotplugEvent(const DeviceChangeEvent& event);
    void autoRefreshDevices();
    
private:
    void setupUI();
    void populateDeviceList();
    void updateDeviceDetails(const DeviceInfo& device);
    void updateStatusInfo();
    QString formatDeviceListItem(const DeviceInfo& device);
    QString formatCompleteDeviceListItem(const DeviceInfo& device);
    QString formatDeviceDetails(const DeviceInfo& device);
    QIcon createDeviceStatusIcon(const DeviceInfo& device);
    QString getDeviceStatusText(const DeviceInfo& device);
    void showDeviceSelectionSuccess();
    void showActiveInterfaces();
    
    // UI components
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_buttonLayout;
    
    QGroupBox* m_deviceListGroup;
    QListWidget* m_deviceList;
    QPushButton* m_selectButton;
    QPushButton* m_switchButton;
    QPushButton* m_deactivateButton;
    QPushButton* m_showInterfacesButton;
    QPushButton* m_refreshButton;
    QPushButton* m_autoRefreshButton;
    QPushButton* m_closeButton;
    
    QGroupBox* m_deviceDetailsGroup;
    QTextEdit* m_deviceDetails;
    
    QGroupBox* m_statusGroup;
    QLabel* m_statusLabel;
    QLabel* m_hotplugStatsLabel;
    
    // Data
    DeviceManager* m_deviceManager;
    SerialPortManager* m_serialPortManager;
    QList<DeviceInfo> m_currentDevices;
    DeviceInfo m_selectedDevice;
    
    // Auto-refresh
    QTimer* m_autoRefreshTimer;
    bool m_autoRefreshEnabled;
    
    // Statistics
    int m_totalHotplugEvents;
    QDateTime m_lastEventTime;
};

#endif // DEVICESELECTORDIALOG_H
