# Preferences Dialog Redesign — Design Spec

## Overview

合并 `SettingDialog`（Preferences）和 `AdvancedSettingsDialog` 为统一的 Preferences 对话框，并将底部全局 OK/Apply/Cancel 三个按钮改为每个设置页内嵌自己的三个按钮：**Apply（应用）/ Revert（恢复）/ Cancel（关闭）**。

## 核心决策

| 决策项 | 结论 |
|--------|------|
| Revert 按钮语义 | **快照模式（B 方案）**：记录页面打开时的值作为基准，Revert 恢复到该快照。Apply 之后不更新快照。 |
| 按钮名称 | **Revert**（中文：恢复） |
| 对话框合并方式 | **平铺到同一个列表**，不再区分 General / Advanced |
| 页面分类 | 设置页（需要三按钮）vs 操作页（保留现有按钮） |

## 页面清单

合并后的 Preferences 对话框包含以下页面：

| 页面 | 来源 | 类型 | 是否需要 Apply / Revert / Cancel |
|------|------|------|----------------------------------|
| General（原 LogPage） | SettingDialog | 设置页 | ✅ 需要 |
| Video | SettingDialog | 设置页 | ✅ 需要 |
| Audio | SettingDialog | 设置页 | ✅ 需要 |
| Target Control | SettingDialog | 设置页 | ✅ 需要 |
| MCP | AdvancedSettingsDialog | 设置页 | ✅ 需要 |
| Video Firmware | AdvancedSettingsDialog | 操作页 | ❌ 保留现有按钮 |
| Control Chip Firmware | AdvancedSettingsDialog | 操作页 | ❌ 保留现有按钮 |
| EDID Configuration | AdvancedSettingsDialog | 操作页 | ❌ 保留现有按钮 |
| Virtual Keyboard | AdvancedSettingsDialog | 操作页 | ❌ 保留现有按钮（拖拽编辑 + 自动保存） |

**分类标准：**
- **设置页** = 用户修改控件值，Apply 把值写到 GlobalSetting，Revert 丢弃未保存的改动
- **操作页** = 触发硬件操作（固件读写、EDID 写入等），本身是动作，不需要"保存/恢复"语义

## 按钮栏设计

### 布局

每个设置页底部新增按钮栏：

```
[ Apply (应用) ]  [ Revert (恢复) ]  [ Cancel (关闭) ]
```

- 按钮栏右对齐（通过左侧 `addStretch()`）
- 按钮尺寸：固定 80×30（与现有风格一致）
- 按钮栏是页面自己布局的一部分

### 三个按钮的语义

| 按钮 | 中文名 | 功能 |
|------|--------|------|
| **Apply** | 应用 | 把当前控件的值写入 GlobalSetting（持久化 + 立即生效），不关闭对话框 |
| **Revert** | 恢复 | 把所有控件恢复到打开页面时的快照值（丢弃所有未保存的修改） |
| **Cancel** | 关闭 | 关闭整个对话框，不保存任何当前未 Apply 的修改 |

### Snapshot（快照）机制

每个设置页需要实现三个方法：

```cpp
void captureSnapshot();   // 记录打开时所有控件的当前值
void applySettings();     // 把控件当前值写入 GlobalSetting（已有）
void revertToSnapshot();  // 把控件恢复到 captureSnapshot 记录的快照值
```

**关键规则：**

1. `captureSnapshot()` 在页面初始化时调用一次（即 `init*Settings()` 内部末尾）
2. **Apply 之后不更新快照** — 这是选择的 B 方案语义
3. 快照用类型安全的成员变量存储每个控件的值

**示例（McpPage 的快照）：**

```cpp
// 快照成员
bool    m_snap_enableChecked;
int     m_snap_transportIndex;
int     m_snap_ssePort;
int     m_snap_sseBindPresetIndex;
QString m_snap_sseBindCustom;
// ... 其余 SSE 字段

void McpPage::captureSnapshot() {
    m_snap_enableChecked      = m_enableCheckBox->isChecked();
    m_snap_transportIndex     = m_transportCombo->currentIndex();
    m_snap_ssePort            = m_ssePortSpin->value();
    m_snap_sseBindPresetIndex = m_sseBindPresetCombo->currentIndex();
    m_snap_sseBindCustom      = m_sseBindCustomEdit->text();
    // ...
}

void McpPage::revertToSnapshot() {
    m_enableCheckBox->setChecked(m_snap_enableChecked);
    m_transportCombo->setCurrentIndex(m_snap_transportIndex);
    m_ssePortSpin->setValue(m_snap_ssePort);
    m_sseBindPresetCombo->setCurrentIndex(m_snap_sseBindPresetIndex);
    m_sseBindCustomEdit->setText(m_snap_sseBindCustom);
    // ... 触发关联的 UI 状态更新（如 SSE group 的显示/隐藏）
}
```

### 按钮栏辅助函数

为了避免在每个页面重复写按钮栏创建代码，在 `SettingDialog` 中提供辅助函数：

```cpp
struct PageButtons {
    QPushButton *applyButton;
    QPushButton *revertButton;
    QPushButton *cancelButton;
};

PageButtons SettingDialog::createPageButtons(
    QWidget *page,
    std::function<void()> onApply,
    std::function<void()> onRevert
);
```

每个设置页在 `setupUI()` 末尾调用这个辅助函数，传入自己的 Apply 和 Revert 回调。

### Cancel 按钮的统一处理

Cancel 按钮连接到 `QDialog::reject()`，关闭对话框。所有页面的 Cancel 行为一致。

**注意：** Cancel 关闭前不弹窗询问是否保存。如果用户改动了但没 Apply，直接丢弃。

## 各页面具体改动

### LogPage（General 页面）

- 在 `setupUI()` 末尾添加按钮栏
- 新增 `captureSnapshot()` 和 `revertToSnapshot()` 方法
- 在 `initLogSettings()` 末尾调用 `captureSnapshot()`

**快照字段：**

```cpp
bool m_snap_coreLog, m_snap_serialLog, m_snap_uiLog, m_snap_hostLog;
bool m_snap_deviceLog, m_snap_backendLog, m_snap_scriptLog;
bool m_snap_storeLog;
QString m_snap_logFilePath;
bool m_snap_screenSaver;
bool m_snap_hideKeyboardInput;
bool m_snap_floatingWindow;
int m_snap_floatingWindowOpacity;
bool m_snap_systemKeyBlocker;
```

**Apply 回调：** 调用现有的 `applyLogsettings()`
**Revert 回调：** 调用 `revertToSnapshot()`

### VideoPage

- 在 `setupUI()` 末尾添加按钮栏
- 新增 `captureSnapshot()` 和 `revertToSnapshot()` 方法
- 在 `initVideoSettings()` 末尾调用 `captureSnapshot()`

**快照字段：**

```cpp
int m_snap_videoFormatIndex;
int m_snap_pixelFormatIndex;
int m_snap_fpsIndex;
QSize m_snap_resolution;
bool m_snap_customResolutionChecked;
int m_snap_customWidth, m_snap_customHeight;
// ... 其余视频相关控件
```

**Apply 回调：** 调用现有的 `applyVideoSettings()`
**Revert 回调：** 调用 `revertToSnapshot()`

### AudioPage

- 在 `setupUI()` 末尾添加按钮栏
- 新增 `captureSnapshot()` 和 `revertToSnapshot()` 方法
- 在 `loadSettings()` 末尾调用 `captureSnapshot()`

**快照字段：**

```cpp
int m_snap_audioCodecIndex;
int m_snap_sampleRate;
int m_snap_quality;
int m_snap_containerFormatIndex;
int m_snap_audioDeviceIndex;
int m_snap_audioBitrate;
bool m_snap_enableAudio;
int m_snap_volume;
```

**Apply 回调：** 调用现有的 `saveSettings()`
**Revert 回调：** 调用 `revertToSnapshot()`

### TargetControlPage

- 在 `setupUI()` 末尾添加按钮栏
- 新增 `captureSnapshot()` 和 `revertToSnapshot()` 方法
- 在 `initHardwareSetting()` 末尾调用 `captureSnapshot()`

**快照字段：**

```cpp
bool m_snap_vidChecked, m_snap_pidChecked;
QString m_snap_vidText, m_snap_pidText;
QString m_snap_vidDescriptor, m_snap_pidDescriptor;
bool m_snap_usbSerialNumberChecked;
QString m_snap_serialNumberText;
bool m_snap_usbCustomStringChecked;
int m_snap_operatingMode;  // 0=Full, 1=KeyboardOnly, 2=KeyboardMouse, 3=CustomHID
```

**Apply 回调：** 调用现有的 `applyHardwareSetting()`
**Revert 回调：** 调用 `revertToSnapshot()`

### McpPage

- 在 `setupUI()` 末尾添加按钮栏
- 新增 `captureSnapshot()` 和 `revertToSnapshot()` 方法
- 在 `initMcpSettings()` 末尾调用 `captureSnapshot()`

**快照字段：**

```cpp
bool m_snap_enableChecked;
int m_snap_transportIndex;
int m_snap_ssePort;
int m_snap_sseBindPresetIndex;
QString m_snap_sseBindCustom;
QString m_snap_ssePathSse;
QString m_snap_ssePathMessages;
int m_snap_sseKeepalive;
int m_snap_sseSessionTimeout;
int m_snap_sseCleanupInterval;
int m_snap_sseMaxSessions;
```

**Apply 回调：** 调用现有的 `applyMcpSettings()`
**Revert 回调：** 调用 `revertToSnapshot()`

### 操作页（不改）

以下页面不添加 Apply/Revert/Cancel 按钮栏，保留现有按钮：

- **FirmwarePage** — 保留 Check for Updates / Backup / Write / Cancel
- **ControlChipFirmwarePage** — 保留 Scan / Connect / Disconnect / Flash
- **EdidConfigPage** — 保留 Read / Apply / Cancel Reading（这是固件操作，不是设置保存）
- **VirtualKeyboardPage** — 保留拖拽编辑 + 自动保存机制

## 对话框合并实现

### SettingDialog 重构

类名保持 `SettingDialog` 不变。

**新增成员（从 AdvancedSettingsDialog 迁移）：**

```cpp
FirmwarePage *firmwarePage;
ControlChipFirmwarePage *controlChipFirmwarePage;
McpPage *mcpPage;
EdidConfigPage *edidConfigPage;
VirtualKeyboardPage *virtualKeyboardPage;
```

**createSettingTree() 修改：**

```cpp
QStringList names = {
    tr("General"),              // 0 - LogPage
    tr("Video"),                // 1 - VideoPage
    tr("Audio"),                // 2 - AudioPage
    tr("Target Control"),       // 3 - TargetControlPage
    tr("MCP"),                  // 4 - McpPage
    tr("Video Firmware"),       // 5 - FirmwarePage
    tr("Control Chip Firmware"),// 6 - ControlChipFirmwarePage
    tr("EDID Configuration"),   // 7 - EdidConfigPage
    tr("Virtual Keyboard")      // 8 - VirtualKeyboardPage
};
```

**createPages() 修改：** 依次 addScrollablePage 所有 9 个页面。

**删除：**
- `buttonWidget` 成员
- `createButtons()` 方法
- `handleOkButton()` 方法
- `applyAccrodingPage()` 方法

**changePage() 修改：** 支持 9 个页面的索引映射。

### 对外接口

保留原有 getter 方法 + 新增迁移页面的 getter：

```cpp
TargetControlPage* getTargetControlPage();
VideoPage* getVideoPage();
LogPage* getLogPage();
McpPage* getMcpPage();                      // 新增
FirmwarePage* getFirmwarePage();            // 新增
VirtualKeyboardPage* getVirtualKeyboardPage();  // 新增
```

新增可选的 `selectPage(const QString& pageName)` 方法。

## MainWindow 改动

### 合并对话框入口

- 保留 `configureSettings()` 打开合并后的 `SettingDialog`
- 删除 `configureAdvancedSettings()` 方法
- 原来调用 `configureAdvancedSettings()` 的菜单项改为调用 `configureSettings()`

### 信号连接重构

在 `configureSettings()` 中合并所有信号连接：

```cpp
void MainWindow::configureSettings() {
    if (!settingDialog) {
        settingDialog = new SettingDialog(m_cameraManager, this);

        // 原有连接（LogPage, VideoPage）
        LogPage* logPage = settingDialog->getLogPage();
        connect(logPage, &LogPage::ScreenSaverInhibitedChanged, ...);
        // ... 其余 LogPage 连接
        
        VideoPage* videoPage = settingDialog->getVideoPage();
        connect(videoPage, &VideoPage::videoSettingsChanged, ...);
        
        // 新增连接（从 AdvancedSettingsDialog 迁移过来的）
        McpPage* mcpPage = settingDialog->getMcpPage();
        connect(mcpPage, &McpPage::mcpSettingsChanged, this, &MainWindow::onMcpSettingsApplied);

        FirmwarePage* firmwarePage = settingDialog->getFirmwarePage();
        connect(firmwarePage, &FirmwarePage::firmwareUpdateCompleted,
                this, []() { QApplication::quit(); });
        
        // 对话框关闭清理
        connect(settingDialog, &QDialog::finished, this, [this]() {
            settingDialog->deleteLater();
            settingDialog = nullptr;
        });
        
        settingDialog->show();
    } else {
        settingDialog->raise();
        settingDialog->activateWindow();
    }
}
```

### 头文件改动

**mainwindow.h：**
- 删除 `class AdvancedSettingsDialog;` 前向声明
- 删除 `AdvancedSettingsDialog *advancedSettingsDialog;` 成员变量
- 删除 `void configureAdvancedSettings();` 方法声明

**mainwindow.cpp：**
- 删除 `#include "ui/preferences/advancedsettingsdialog.h"`
- 删除 `configureAdvancedSettings()` 方法实现
- 更新菜单连接

## 构建系统与文件清单

### CMakeLists.txt 改动

从源文件列表中删除：
```cmake
ui/preferences/advancedsettingsdialog.cpp
ui/preferences/advancedsettingsdialog.h
```

### 头文件改动汇总

| 文件 | 改动 |
|------|------|
| `settingdialog.h` | 新增 5 个页面成员；删除 `buttonWidget`；删除 `createButtons()`, `handleOkButton()`, `applyAccrodingPage()`；新增 `createPageButtons()` 辅助方法；新增 getter 方法；新增 `selectPage()` |
| `logpage.h` | 新增 `captureSnapshot()`, `revertToSnapshot()` 方法声明；新增 `m_snap_*` 快照成员 |
| `videopage.h` | 同上 |
| `audiopage.h` | 同上 |
| `targetcontrolpage.h` | 同上 |
| `mcppage.h` | 同上 |
| `mainwindow.h` | 删除 `AdvancedSettingsDialog` 相关声明 |

### 源文件改动汇总

| 文件 | 改动 |
|------|------|
| `settingdialog.cpp` | 合并 AdvancedSettingsDialog 的页面创建和初始化逻辑；删除全局按钮栏代码；changePage() 扩展支持 9 个页面 |
| `logpage.cpp` | setupUI() 末尾添加按钮栏；实现 captureSnapshot() 和 revertToSnapshot() |
| `videopage.cpp` | 同上 |
| `audiopage.cpp` | 同上 |
| `targetcontrolpage.cpp` | 同上 |
| `mcppage.cpp` | 同上 |
| `mainwindow.cpp` | configureSettings() 中合并所有信号连接；删除 configureAdvancedSettings()；更新菜单连接 |

### 文件删除清单

```
ui/preferences/advancedsettingsdialog.cpp   # 删除
ui/preferences/advancedsettingsdialog.h     # 删除
```

### 不需要改动的文件

- `firmwarepage.cpp/h` — 不改
- `controlchipfirmwarepage.cpp/h` — 不改
- `edidconfigpage.cpp/h` — 不改
- `virtualkeyboardpage.cpp/h` — 不改

## 边界情况

### Cancel 不弹窗确认
用户在任何页面有未 Apply 的改动时点 Cancel，直接关闭对话框。不弹出 "是否保存" 的提示。

### 多次打开对话框
每次打开对话框，所有设置页都会重新 `init*Settings()`，从 GlobalSetting 读取最新值并调用 `captureSnapshot()`。快照总是最新的磁盘值。

### Apply 后立即 Revert
Apply 把值写入 GlobalSetting。Revert 恢复到打开时的快照（不更新快照）。所以如果用户改了 A → Apply → 改了 B → Revert → 回到 A（不是 Apply 之后的 A'）。

### 切换页面时没有未保存提示
切换页面时直接切换，当前页面的未 Apply 改动保留在控件中。用户可以在当前页面继续改，也可以点 Revert 丢弃。

### 操作进行中关闭对话框
FirmwarePage 等硬件操作页的阻止关闭逻辑属于它们自身的职责，不在本次设计范围内。

## 测试策略

### 手动测试清单

**基础功能：**
1. 打开 Preferences，确认 9 个页面都在左侧树中
2. 点击每个页面，确认能正常切换
3. 确认每个设置页底部都有三个按钮
4. 确认操作页保留原有按钮

**Apply / Revert / Cancel 测试：**
5. 在 General 页面改值 → Apply → 确认 GlobalSetting 被更新
6. 在 General 页面改值 → Revert → 确认控件恢复到打开时的值
7. 在 General 页面改值 → Cancel → 确认对话框关闭，GlobalSetting 未被修改
8. 重新打开对话框，确认上一步的改动确实没保存

**MCP 页面专项：**
9. 改传输模式 → Apply → 确认 mcpSettingsChanged 信号被触发
10. 改 SSE 端口 → Revert → 确认端口恢复
11. 改多个值 → Cancel → 确认对话框关闭，MCP 配置不变

**跨页面：**
12. 在 Video 页面改值 → 切到 Audio → 切回 Video → 确认 Video 的改动还在
13. 在多个页面改值 → Cancel → 确认所有页面都没保存

**信号连接：**
14. 修改 MCP 配置并 Apply → 确认 MCP 服务器重启
15. 修改 System Key Blocker 并 Apply → 确认行为变化

### 回归测试

- 原有 SettingDialog 的功能不应受影响
- 原 AdvancedSettingsDialog 中的页面功能不应受影响
