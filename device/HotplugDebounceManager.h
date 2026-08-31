#ifndef HOTPLUG_DEBOUNCE_MANAGER_H
#define HOTPLUG_DEBOUNCE_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QString>
#include <QDateTime>

namespace device {

/**
 * @brief 设备状态枚举
 *
 * 设备状态机: Stable → Removing → Removed → Inserting → Inserted → Stable
 */
enum class DeviceState {
    Stable,      // 设备稳定状态
    Removing,    // 正在移除（防抖窗口中）
    Removed,     // 已移除
    Inserting,   // 正在插入（防抖窗口中）
    Inserted     // 已插入（防抖窗口中）
};

/**
 * @brief 设备追踪信息
 */
struct DeviceInfo {
    QString deviceId;
    QString devicePath;
    DeviceState state;
    QDateTime lastStateChange;
    int debounceCount;

    DeviceInfo() : state(DeviceState::Stable), debounceCount(0) {}
};

/**
 * @brief 热插拔防抖管理器
 *
 * 核心功能:
 * 1. 设备状态机管理
 * 2. 快速扫描窗口 (设备拔出后1分钟内每300ms扫描一次)
 * 3. 防抖机制 (500ms窗口防止重复事件)
 * 4. 快速重新插入检测
 */
class HotplugDebounceManager : public QObject
{
    Q_OBJECT

public:
    explicit HotplugDebounceManager(QObject *parent = nullptr);
    ~HotplugDebounceManager();

    // 配置参数
    static constexpr int FAST_SCAN_INTERVAL_MS = 300;      // 快速扫描间隔
    static constexpr int FAST_SCAN_DURATION_MS = 60000;    // 快速扫描持续时间 (60秒)
    static constexpr int DEBOUNCE_INTERVAL_MS = 500;         // 防抖间隔
    static constexpr int NORMAL_POLL_INTERVAL_MS = 3000;   // 正常轮询间隔

    /**
     * @brief 处理设备移除事件
     * @param deviceId 设备ID
     * @param devicePath 设备路径
     */
    void handleDeviceRemoved(const QString &deviceId, const QString &devicePath);

    /**
     * @brief 处理设备添加事件
     * @param deviceId 设备ID
     * @param devicePath 设备路径
     * @return true 如果这是快速重新插入事件
     */
    bool handleDeviceAdded(const QString &deviceId, const QString &devicePath);

    /**
     * @brief 检查是否处于快速扫描模式
     */
    bool isFastScanning() const;

    /**
     * @brief 获取当前轮询间隔
     */
    int getCurrentPollInterval() const;

    /**
     * @brief 停止快速扫描
     */
    void stopFastScan();

    /**
     * @brief 获取设备当前状态
     */
    DeviceState getDeviceState(const QString &deviceId) const;

    /**
     * @brief 强制重置所有状态
     */
    void resetAllStates();

signals:
    /**
     * @brief 设备快速重新连接信号
     *
     * 当原设备在快速扫描窗口内重新出现时触发
     */
    void deviceRapidlyReconnected(const QString &deviceId, const QString &devicePath);

    /**
     * @brief 检测到全新设备
     *
     * 在快速扫描窗口内检测到之前未记录的设备
     */
    void newDeviceDetected(const QString &deviceId, const QString &devicePath);

    /**
     * @brief 设备状态变化信号
     */
    void deviceStateChanged(const QString &deviceId, DeviceState oldState, DeviceState newState);

    /**
     * @brief 快速扫描开始信号
     */
    void fastScanStarted();

    /**
     * @brief 快速扫描结束信号
     */
    void fastScanEnded();

    /**
     * @brief 防抖触发信号
     */
    void debounceTriggered(const QString &deviceId, const QString &action);

private slots:
    void onFastScanTimeout();
    void onDebounceTimeout(const QString &deviceId);

private:
    void startFastScan();
    void setDeviceState(const QString &deviceId, DeviceState newState);
    void cleanupRemovedDevices();
    bool isSameDevice(const QString &deviceId1, const QString &deviceId2) const;
    void startDebounceTimer(const QString &deviceId);

    QTimer *m_fastScanTimer;
    QTimer *m_fastScanDurationTimer;
    QMap<QString, QTimer*> m_debounceTimers;
    QMap<QString, DeviceInfo> m_devices;
    QSet<QString> m_removedDevices;  // 记录已移除的设备ID，用于快速重新插入检测

    bool m_fastScanning;
    int m_fastScanRemainingCount;
};

} // namespace device

#endif // HOTPLUG_DEBOUNCE_MANAGER_H
