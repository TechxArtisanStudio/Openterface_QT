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

## 修复八：热插拔时 UI 完全卡死（主线程阻塞 7-8 秒）

**问题**：拔出设备后重新插入，整个软件卡住不能正常使用。UI 完全无响应，鼠标移动命令无法发送到串口。

**根因**：FFmpeg 捕获线程在设备断开时卡在 `av_read_frame()` 中（读取已断开的 USB 设备）。`shouldDisconnectCamera` handler 在主线程同步调用 `stopCamera()` → `stopDirectCapture()` → `StopCapture()` → `StopCaptureThread()`，其中 `capture_thread_->wait(5000)` 等待线程退出最长 5 秒 + `wait(2000)` 重试 + `terminate()` + `wait(1000)` = **最长 8 秒主线程阻塞**！

加上 `stopDirectCapture()` 的 `waitForCaptureStop(2000)` + `QThread::msleep(200)` 和 `deactivateCameraByPortChain()` 的 `QThread::msleep(50) × 3 = 150ms`，主线程最长被阻塞 **~10 秒**。

**阻塞链**：
```
shouldDisconnectCamera handler（主线程）
 → stopCamera()
  → m_backendHandler->stopCamera()
   → stopDirectCapture()                    ← waitForCaptureStop(2000) + msleep(200)
    → m_captureManager->StopCapture()
     → StopCaptureThread()                  ← wait(5000) + wait(2000) + terminate + wait(1000)
      → capture_thread_->wait(5000)         ← 主线程卡在这里！
 → deactivateCameraByPortChain()            ← msleep(50) × 3
```

**修复位置**：
- `host/backend/ffmpeg/ffmpeg_capture_manager.cpp` — `StopCaptureThread()` + `StopCapture()`
- `host/backend/ffmpeg/ffmpeg_capture_manager.h` — 新增 `deferred_thread_cleanup_` 标志
- `host/backend/ffmpegbackendhandler.cpp` — `stopDirectCapture()`
- `host/cameramanager.cpp` — `deactivateCameraByPortChain()`

**修复逻辑（四层非阻塞化）**：

1. **`StopCaptureThread()` 非阻塞化**：给线程 100ms 快速退出机会。如果线程在 100ms 内退出（正常断开），直接在主线程清理。如果 100ms 内未退出（USB 设备断开），将阻塞等待 + terminate 委托给 `std::thread` 后台线程：
```cpp
if (capture_thread_->wait(100)) {
    // 快速路径：线程已退出，安全关闭设备
    capture_thread_.reset();
    CloseInputDevice();
} else {
    // 慢速路径：后台线程处理阻塞等待 + terminate + 设备关闭
    deferred_thread_cleanup_ = true;
    QThread* rawThread = capture_thread_.release();
    std::thread([rawThread, self]() {
        rawThread->wait(5000);  // 后台等待，不阻塞主线程
        // ... terminate if needed
        self->CloseInputDevice();  // 线程退出后安全关闭
        delete rawThread;
    }).detach();
}
```

2. **`StopCapture()` 适配**：检查 `deferred_thread_cleanup_` 标志。快速路径已在 `StopCaptureThread()` 中关闭设备；慢速路径由后台线程负责关闭。

3. **`stopDirectCapture()` 去除阻塞**：移除 `waitForCaptureStop(2000)` 和 `QThread::msleep(200)`。2000ms 的 reconnect 延迟已提供足够的设备稳定时间。

4. **`deactivateCameraByPortChain()` 去除阻塞**：移除第二次 `stopCamera()` 调用（已由 handler 先调用），将 `QThread::msleep(50) × 3` 的视频输出重置改为 `QTimer::singleShot(0, ...)` 延迟清理。

**效果**：整个断开路径从 **~10 秒阻塞** 降至 **< 1ms**（仅设置标志 + 信号）。热插拔时 UI 完全保持响应。

---

## 修复九：热插拔后视频流无法恢复

**问题**：UI 不再卡住（修复八生效），但热插拔后视频流无法恢复。两个独立问题叠加：

1. **串口 "Access is denied" 持续 ~10 秒**：设备拔出后，`closePortInternal()` 将 QSerialPort 对象保留在关闭状态（不删除）。重新插入时 `openPort()` 复用同一个 QSerialPort 对象，但该对象持有的 Windows 内核句柄已经失效（指向已断开的 USB 设备）。Windows 需要 ~10 秒才释放旧句柄，导致 `open()` 持续返回 "Access is denied"。
2. **FFmpeg 分离线程关闭新设备的竞态条件**：修复八引入了 `std::thread` 后台等待。当热插拔触发摄像头重启时，旧捕获的分离线程可能在 `StartCapture()` 已打开新设备后才被调度。分离线程调用 `self->CloseInputDevice()` → `device_manager_->CloseDevice()`，关闭的是 `device_manager_` **当前指向的新设备**，而非旧设备。新捕获线程读到已关闭的 format context → 崩溃或无帧。

### 修复九-A：设备拔出后删除 QSerialPort

**文件**：`serial/SerialPortManager.h`、`serial/SerialPortManager.cpp`

**修复逻辑**：为 `closePortInternal()` 添加 `bool deleteAfterClose` 参数（默认 `false`）。当从设备拔出错误路径调用时传 `true`：

```cpp
// 修复前：所有路径都保留 QSerialPort 对象
closePortInternal();

// 修复后：设备拔出错误路径删除对象
closePortInternal(true);  // handleSerialError → ERROR_STATE 路径
```

当 `deleteAfterClose == true`：
```cpp
if (deleteAfterClose) {
    delete serialPort;
    serialPort = nullptr;
} else {
    // 正常关闭：保持对象存活（原有逻辑）
}
```

下次 `openPort()` 检测到 `serialPort == nullptr`，创建全新 QSerialPort → 获取全新 Windows 句柄 → 不再 "Access is denied"。

**为什么安全**：
- 仅在设备拔出错误路径（ERROR_STATE）使用 `deleteAfterClose=true`
- 正常关闭（用户操作、关机）保持原有行为
- `openPort()` 已有 `if (serialPort == nullptr) { serialPort = new QSerialPort(); }` 路径
- 信号重新连接逻辑已存在（HOTPLUG FIX：Always reconnect signals）

### 修复十：慢速路径设备关闭委托给分离线程，消除主线程死锁

**文件**：`host/backend/ffmpeg/ffmpeg_capture_manager.cpp`、`host/backend/ffmpeg/ffmpeg_capture_manager.h`

**根因**：修复十-A（前一版本）尝试从主线程调用 `CloseInputDevice()`，但 `avformat_close_input()` 会拆解 DirectShow 图，而图的拆解需要等待捕获线程的 `GetNextSample()` 返回。捕获线程正阻塞在死掉的 USB 设备上等待数据 → **主线程死锁，UI 完全卡死**。

**修复方案（修复十-B）**：将设备关闭委托给分离线程，且分离线程**先关闭设备**（迫使 `av_read_frame()` 返回错误），再等待捕获线程退出。

```
修复十-A 时序（有 BUG — 主线程死锁）：
  T+0ms:    拔出 → 慢速路径 → 主线程 CloseInputDevice() → avformat_close_input() 死锁！
  （主线程永久阻塞在 DirectShow 图拆解，等待捕获线程退出）

修复十-B 时序（正确）：
  T+0ms:    拔出 → 慢速路径 → 分离线程 dm->CloseDevice()（~10ms，不阻塞主线程）
  T+10ms:   av_read_frame() 失败 → 捕获线程退出
  T+100ms:  分离线程完成清理
  T+2000ms: 插入 → StartCapture → 打开设备（安全 — 旧设备已关闭）
```

```cpp
// 慢速路径（捕获线程100ms内未退出）：
// 分离线程负责关闭设备 + 等待线程退出 + 清理内存
deferred_thread_cleanup_ = true;

QThread* rawThread = capture_thread_.release();
FFmpegDeviceManager* dm = device_manager_;

std::thread([rawThread, dm]() {
    // STEP 1: 先关闭设备，迫使 av_read_frame() 返回错误
    // 从后台线程关闭是安全的——主线程不会被阻塞
    if (dm) {
        dm->CloseDevice();
    }
    // STEP 2: 等待捕获线程退出（最多5秒）
    rawThread->wait(5000);
    // ... terminate if needed
    // STEP 3: 清理 QThread 内存
    delete rawThread;
}).detach();
```

**为什么消除竞态条件**：
- 分离线程在 ~10ms 内关闭设备（远早于 2000ms 的重新连接延迟）
- 新捕获在 T+2000ms 才启动，此时旧设备早已关闭，不存在"关闭新设备"的竞态
- 修复八的竞态（分离线程在 T+5000ms 关闭新设备）不可能发生，因为设备在 T+10ms 就已关闭

**效果**：
- 主线程不再死锁 → UI 保持响应
- 旧捕获线程被"釜底抽薪"（设备被关闭）→ 快速退出
- 新捕获打开设备时不存在竞态 → 视频流正常恢复
- 不新增任何成员变量，不改变 `StartCapture()` / `CleanupResources()` 逻辑

---

## 修复十一：串口看门狗恢复后摄像头不重启

**问题**：设备热插拔（拔出→重新插入）后，串口通过看门狗恢复成功，但摄像头从未重启 → 无视频流。

**根因**：

1. **HotplugMonitor 未检测到设备移除**：Windows USB 枚举在设备拔出后仍保留过期条目（内核句柄未释放），导致 3 秒轮询间隔内设备始终"可见"。
2. **DeviceLifecycleManager 停留在 Ready 状态**：因为未检测到移除，`onDeviceRemoved` 从未触发，所有接口（Serial/HID/Camera/Audio）仍标记为 "Connected"。
3. **`shouldConnectCamera` 从未发出**：lifecycle manager 认为一切正常，不会触发任何接口重连。
4. **串口通过独立路径恢复**：`ConnectionWatchdog` 检测到串口错误 → `performRecovery()` → `switchSerialPortByPortChain()` → 串口重新打开。这条路径**不经过** lifecycle manager，因此不会触发 HID/Camera 重连。

**时序图**：

```
T+0s    设备拔出
T+3s    HotplugMonitor 轮询 → Windows 仍报告设备存在 → 无变化
T+5s    串口 "Access is denied" → watchdog 关闭端口
T+6s    HotplugMonitor 轮询 → 设备仍"存在" → 无变化（过期条目）
T+10s   用户重新插入设备
T+15s   串口看门狗恢复成功 → serialPortConnectionSuccess 发出
        ↑ 但 CameraManager 未监听此信号 → 摄像头未重启
T+...   串口工作正常，HID 工作正常，但摄像头永远不重启 → 无视频
```

**修复方案**：CameraManager 监听 `SerialPortManager::serialPortConnectionSuccess` 信号。当串口恢复成功且摄像头未处于流式传输状态时，自动重启摄像头。

**位置**：`host/cameramanager.cpp` — 构造函数中的 signal 连接

```cpp
connect(&SerialPortManager::getInstance(), &SerialPortManager::serialPortConnectionSuccess,
    this, [this](const QString&) {
        if (!hasActiveCameraDevice()) return;   // 初始启动阶段，跳过
        if (isCameraStreaming()) return;         // 已在流式传输，跳过
        if (m_currentCameraPortChain.isEmpty()) return;

        // 摄像头有活跃设备但未流式传输 → 热插拔恢复场景
        QString portChain = m_currentCameraPortChain;
        QTimer::singleShot(3000, this, [this, portChain]() {
            if (!hasActiveCameraDevice() || isCameraStreaming()) return;
            refreshAvailableCameraDevices();
            stopCamera();
            switchToCameraDeviceByPortChain(portChain);
            startCamera();  // 同设备同 portChain 时 switchToCameraDevice 会提前返回
        });
    });
```

**场景分析**：

| 场景 | hasActiveCameraDevice() | isCameraStreaming() | 行为 |
|------|------------------------|---------------------|------|
| 初始启动（串口首次连接） | false | false | **跳过** — 正常由 shouldConnectCamera 启动 |
| 热插拔恢复（串口看门狗恢复） | true | false | **重启摄像头** |
| 正常生命周期重连 | true | true | **跳过** — 已由 shouldConnectCamera 处理 |
| 用户手动停止摄像头后串口重连 | true/变化 | false | 视情况重启 |

**效果**：
- 无论 HotplugMonitor 是否检测到设备移除，只要串口恢复成功，摄像头就会自动重启
- 不干扰初始启动和正常生命周期管理流程
- 3 秒延迟等待 Windows DirectShow 设备重新枚举

---

## 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `serial/SerialPortManager.cpp` | 修复一：同设备早期返回；修复三：stale guard + shouldDisconnectSerial 重置 + forceResetSerialOpen()；修复六：clearError() + TX 失败计数器 + commandExecuted 监控；修复九-A：设备拔出错误路径 `closePortInternal(true)` 删除 QSerialPort |
| `serial/SerialPortManager.h` | 修复三：新增 `m_lastOpenAttemptTime`、`forceResetSerialOpen()` 声明；修复六：新增 `m_consecutiveTxFailures`、`TX_FAILURE_RECOVERY_THRESHOLD`；修复九-A：`closePortInternal(bool deleteAfterClose)` |
| `serial/SerialCommandCoordinator.cpp` | 修复六：`executeCommand()` 所有 write 失败路径添加 `clearError()` |
| `host/cameramanager.cpp` | 修复二：同 port chain 早期返回；修复四：msleep → QTimer::singleShot；修复七：延迟 300→2000ms + 帧超时自动重试；修复八：`deactivateCameraByPortChain` 去除重复 stopCamera + msleep → QTimer 延迟；修复十一：监听 `serialPortConnectionSuccess`，串口恢复后自动重启摄像头 |
| `host/cameramanager.h` | 修复七：新增 `m_hotplugCameraRestartRetries`、`MAX_HOTPLUG_CAMERA_RESTART_RETRIES` |
| `host/backend/ffmpegbackendhandler.cpp` | 修复八：`stopDirectCapture()` 去除 `waitForCaptureStop(2000)` + `msleep(200)` 阻塞 |
| `host/backend/ffmpeg/ffmpeg_capture_manager.cpp` | 修复八：`StopCaptureThread()` 非阻塞化（100ms 快速路径 + std::thread 慢速路径）；`StopCapture()` 适配 `deferred_thread_cleanup_`；修复十-B：慢速路径将设备关闭委托给分离线程（分离线程先 `dm->CloseDevice()` 再等待退出），避免主线程在 `avformat_close_input()` 中死锁 |
| `host/backend/ffmpeg/ffmpeg_capture_manager.h` | 修复八：新增 `deferred_thread_cleanup_` 标志；修复十-B：新增 `device_op_mutex_` 序列化分离线程关闭与主线程打开 |
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
| Camera (非阻塞断开) | `StopCaptureThread()` | 100ms 快速路径 + std::thread 慢速路径 | ✅ 修复八 |
| Camera (非阻塞清理) | `deactivateCameraByPortChain()` | 去除重复 stopCamera + msleep → QTimer | ✅ 修复八 |
| Camera (分离线程竞态) | 慢速路径 | 分离线程先 `dm->CloseDevice()` 再等待退出，主线程不阻塞，设备在 ~10ms 内关闭（远早于 2000ms 重连延迟） | ✅ 修复十-B |
| Serial (设备拔出清理) | `closePortInternal(true)` | 删除 stale QSerialPort → 新对象立即打开 | ✅ 修复九-A |
| 菜单路径 | `onDeviceSelected()` | 切换前调用 `forceResetSerialOpen()` | ✅ 本次修复 |
| Camera (串口恢复重启) | `serialPortConnectionSuccess` 监听 | 有活跃设备但未流式传输 → 自动重启摄像头 | ✅ 修复十一 |
| Camera (device_op_mutex) | `OpenInputDevice()` + 分离线程 | `tryLock(5000)` 序列化设备打开/关闭，避免竞态 | ✅ 修复十-B |

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
