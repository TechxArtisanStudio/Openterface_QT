#ifndef HOTPLUGMONITOR_H
#define HOTPLUGMONITOR_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <functional>
#include "DeviceInfo.h"

class DeviceManager;

struct DeviceChangeEvent {
    QDateTime timestamp;
    QList<DeviceInfo> addedDevices;
    QList<DeviceInfo> removedDevices;
    QList<QPair<DeviceInfo, DeviceInfo>> modifiedDevices; // old, new
    QList<DeviceInfo> currentDevices;
    QList<DeviceInfo> initialDevices;
    
    bool hasChanges() const {
        return !addedDevices.isEmpty() || !removedDevices.isEmpty() || !modifiedDevices.isEmpty();
    }
};

class HotplugMonitor : public QObject
{
    Q_OBJECT
    
public:
    using ChangeCallback = std::function<void(const DeviceChangeEvent&)>;
    
    explicit HotplugMonitor(DeviceManager* deviceManager, QObject *parent = nullptr);
    ~HotplugMonitor();
    
    void addCallback(ChangeCallback callback);
    void removeCallback(ChangeCallback callback);
    void clearCallbacks();
    
    void start(int pollIntervalMs = 5000);
    void stop();
    
    bool isRunning() const { return m_running; }
    int getPollInterval() const { return m_pollInterval; }
    
    DeviceChangeEvent getCurrentState() const;
    DeviceChangeEvent getInitialState() const;
    QList<DeviceInfo> getLastSnapshot() const { return m_lastSnapshot; }
    
    // Statistics
    int getChangeEventCount() const { return m_changeEventCount; }
    QDateTime getLastChangeTime() const { return m_lastChangeTime; }
    
    // Manual trigger for testing
    void checkForChanges();
    
signals:
    void deviceChangesDetected(const DeviceChangeEvent& event);
    void newDevicePluggedIn(const DeviceInfo& device);
    void deviceUnplugged(const DeviceInfo& device);
    void monitoringStarted();
    void monitoringStopped();
    void errorOccurred(const QString& error);
    
private slots:
    void checkForChangesSlot();
    
private:
    void notifyCallbacks(const DeviceChangeEvent& event);
    DeviceChangeEvent createChangeEvent(const QList<DeviceInfo>& current, 
                                      const QList<DeviceInfo>& previous);
    
    DeviceManager* m_deviceManager;
    QTimer* m_timer;
    QList<ChangeCallback> m_callbacks;
    QList<DeviceInfo> m_lastSnapshot;
    QList<DeviceInfo> m_initialSnapshot;
    bool m_running;
    int m_pollInterval;
    int m_changeEventCount;
    QDateTime m_lastChangeTime;
};

#endif // HOTPLUGMONITOR_H
