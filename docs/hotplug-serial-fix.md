# CH32V208 热插拔问题详细分析

## 问题现象

### 1. 热插拔后 UI 卡死
拔出 CH32V208 设备后重新插入，应用程序完全无响应。

### 2. 热插拔后视频流不正确显示
设备重新插入后，摄像头画面黑屏、花屏或无法正常刷新。

### 3. 设备菜单无法切换设备
点击 Device 菜单选择对应设备时，无法正确切换，串口进入错误状态。

---

## 根因分析

### 根因一：串口强制关闭/重开导致芯片进入不可用状态

**位置**：`serial/SerialPortManager.cpp` — `switchSerialPortByPortChain()`

#### 问题代码（修复前）

```cpp
// TARGET RESTART FIX: 即使端口路径匹配且已打开，仍然强制 close/reopen
if (!m_currentSerialPortPath.isEmpty()
    && m_currentSerialPortPath == selectedDevice.serialPortPath
    && isPortOpen()) {
    qCInfo(log_core_serial_conn) << "Port path matches and port is open — forcing close/reopen"
                                 << "for fresh initialization:" << selectedDevice.serialPortPath;
}
// ↓ 没有 return，继续执行 close + reopen 逻辑
```

#### 触发场景

启动后约 2 秒，`MainWindowInitializer` 调用 `performInitialDiscovery()`，`DeviceLifecycleManager` 发现已连接的设备并创建 session，然后依次触发连接序列：

```
Serial (shouldConnectSerial) → HID (shouldConnectHid) → Camera (shouldConnectCamera) → Audio
```

`shouldConnectSerial` 处理器调用 `switchSerialPortByPortChain()`。此时串口已经在启动自动选择路径中打开并正常工作（`COM3`，`ready=true`），但上述代码**没有检查 ready 状态**，也没有 `return`，直接执行 close/reopen。

#### 级联效应

1. **串口关闭/重开** → CH32V208 芯片收到意外中断
2. **CMD_RESET 无响应** → `ready` 保持 `false`
3. **Error 6**："The device does not recognize the command"
4. **RTS 硬件复位恢复失败**
5. 串口最终标记为 Connected，但芯片实际已处于异常状态
6. 后续 HID 连接触发又一次 `shouldConnectSerial` → 又一次 close/reopen → 进一步恶化
7. 摄像头在串口错误环境中被启动，FFmpeg 捕获异常

#### 日志证据

```
[15:55:45.726] Port path matches and port is open — forcing close/reopen for fresh initialization: "COM3"
[15:55:45.736] Serial port object kept alive (closed state): 0x22532c9dbc0 (non-CH9329)
...
[15:55:46.867] TX: 57 ab 00 0f 00 Success:false
[15:55:46.867] No response to CMD_RESET — chip may still be booting, retrying...
...
[15:55:57.432] CH32V208 not responding after retries — ready stays false
[15:55:57.434] Serial port error: "The device does not recognize the command." Error code: 6
[15:55:57.439] RTS reset initiation failed — clearing recovery flag
```

### 根因二：设备菜单选择触发不必要的串口重开

**位置**：`ui/coordinator/devicecoordinator.cpp` — `onDeviceSelected()`

用户从 Device 菜单选择当前已连接的设备时，调用链如下：

```
onDeviceSelected → switchToDeviceByPortChainWithCamera
  → switchToDeviceByPortChain
    → switchSerialPortByPortChain(same port chain)  ← 强制 close/reopen！
```

由于 `switchSerialPortByPortChain` 的强制重开逻辑，选择已连接设备会导致串口被关闭再重开。如果 CH32V208 在重开后无法响应，设备选择就失败了。

### 根因三：摄像头被不必要的重启

**位置**：`host/cameramanager.cpp` — `shouldConnectCamera` 生命周期处理器

#### 问题逻辑

```cpp
// 仅检查 isCameraStreaming()，未检查 port chain 是否匹配
if (isCameraStreaming()) {
    // 已经在流式传输，跳过
    return;
}
// 否则执行完整初始化路径
switchToCameraDeviceByPortChain(portChain);  // 对同设备返回 true（无操作）
startCamera();  // ← 仍然重启 FFmpeg 捕获！
```

`isCameraStreaming()` 检查条件：
- 有活跃摄像头设备
- `m_lastFrameTimestamp > 0`（至少收到过一帧）
- 距最近一帧不超过 5 秒

在初始发现级联期间，如果帧尚未到达（`m_lastFrameTimestamp == 0`），`isCameraStreaming()` 返回 `false`，导致执行完整初始化路径。虽然 `switchToCameraDevice` 对同设备返回 `true`（无操作），但 `startCamera()` 仍然被调用，重启 FFmpeg 捕获，造成视频闪烁或黑屏。

#### 日志证据

```
[15:55:57.585] Starting connection for "Camera" on session "0001-0002"
[15:56:00.056] VideoPane: Camera active changed: false  ← FFmpeg 被重启
[15:56:00.058] VideoPane: Camera active changed: false  ← 多次重复
[15:56:00.061] "Camera" "Connecting" → "Connected"      ← 生命周期认为已连接
```

---

## 修复方案

### 修复一：串口同设备早期返回

**文件**：`serial/SerialPortManager.cpp` — `switchSerialPortByPortChain()`

**修复逻辑**：当端口路径匹配、端口已打开、`ready==true` **且 `m_portState == SerialPortState::OPEN`** 时，跳过 close/reopen，直接返回成功。

```cpp
// HOTPLUG FIX: 如果端口已经打开且匹配目标，跳过 close/reopen。
if (!m_currentSerialPortPath.isEmpty()
    && m_currentSerialPortPath == selectedDevice.serialPortPath
    && isPortOpen() && ready.load()
    && m_portState.load() == SerialPortState::OPEN) {
    qCInfo(log_core_serial_conn) << "Port path matches, port is open, ready, and state is OPEN —"
                                 << "skipping close/reopen:" << selectedDevice.serialPortPath
                                 << "(portState=" << static_cast<int>(m_portState.load()) << ")";
    // 延迟发射 serialPortConnectionSuccess，让生命周期的 connect() 有时间注册
    QString portName = selectedDevice.serialPortPath;
    QTimer::singleShot(0, this, [this, portName]() {
        emit serialPortConnectionSuccess(portName);
    });
    return true;
}
```

**关键设计**：
- `ready.load()` 检查确保芯片确实在正常响应命令
- `m_portState.load() == SerialPortState::OPEN` 检查确保端口不处于 ERROR_STATE 或 CLOSING 状态。若端口已进入错误状态（如 Error 8 "device does not exist"），即使 `ready==true`（RTS 恢复失败路径未清除 ready），也必须跳过早期返回，允许 close/reopen 恢复流程正常执行
- `QTimer::singleShot(0)` 将信号发射推迟到下一个事件循环迭代，确保生命周期处理器的 `connect()` 已注册
- 目标重启检测由 `ConnectionWatchdog` 负责（检测通信超时 → 触发 RTS 复位恢复），不依赖此处的强制重开

**RTS 恢复失败状态一致性修复**：在 `triggerRtsRecoveryForUnresponsiveDevice()` 中，当 `factoryResetHipChip()` 返回失败时，现在同时设置 `ready.store(false)`。修复前该路径仅清除 `m_rtsRecoveryInProgress` 而不影响 `ready`，导致 `ready` 可能为 `true` 而 `m_portState == ERROR_STATE` 的不一致状态，使 HOTPLUG FIX 早期返回错误触发。

### 修复二：摄像头同 port chain 早期返回

**文件**：`host/cameramanager.cpp` — `shouldConnectCamera` 生命周期处理器

**修复逻辑**：在 `isCameraStreaming()` 检查之后，增加 port chain 匹配检查。

```cpp
// HOTPLUG FIX: 如果摄像头已经在相同的 port chain 上（即使当前未流式传输），
// 跳过重启。
if (!portChain.isEmpty() && !m_currentCameraPortChain.isEmpty()
    && m_currentCameraPortChain == portChain
    && hasActiveCameraDevice()) {
    qCInfo(log_ui_camera) << "[Lifecycle] Camera already on same port chain"
                          << portChain << "— skipping restart";
    DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
        sessionKey, InterfaceType::Camera);
    return;
}
```

**为什么安全**：
- 真实热插拔时，`shouldDisconnectCamera` → `deactivateCameraByPortChain()` 会清空 `m_currentCameraPortChain`
- 因此当设备重新出现时，`m_currentCameraPortChain` 为空，不匹配条件，正常执行完整初始化路径
- 只在"初始发现级联"场景（摄像头从未被断开）中触发早期返回

---

## 修复三：`m_openInProgress` 原子锁卡死恢复

**问题**：`switchSerialPortByPortChain()` 使用 `m_openInProgress` 原子锁防止并发打开。当工作线程异常退出或设备在操作期间被拔出时，该锁可能永久卡在 `true`。此后所有切换调用（包括设备菜单选择）都被立即拒绝：

```
"Open already in progress, ignoring request for portChain: ..."
```

**修复位置**：`serial/SerialPortManager.cpp`、`serial/SerialPortManager.h`

**修复逻辑（两层防护）**：

1. **超时检测**（`switchSerialPortByPortChain`）：
```cpp
// 若 m_openInProgress 为 true 但已超过 5 秒，视为卡死，强制接管
qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastOpenAttemptTime.load();
if (elapsed > 5000) {
    qCWarning(...) << "Stale m_openInProgress detected — force resetting";
    m_openInProgress.store(true);  // 接管操作
} else {
    return false;  // 确实有操作正在进行
}
```

2. **设备移除时清理**（`shouldDisconnectSerial` handler）：
```cpp
// 在 closePort() 之前重置锁，防止热插拔后新操作被拒绝
m_openInProgress.store(false);
m_deviceUnpluggedDetected.store(false);
closePort();
```

3. **公共方法 `forceResetSerialOpen()`**：供 `DeviceCoordinator` 在菜单切换前调用，确保 Legacy 路径不被 Lifecycle 路径的锁阻塞。

---

## 修复四：消除 `shouldConnectCamera` 主线程阻塞

**问题**：`shouldConnectCamera` handler 中 `QThread::msleep(300)` 直接在主线程上阻塞 300ms，造成 UI 卡死。

**修复位置**：`host/cameramanager.cpp` — `shouldConnectCamera` handler

**修复逻辑**：将 `QThread::msleep(300)` 替换为 `QTimer::singleShot(300, this, [...] { ... })`，将后续操作（`switchToCameraDeviceByPortChain` + `startCamera`）推迟到事件循环，主线程在 300ms 延迟期间保持响应。

---

## 修复五：设备菜单切换路径防阻塞

**问题**：`DeviceCoordinator::onDeviceSelected()` 走 Legacy 路径（同步）直接调用 `switchSerialPortByPortChain()`。若 Lifecycle 路径的热插拔操作正在运行，`m_openInProgress == true` 导致菜单切换立即失败。

**修复位置**：`ui/coordinator/devicecoordinator.cpp` — `onDeviceSelected()`、`autoSelectFirstDevice()` 及重试 lambda

**修复逻辑**：在调用 `switchToDeviceByPortChainWithCamera()` 之前，先调用 `SerialPortManager::getInstance().forceResetSerialOpen()` 清除可能残留的原子锁。

---

## 修复六：串口僵尸状态快速检测与恢复

**问题**：CH32V208 的 USB 端点偶尔进入"僵尸状态"——`serialPort->write()` 持续返回 -1，但 `errorOccurred` 信号不触发。原因：
1. **缺少 `clearError()`**：write 失败后，Qt 内部保留错误状态，后续 write 可能被 Qt 直接拒绝，即使底层 USB 已恢复。这将一次瞬态故障变为永久死锁
2. **无 TX 连续失败计数器**：`ConnectionWatchdog` 的 30 秒通信超时是唯一的检测机制，太慢

**日志证据**：
```
[17:21:14.915] TX Success:true  ← 正常工作
[17:21:20.801] TX Success:true  ← 最后一次成功
[17:21:21.828] TX Success:false ← 僵尸状态开始（无任何 errorOccurred 信号）
[17:21:22.xxx] TX Success:false ← 持续失败，无恢复
...（13 秒后...）
[17:21:34.441] Error 9 timeout  ← 终于被 Watchdog 检测到（太慢！）
```

**修复位置**：
- `serial/SerialCommandCoordinator.cpp` — `executeCommand()` 方法
- `serial/SerialPortManager.cpp` — `writeDataInThread()` 方法 + `commandExecuted` lambda + `onSerialPortConnectionSuccess()` + `closePortInternal()`
- `serial/SerialPortManager.h` — 新增 `m_consecutiveTxFailures` 原子计数器

**修复逻辑（两层防护）**：

1. **`clearError()` 清除错误状态**：每次 write 失败后立即调用 `serialPort->clearError()`，确保 Qt 允许后续 write 重试，防止瞬态 USB 故障变为永久死锁

2. **TX 连续失败计数器**：
```cpp
// SerialPortManager.h
std::atomic<int> m_consecutiveTxFailures{0};
static constexpr int TX_FAILURE_RECOVERY_THRESHOLD = 3;

// commandExecuted lambda
if (success) {
    m_consecutiveTxfailures.store(0);
} else {
    int failures = m_consecutiveTxFailures.fetch_add(1) + 1;
    if (failures >= TX_FAILURE_RECOVERY_THRESHOLD) {
        qCWarning(log_core_serial_watchdog) << "Zombie state detected:" << failures
            << "consecutive TX failures — triggering fast RTS recovery";
        m_consecutiveTxFailures.store(0);
        triggerRtsRecoveryForUnresponsiveDevice();  // RTS 硬件复位
    }
}
```

3. **计数器重置**：在 `onSerialPortConnectionSuccess()` 和 `closePortInternal()` 中重置为 0，确保每次连接/恢复后从干净状态开始

**效果**：僵尸状态在 ~5 秒内检测并恢复（3 次失败 × ~1.6 秒/次），而之前需要 30+ 秒且经常无法自动恢复。

---

## 修复七：热插拔后摄像头不显示视频流

**问题**：热插拔后串口正常恢复，但摄像头 `active=true` 却不产生任何视频帧。根因三层叠加：

1. **FFmpeg 捕获未停止**：`onDeviceRemoved` 仅在 `camera.state == Connected/Connecting` 时 emit `shouldDisconnectCamera`。如果摄像头是在 lifecycle 之外启动的（启动时通过 `switchToDeviceByPortChainWithCamera`），lifecycle session 中 camera 为 `Absent`，断开时 FFmpeg 捕获不会被停止 → 持有死掉的设备句柄
2. **重启延迟太短**：`shouldConnectCamera` handler 延迟仅 300ms，Windows USB 视频设备重新枚举后需 1-3 秒才能接受捕获请求。300ms 导致 FFmpeg 要么打开一个未就绪的设备，要么 capture 线程读取空流
3. **无重试机制**：帧超时（frame timeout）只发 warning，不自动重启摄像头

**日志证据**（CH9329 设备热插拔）：
```
[18:17:42.050] Error 2 "Access is denied"   ← USB 断开瞬间
[18:17:47.355] Device removed from session   ← lifecycle 检测到移除
[18:17:53.237] Serial reconnects             ← 串口恢复
[18:17:55.151] cameraActiveChanged(true)     ← 摄像头标记活跃
[18:17:55~18:18:02] ← 无任何帧数据到达！    ← FFmpeg 死句柄
```

**修复位置**：
- `device/DeviceLifecycleManager.cpp` — `onDeviceRemoved()` + `updateSessionFromDeviceInfo()`
- `host/cameramanager.cpp` — `shouldConnectCamera` handler + frame timeout handler
- `host/cameramanager.h` — 新增 `m_hotplugCameraRestartRetries` 计数器

**修复逻辑（三层防护）**：

1. **`onDeviceRemoved` 无条件 emit `shouldDisconnectCamera`**：
```cpp
// 修复前：只在 Connected/Connecting 时 emit
if (session.camera.state == InterfaceState::Connected
    || session.camera.state == InterfaceState::Connecting) {
    emit shouldDisconnectCamera(key);
}
// 修复后：无条件 emit，确保 FFmpeg 捕获被停止
emit shouldDisconnectCamera(key);
```
配合 `updateSessionFromDeviceInfo()` 后处理：如果设备无 camera path，将 camera 标记为 `Absent`，避免 reconnect 序列浪费 26 秒尝试连接不存在的摄像头。

2. **`shouldConnectCamera` 延迟从 300ms 增至 2000ms**：
```cpp
// 修复前
QTimer::singleShot(300, this, [this, sessionKey, portChain]() {
// 修复后：给 Windows USB 视频设备足够的重新枚举时间
QTimer::singleShot(2000, this, [this, sessionKey, portChain]() {
```

3. **帧超时自动重试**：如果 10 秒内无帧到达，自动重启 FFmpeg 捕获（最多 3 次，递增延迟 2s/4s/6s）：
```cpp
// frame timeout handler
if (!m_currentCameraPortChain.isEmpty()
    && m_hotplugCameraRestartRetries < MAX_HOTPLUG_CAMERA_RESTART_RETRIES) {
    m_hotplugCameraRestartRetries++;
    stopCamera();  // 停止死句柄
    int retryDelay = m_hotplugCameraRestartRetries * 2000;
    QTimer::singleShot(retryDelay, this, [this, portChain]() {
        refreshAvailableCameraDevices();
        if (switchToCameraDeviceByPortChain(portChain)) {
            startCamera();
        }
    });
}
```

**效果**：热插拔后视频流在 2-8 秒内自动恢复（lifecycle 重启 + 重试机制），之前永远不会恢复。

---

## 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `serial/SerialPortManager.cpp` | 修复一：同设备早期返回；修复三：stale guard + shouldDisconnectSerial 重置 + forceResetSerialOpen()；修复六：clearError() + TX 失败计数器 + commandExecuted 监控 |
| `serial/SerialPortManager.h` | 修复三：新增 `m_lastOpenAttemptTime`、`forceResetSerialOpen()` 声明；修复六：新增 `m_consecutiveTxFailures`、`TX_FAILURE_RECOVERY_THRESHOLD` |
| `serial/SerialCommandCoordinator.cpp` | 修复六：`executeCommand()` 所有 write 失败路径添加 `clearError()` |
| `host/cameramanager.cpp` | 修复二：同 port chain 早期返回；修复四：msleep → QTimer::singleShot；修复七：延迟 300→2000ms + 帧超时自动重试 |
| `host/cameramanager.h` | 修复七：新增 `m_hotplugCameraRestartRetries`、`MAX_HOTPLUG_CAMERA_RESTART_RETRIES` |
| `device/DeviceLifecycleManager.cpp` | 修复七：`onDeviceRemoved` 无条件 emit shouldDisconnectCamera + `updateSessionFromDeviceInfo` camera Absent 后处理 |
| `ui/coordinator/devicecoordinator.cpp` | 修复五：菜单/自动选择切换前调用 forceResetSerialOpen() |

---

## 各子系统"同设备保护"现状

| 子系统 | 方法 | 早期返回条件 | 状态 |
|--------|------|-------------|------|
| Serial | `switchSerialPortByPortChain()` | 路径匹配 + 端口打开 + `ready==true` + `m_portState==OPEN` | ✅ 本次修复 |
| Serial (锁保护) | `m_openInProgress` stale guard | >5秒自动重置 + 设备移除时清除 | ✅ 本次修复 |
| HID | `switchToHIDDeviceByPortChain()` | 设备路径匹配 | ✅ 已有 |
| Audio | `switchToAudioDeviceByPortChain()` | 设备 ID 匹配 | ✅ 已有 |
| Camera | `switchToCameraDevice()` | 设备 ID + port chain 匹配 | ✅ 已有 |
| Camera (lifecycle) | `shouldConnectCamera` handler | 流式传输中 **或** port chain 匹配 | ✅ 本次修复 |
| Camera (非阻塞) | `shouldConnectCamera` handler | `msleep(300)` → `QTimer::singleShot(2000)` | ✅ 本次修复 |
| Camera (热插拔清理) | `onDeviceRemoved()` | 无条件 emit shouldDisconnectCamera | ✅ 修复七 |
| Camera (帧超时重试) | frame timeout handler | 自动重启 FFmpeg 捕获（3 次重试） | ✅ 修复七 |
| 菜单路径 | `onDeviceSelected()` | 切换前调用 `forceResetSerialOpen()` | ✅ 本次修复 |

---

## 验证步骤

1. **启动测试**：启动应用，等待 10 秒，确认串口正常连接、视频正常显示
2. **设备菜单测试**：点击 Device 菜单，选择当前已连接的设备，确认不会导致串口断开重连
3. **热插拔测试**：
   - 拔出设备，等待 3 秒
   - 重新插入设备
   - 确认：串口重连、视频恢复、HID 正常、**UI 在 300ms 内保持响应**
4. **多次热插拔**：连续插拔 3 次，确认每次都能正确恢复
5. **热插拔后立即菜单切换**：热插拔恢复后，立即从设备菜单选择设备，确认切换成功（`m_openInProgress` 不死锁）
6. **快速插拔**：拔出后 1 秒内重新插入，确认不出现"Open already in progress"错误
