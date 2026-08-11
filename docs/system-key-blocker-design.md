# SystemKeyBlocker 库 — 设计文档

> **目标**：一个可手动开关、跨平台（Windows/Linux）的键盘捕获库，开启后拦截**所有**系统快捷键（Win、PrintScreen、Alt+Tab 等），让 Openterface 能获取到完整的键盘 HID 码并直接发送到目标电脑。
>
> **参考实现**：VirtualBox [UIKeyboardHandler.cpp](https://github.com/vbox/vbox/blob/main/src/VBox/Frontends/VirtualBox/src/runtime/UIKeyboardHandler.cpp)

---

## 1. 概述

### 1.1 解决的问题

Openterface 作为 KVM 软件，需要将用户在物理键盘上的**所有**按键（包括系统级快捷键）捕获后转发到目标电脑。但操作系统会优先拦截以下按键：

| 按键 | Windows 行为 | Linux 行为 |
|------|-------------|-----------|
| **Win / Super 键** | 弹出开始菜单 | 打开 Activities/Launcher |
| **Win+D** | 显示桌面 | - |
| **Win+E** | 打开文件管理器 | - |
| **Win+L** | 锁屏 | 锁屏 |
| **PrintScreen** | 截图工具 | 截图工具 |
| **Alt+Tab** | 切换窗口 | 切换窗口 |
| **Alt+F4** | 关闭窗口 | 关闭窗口 |
| **Ctrl+Esc** | 打开开始菜单 | - |
| **Ctrl+Alt+Del** | 安全桌面（**不可拦截**） | - |

Qt 在操作系统**之后**接收按键事件，因此这些按键 Qt 根本收不到。本库在 **OS 之前**拦截它们。

### 1.2 设计原则

- **全局开关**：一个 `start()` / `stop()` 控制所有按键的捕获，不做细粒度按键配置
- **焦点驱动**：VideoPane 获得焦点 → 自动开启全键盘捕获；失去焦点 → 自动释放
- **简单可靠**：API 极简，只有 `start()` / `stop()` / `isActive()`
- **跨平台**：Windows / Linux X11（Wayland 第一版不支持）

### 1.3 不可拦截的键

| 键 | 原因 | 处理方式 |
|---|------|---------|
| **Ctrl+Alt+Del** | Windows 内核级 Secure Attention Sequence (SAS) | 用作"逃生键"——检测到后自动释放捕获，交还系统 |
| **Fn 组合键** | 硬件级处理，OS 根本看不到 | 无法处理 |
| **Wayland 系统键** | Wayland compositor 安全模型限制 | 第一版标注不支持 |

---

## 2. API 设计

### 2.1 核心类（极简）

```cpp
class SystemKeyBlocker : public QObject {
    Q_OBJECT
public:
    /// 全局单例
    static SystemKeyBlocker& instance();

    /// 开始捕获所有键盘（OS 不再处理任何按键，全部转发）
    bool start(quintptr nativeParentHwnd = 0);

    /// 停止捕获，释放钩子，系统恢复正常
    void stop();

    /// 当前是否处于捕获状态
    bool isActive() const;

signals:
    /**
     * 捕获到任意键盘事件时发出
     * @param qtKeyCode  Qt 键码（如 Qt::Key_Meta, Qt::Key_Print）
     * @param modifiers  Qt 修饰键（如 Qt::ControlModifier | Qt::MetaModifier）
     * @param isKeyDown  true=按下, false=释放
     * @param nativeVk   平台原生虚拟键码（Windows: VK_*, Linux: keysym）
     *
     * 连接方应将此信号转发给 KeyboardManager::handleKeyboardAction()
     */
    void keyCaptured(int qtKeyCode, int modifiers, bool isKeyDown, quint32 nativeVk);

    /// 捕获状态改变
    void captureStateChanged(bool active);

private:
    bool startImpl(quintptr nativeParentHwnd);
    void stopImpl();
    int  nativeToQtKey(quint32 nativeVk, bool extended) const;
};
```

**注意**：没有任何 `setBlockWinKey()` / `setBlockPrintScreen()` 等细粒度方法。开关就是一个：`start()` 捕获所有，`stop()` 释放所有。

### 2.2 生命周期

```
start()
  ├─ 已在运行 → 先 stop() 再重启
  ├─ 调用平台特定 startImpl()
  │    ├─ Windows: SetWindowsHookEx(WH_KEYBOARD_LL, ...)
  │    └─ Linux X11: QAbstractNativeEventFilter
  ├─ 成功 → m_active = true, emit captureStateChanged(true)
  └─ 失败 → 日志输出, emit captureStateChanged(false)

stop()
  ├─ 未在运行 → 无操作
  ├─ 调用平台特定 stopImpl()
  │    ├─ Windows: UnhookWindowsHookEx()
  │    └─ Linux X11: 移除 nativeEventFilter
  └─ m_active = false, emit captureStateChanged(false)
```

### 2.3 逃生键 Ctrl+Alt+Del

Windows 内核级保护，任何用户态程序都无法拦截。处理方式：

- 钩子回调中检测到 Ctrl+Alt+Del 三键同时按下 → 自动调用 `stop()` 释放捕获
- 用户点击 VideoPane 重新获得焦点 → 自动调用 `start()` 恢复捕获
- UI 提示："Ctrl+Alt+Del 无法转发，请使用目标电脑的该组合键"

---

## 3. 平台实现

### 3.1 Windows（`SystemKeyBlocker_win.cpp`）

**技术**：`SetWindowsHookEx(WH_KEYBOARD_LL, ...)` 全局低层键盘钩子

**核心流程**：
```
钩子安装 → 所有按键进入回调 → 转发信号 → return 1 吞掉事件 → OS 不再处理
                                                    ↓ (除 Ctrl+Alt+Del)
                                         检测到 → stop() 释放钩子 → 放行给系统
```

```cpp
#ifdef Q_OS_WIN
#include <windows.h>
#include "SystemKeyBlocker.h"

SystemKeyBlocker* SystemKeyBlocker::s_self = nullptr;

bool SystemKeyBlocker::startImpl(quintptr nativeParentHwnd) {
    s_self = this;
    m_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardProc,
                                GetModuleHandle(nullptr), 0);
    if (!m_hHook) {
        qWarning() << "SystemKeyBlocker: SetWindowsHookEx failed:"
                   << GetLastError();
        s_self = nullptr;
        return false;
    }
    qInfo() << "SystemKeyBlocker: 全键盘捕获已启动";
    return true;
}

void SystemKeyBlocker::stopImpl() {
    if (m_hHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_hHook));
        m_hHook = nullptr;
        qInfo() << "SystemKeyBlocker: 全键盘捕获已停止";
    }
    s_self = nullptr;
}

// 钩子回调 — 运行在系统线程，必须快速返回（< 10ms）
LRESULT CALLBACK SystemKeyBlocker::lowLevelKeyboardProc(
    int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_self && s_self->m_active) {
        KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        quint32 vk = kb->vkCode;
        bool isExtended = (kb->flags & LLKHF_EXTENDED) != 0;

        // ── 逃生键检测：Ctrl+Alt+Del ──
        // Ctrl+Alt+Del 在钩子里收不到（内核级拦截），但作为安全策略保留
        // 钩子回调中的实际处理由系统自动完成

        // ── 捕获所有按键：转发信号并吞掉事件 ──
        int qtKey = s_self->nativeToQtKey(vk, isExtended);

        // 收集当前修饰键状态
        int modifiers = 0;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)    modifiers |= Qt::ShiftModifier;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000)  modifiers |= Qt::ControlModifier;
        if (GetAsyncKeyState(VK_MENU) & 0x8000)     modifiers |= Qt::AltModifier;
        if ((GetAsyncKeyState(VK_LWIN) & 0x8000) ||
            (GetAsyncKeyState(VK_RWIN) & 0x8000))   modifiers |= Qt::MetaModifier;

        // 发出信号（Qt 自动跨线程投递到主事件循环）
        emit s_self->keyCaptured(qtKey, modifiers, isKeyDown, vk);

        // ── 吞掉所有按键事件，OS 不再处理 ──
        // 仅 key release 放行，防止按键卡住（与 VirtualBox 策略一致）
        if (isKeyDown) {
            return 1;  // 吞掉按键按下事件
        }
        // 释放事件放行，避免按键卡在 OS 层面
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif
```

**要点**：
- 不需要管理员权限，普通用户即可安装 `WH_KEYBOARD_LL` 钩子
- `GetModuleHandle(NULL)` 获取当前进程模块句柄，不需要 DLL
- 钩子回调运行在**系统线程**，必须快速返回（< 10ms），所有处理通过 `emit signal` 异步投递
- **释放事件放行**：与 VirtualBox 一致，让 OS 看到 key release 防止按键卡住

### 3.2 Linux X11（`SystemKeyBlocker_x11.cpp`）

**技术**：`QAbstractNativeEventFilter` 拦截 XCB 键盘事件

> 在 X11 下，`QAbstractNativeEventFilter` 可以拦截所有到达 Qt 事件循环的 XCB 事件。对于全局快捷键冲突（如 DE 已注册 Super 键），需要额外 `xcb_grab_key`。

```cpp
#ifdef Q_OS_LINUX
#include "SystemKeyBlocker.h"
#include <QAbstractNativeEventFilter>
#include <xcb/xcb.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

class X11KeyCaptureFilter : public QAbstractNativeEventFilter {
public:
    SystemKeyBlocker *blocker;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           long *result) override
    {
        if (eventType != "xcb_generic_event_t" || !blocker->isActive())
            return false;

        auto *event = static_cast<xcb_generic_event_t*>(message);
        uint8_t type = event->response_type & ~0x80;

        if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
            auto *ke = static_cast<xcb_key_press_event_t*>(message);
            bool isDown = (type == XCB_KEY_PRESS);

            // 获取 keysym
            Display *dpy = XOpenDisplay(nullptr);  // 应缓存
            KeySym ks = XkbKeycodeToKeysym(dpy, ke->detail, 0, 0);

            // 收集修饰键
            int modifiers = 0;
            if (ke->state & ShiftMask)   modifiers |= Qt::ShiftModifier;
            if (ke->state & ControlMask) modifiers |= Qt::ControlModifier;
            if (ke->state & Mod1Mask)    modifiers |= Qt::AltModifier;
            if (ke->state & Mod4Mask)    modifiers |= Qt::MetaModifier;

            int qtKey = blocker->nativeToQtKey(ks, false);

            emit blocker->keyCaptured(qtKey, modifiers, isDown, ks);

            // 吞掉所有按下事件，释放事件放行（同 Windows 策略）
            if (isDown)
                return true;
        }
        return false;
    }
};
#endif
```

---

## 4. 文件结构

```
target/
├── SystemKeyBlocker.h        # 公共接口头
├── SystemKeyBlocker.cpp      # 公共逻辑（单例、start/stop 管理、nativeToQtKey）
├── SystemKeyBlocker_win.cpp  # Windows 实现（WH_KEYBOARD_LL 钩子）
└── SystemKeyBlocker_x11.cpp  # Linux X11 实现（QAbstractNativeEventFilter）
```

---

## 5. 与现有系统的集成

### 5.1 InputHandler 集成（焦点联动）

```cpp
// InputHandler::eventFilter 中

case QEvent::FocusIn:
    // VideoPane 获得焦点 → 开启全键盘捕获
    SystemKeyBlocker::instance().start(m_videoPane->winId());
    break;

case QEvent::FocusOut:
    // VideoPane 失去焦点 → 释放所有捕获
    SystemKeyBlocker::instance().stop();
    break;
```

### 5.2 KeyboardManager 信号连接

```cpp
// main.cpp 或 MainWindow 初始化时

connect(&SystemKeyBlocker::instance(), &SystemKeyBlocker::keyCaptured,
    [](int qtKey, int modifiers, bool isKeyDown, quint32 nativeVk) {
        // 将捕获到的所有按键转发给 KeyboardManager
        KeyboardManager::instance().handleKeyboardAction(
            qtKey, modifiers, isKeyDown, nativeVk);
    });
```

### 5.3 UI 设置

```
[✓] 键盘捕获模式（开启后所有按键将转发到目标电脑，系统不再处理）
    说明：点击视频画面启用，点击其他区域释放。
    注意：Ctrl+Alt+Del 无法捕获（系统内核级保护）。
```

只需一个开关，不需要按键级别的配置。

---

## 6. 代码量预估

| 文件 | 预估行数 |
|------|---------|
| `SystemKeyBlocker.h` | ~60 行 |
| `SystemKeyBlocker.cpp` | ~120 行 |
| `SystemKeyBlocker_win.cpp` | ~150 行 |
| `SystemKeyBlocker_x11.cpp` | ~130 行 |
| `InputHandler.cpp` 改动 | ~15 行 |
| UI 改动 | ~30 行 |
| **总计** | **~505 行** |

**依赖**：
- Windows: `<windows.h>`（系统自带，无需管理员权限）
- Linux: `<X11/XKBlib.h>` + `<xcb/xcb.h>`（大部分发行版已装）
- Qt: `QObject` + `QAbstractNativeEventFilter`（项目已有）
- **零第三方依赖**

---

## 7. 已知限制

| 键/场景 | 状态 | 原因 |
|---------|------|------|
| `Ctrl+Alt+Del` | ❌ 无法捕获 | Windows 内核级 SAS，用户态程序无权限 |
| `Fn` 组合键 | ❌ 无法捕获 | 硬件级处理，OS 看不到 |
| Wayland 系统键 | ⚠️ 第一版不支持 | 需要 compositor 协议配合 |
| 全屏游戏独占 | ⚠️ 可能部分失效 | 游戏直接读设备 |

---

## 8. 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| 钩子回调超时（>10ms）导致系统卡顿 | 所有处理通过 `emit signal` 异步投递，回调只做判断+emit |
| 杀毒软件拦截钩子 | 文档说明；必要时提示白名单 |
| 焦点丢失后钩子未释放 | `QEvent::FocusOut` 严格调用 `stop()` |
| Linux X11 grab 冲突 | 日志提示；建议用户关闭系统全局快捷键 |
| 用户忘记如何退出捕获 | UI 显示当前状态；Ctrl+Alt+Del 自动释放 |

---

## 9. 实施计划

| 阶段 | 任务 | 预估 | 涉及文件 |
|------|------|------|---------|
| Phase 1 | 公共接口 + 单例 + start/stop | 半天 | `SystemKeyBlocker.h`, `.cpp` |
| Phase 2 | Windows `WH_KEYBOARD_LL` 实现 | 1 天 | `SystemKeyBlocker_win.cpp` |
| Phase 3 | Linux X11 `nativeEventFilter` 实现 | 1 天 | `SystemKeyBlocker_x11.cpp` |
| Phase 4 | 集成到 `InputHandler`（焦点联动） | 半天 | `InputHandler.cpp` |
| Phase 5 | UI 设置（一个开关） | 半天 | `targetcontrolpage.cpp` |
| Phase 6 | 跨平台测试 | 半天 | 手动测试 |

**总计：约 4 天**

### 测试矩阵

| 平台 | 环境 | 测试内容 |
|------|------|---------|
| Windows 10 | 普通用户 | 所有按键捕获 + 焦点切换 |
| Windows 11 | 普通用户 | 同上 |
| Ubuntu 22.04 | X11 + GNOME | Super 键、PrintScreen 捕获 |
| Ubuntu 22.04 | X11 + KDE | 同上 |
| Ubuntu 22.04 | Wayland | 验证不支持时优雅降级 |
| Debian 12 | X11 | 基本功能 |

---

## 附录 A：nativeToQtKey 映射表（核心部分）

| Windows VK | Linux KeySym | Qt Key | HID Scancode |
|-----------|-------------|--------|-------------|
| `VK_LWIN` (0x5B) | `XK_Super_L` | `Qt::Key_Meta` | 0xE3 |
| `VK_RWIN` (0x5C) | `XK_Super_R` | `Qt::Key_Meta` | 0xE7 |
| `VK_SNAPSHOT` (0x2C) | `XK_Print` | `Qt::Key_Print` | 0x46 |
| `VK_TAB` (0x09) | `XK_Tab` | `Qt::Key_Tab` | 0x2B |
| `VK_ESCAPE` (0x1B) | `XK_Escape` | `Qt::Key_Escape` | 0x29 |
| `VK_APPS` (0x5D) | `XK_Menu` | `Qt::Key_Menu` | 0x65 |

## 附录 B：与 VirtualBox 策略对比

| 方面 | VirtualBox | 我们的方案 |
|------|-----------|-----------|
| Windows 钩子 | `WH_KEYBOARD_LL` | 相同 |
| Linux 方案 | `xcb_grab_keyboard` | `QAbstractNativeEventFilter` |
| 捕获粒度 | 捕获所有（有 Host Key 概念） | 捕获所有（更简单） |
| Ctrl+Alt+Del | 用作释放键盘的逃生键 | 相同 |
| AltGr 处理 | 专门的 `WinAltGrMonitor` 检测假 LCtrl | 第一版不做（后续可扩展） |
| key release 处理 | 放行给 OS 防止按键卡住 | 相同 |
| Host Key 组合 | 可自定义（如右 Ctrl）作为逃生键 | 不需要（更简单） |
