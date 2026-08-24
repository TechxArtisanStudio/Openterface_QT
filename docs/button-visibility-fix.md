# Control Firmware 页面按钮可见性修复

## 问题描述

在 Preferences > Control Firmware 页面中，Connect、Disconnect 和 Flash 按钮在未 scan device 时看不清楚，因为：
1. 按钮在初始化时被设置为禁用状态（`setEnabled(false)`）
2. 禁用的按钮在某些主题下显示为浅灰色，难以辨认
3. 没有明确的视觉提示告诉用户需要先执行什么操作

## 修复方案

### 1. 为 Connect 和 Disconnect 按钮添加样式表

**文件**: `ui/preferences/controlchipfirmwarepage.cpp`

添加了自定义样式表，使禁用状态下的按钮更清晰可见：

```cpp
QString disabledBtnStyle = R"(
    QPushButton:disabled {
        background-color: #f0f0f0;
        color: #666666;
        border: 1px solid #cccccc;
        border-radius: 4px;
        padding: 5px 15px;
    }
    QPushButton {
        background-color: #ffffff;
        color: #333333;
        border: 1px solid #bbbbbb;
        border-radius: 4px;
        padding: 5px 15px;
    }
    QPushButton:hover {
        background-color: #e8e8e8;
        border: 1px solid #999999;
    }
    QPushButton:pressed {
        background-color: #d0d0d0;
    }
)";
m_connectBtn->setStyleSheet(disabledBtnStyle);
m_disconnectBtn->setStyleSheet(disabledBtnStyle);
```

**效果**：
- 禁用状态：浅灰色背景（#f0f0f0），深灰色文本（#666666），清晰的边框
- 启用状态：白色背景，深色文本
- 鼠标悬停和点击状态也有明显的视觉反馈

### 2. 为 Flash 按钮添加样式表

```cpp
QString flashBtnStyle = R"(
    QPushButton:disabled {
        background-color: #f0f0f0;
        color: #666666;
        border: 2px solid #cccccc;
        border-radius: 4px;
        padding: 8px 20px;
        font-weight: bold;
    }
    QPushButton {
        background-color: #4CAF50;
        color: white;
        border: 2px solid #45a049;
        border-radius: 4px;
        padding: 8px 20px;
        font-weight: bold;
    }
    QPushButton:hover {
        background-color: #45a049;
    }
    QPushButton:pressed {
        background-color: #3d8b40;
    }
)";
m_flashBtn->setStyleSheet(flashBtnStyle);
```

**效果**：
- 禁用状态：浅灰色背景，深灰色文本，粗体边框
- 启用状态：绿色背景（#4CAF50），白色文本，加粗字体
- 明显的视觉区分，Flash 按钮更加突出

### 3. 添加提示信息（Tooltip）

```cpp
m_connectBtn->setToolTip(tr("Click 'Scan Devices' first to find available WCH devices"));
m_disconnectBtn->setToolTip(tr("Connect to a device first before disconnecting"));
m_flashBtn->setToolTip(tr("Connect to a device and select firmware file first"));
```

**效果**：
- 鼠标悬停在按钮上时会显示提示文本
- 明确告诉用户需要先执行什么操作
- 提升用户体验

## 按钮状态逻辑

按钮的启用/禁用逻辑保持不变：

### Connect 按钮
- **禁用条件**：已连接设备 或 未扫描到设备
- **启用条件**：未连接设备 且 已扫描到设备（`m_deviceCombo->count() > 0`）
- **状态更新**：在 `setConnectedState()` 和 `onDevicesFound()` 中更新

### Disconnect 按钮
- **禁用条件**：未连接设备
- **启用条件**：已连接设备
- **状态更新**：在 `setConnectedState()` 中更新

### Flash 按钮
- **禁用条件**：未连接设备 或 未选择固件文件 或 正在操作
- **启用条件**：已连接设备 且 已选择固件文件 且 未在进行操作
- **状态更新**：在 `updateFlashButton()` 中更新

## 视觉改进对比

### 修复前
- 禁用按钮：系统默认灰色，可能难以看清
- 无提示信息
- 按钮之间视觉差异不明显

### 修复后
- 禁用按钮：浅灰色背景，深灰色文本，清晰的边框
- 有明确的提示信息
- Connect/Disconnect 按钮与 Flash 按钮有明显的视觉区分
- 鼠标悬停和点击有视觉反馈

## 测试步骤

1. 重新编译应用：
   ```bash
   cd build
   make
   ```

2. 运行应用：
   ```bash
   ./openterfaceQT
   ```

3. 打开 Preferences > Control Firmware 页面

4. 验证：
   - ✅ Connect 和 Disconnect 按钮在未 scan 时清晰可见（灰色但可辨认）
   - ✅ Flash 按钮在未连接时清晰可见（灰色但可辨认）
   - ✅ 鼠标悬停在按钮上时显示提示信息
   - ✅ 点击 "Scan Devices" 后，如果有设备，Connect 按钮变为可用状态（白色背景）
   - ✅ 连接设备后，Disconnect 按钮变为可用状态
   - ✅ 选择固件文件后，Flash 按钮变为可用状态（绿色背景）

## 相关文件

- `ui/preferences/controlchipfirmwarepage.cpp` - 主要修改文件
- `ui/preferences/controlchipfirmwarepage.h` - 头文件（未修改）

## 兼容性

- ✅ 不影响现有功能
- ✅ 不影响按钮状态逻辑
- ✅ 只改善视觉效果
- ✅ 支持所有 Qt 主题（使用 QSS 样式表）

## 后续建议

如果需要进一步改善，可以考虑：
1. 添加动画效果，当按钮状态改变时平滑过渡
2. 使用图标增强按钮的视觉识别
3. 添加步骤指示器，显示当前操作流程（Scan → Connect → Select Firmware → Flash）
4. 在按钮旁边添加状态文本，如 "Waiting for scan..." / "Ready to connect" 等
