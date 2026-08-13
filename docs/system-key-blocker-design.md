# SystemKeyBlocker Library — Design Document

> **Goal**: A manually toggleable, cross-platform (Windows/Linux) keyboard capture library. When enabled, it intercepts **all** system shortcuts (Win, PrintScreen, Alt+Tab, etc.) so that Openterface can obtain the full keyboard HID codes and forward them directly to the target computer.
>
> **Reference implementation**: VirtualBox [UIKeyboardHandler.cpp](https://github.com/vbox/vbox/blob/main/src/VBox/Frontends/VirtualBox/src/runtime/UIKeyboardHandler.cpp)

---

## 1. Overview

### 1.1 Problem Statement

As KVM software, Openterface needs to capture **all** keystrokes from the physical keyboard (including system-level shortcuts) and forward them to the target computer. However, the operating system intercepts the following keys before any application can see them:

| Key | Windows Behavior | Linux Behavior |
|-----|-----------------|----------------|
| **Win / Super key** | Opens Start Menu | Opens Activities/Launcher |
| **Win+D** | Shows desktop | - |
| **Win+E** | Opens File Explorer | - |
| **Win+L** | Lock screen | Lock screen |
| **PrintScreen** | Screenshot tool | Screenshot tool |
| **Alt+Tab** | Switch window | Switch window |
| **Alt+F4** | Close window | Close window |
| **Ctrl+Esc** | Opens Start Menu | - |
| **Ctrl+Alt+Del** | Secure Attention Screen (**cannot be intercepted**) | - |

Qt receives key events **after** the operating system, so these keys never reach the application. This library intercepts them **before** the OS does.

### 1.2 Design Principles

- **Global toggle**: A single `start()` / `stop()` controls all key capture — no per-key configuration
- **Focus-driven**: VideoPane gains focus → full keyboard capture activates automatically; loses focus → capture released
- **Simple and reliable**: Minimal API — only `start()` / `stop()` / `isActive()`
- **Cross-platform**: Windows / Linux X11 (Wayland not supported in v1)

### 1.3 Uninterceptable Keys

| Key | Reason | Handling |
|-----|--------|----------|
| **Ctrl+Alt+Del** | Windows kernel-level Secure Attention Sequence (SAS) | Used as an "escape key" — detection triggers automatic release of capture, returning control to the system |
| **Fn key combinations** | Handled at hardware level; the OS never sees them | Cannot be handled |
| **Wayland system keys** | Wayland compositor security model restrictions | Marked as unsupported in v1 |

---

## 2. API Design

### 2.1 Core Class (Minimal)

```cpp
class SystemKeyBlocker : public QObject {
    Q_OBJECT
public:
    /// Global singleton
    static SystemKeyBlocker& instance();

    /// Start capturing all keys (OS no longer processes any keys; all forwarded)
    bool start(quintptr nativeParentHwnd = 0);

    /// Stop capture, release hooks, system returns to normal
    void stop();

    /// Whether capture is currently active
    bool isActive() const;

signals:
    /**
     * Emitted when any keyboard event is captured.
     * @param qtKeyCode  Qt key code (e.g. Qt::Key_Meta, Qt::Key_Print)
     * @param modifiers  Qt modifiers (e.g. Qt::ControlModifier | Qt::MetaModifier)
     * @param isKeyDown  true=press, false=release
     * @param nativeVk   Platform-native virtual key code (Windows: VK_*, Linux: keysym)
     *
     * The connected slot should forward this to KeyboardManager::handleKeyboardAction()
     */
    void keyCaptured(int qtKeyCode, int modifiers, bool isKeyDown, quint32 nativeVk);

    /// Emitted when capture state changes
    void captureStateChanged(bool active);

private:
    bool startImpl(quintptr nativeParentHwnd);
    void stopImpl();
    int  nativeToQtKey(quint32 nativeVk, bool extended) const;
};
```

**Note**: There are no per-key methods like `setBlockWinKey()` / `setBlockPrintScreen()`. There is a single toggle: `start()` captures everything, `stop()` releases everything.

### 2.2 Lifecycle

```
start()
  ├─ Already running → stop() first, then restart
  ├─ Call platform-specific startImpl()
  │    ├─ Windows: SetWindowsHookEx(WH_KEYBOARD_LL, ...)
  │    └─ Linux X11: XGrabKeyboard + QAbstractNativeEventFilter
  ├─ Success → m_active = true, emit captureStateChanged(true)
  └─ Failure → log output, emit captureStateChanged(false)

stop()
  ├─ Not running → no-op
  ├─ Call platform-specific stopImpl()
  │    ├─ Windows: UnhookWindowsHookEx()
  │    └─ Linux X11: remove nativeEventFilter, release grab
  └─ m_active = false, emit captureStateChanged(false)
```

### 2.3 Escape Key: Ctrl+Alt+Del

Protected at the Windows kernel level — no user-mode program can intercept it. Handling:

- Hook callback detects Ctrl+Alt+Del pressed simultaneously → automatically calls `stop()` to release capture
- User clicks VideoPane to regain focus → automatically calls `start()` to restore capture
- UI prompt: "Ctrl+Alt+Del cannot be forwarded; please use this combination on the target computer"

---

## 3. Platform Implementations

### 3.1 Windows (`SystemKeyBlocker_win.cpp`)

**Technology**: `SetWindowsHookEx(WH_KEYBOARD_LL, ...)` — global low-level keyboard hook

**Core flow**:
```
Hook installed → all keys enter callback → forward signal → return 1 to swallow event → OS no longer processes
                                                    ↓ (except Ctrl+Alt+Del)
                                         detected → stop() releases hook → pass through to system
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
    qInfo() << "SystemKeyBlocker: Full keyboard capture started";
    return true;
}

void SystemKeyBlocker::stopImpl() {
    if (m_hHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_hHook));
        m_hHook = nullptr;
        qInfo() << "SystemKeyBlocker: Full keyboard capture stopped";
    }
    s_self = nullptr;
}

// Hook callback — runs on system thread, must return quickly (< 10ms)
LRESULT CALLBACK SystemKeyBlocker::lowLevelKeyboardProc(
    int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_self && s_self->m_active) {
        KBDLLHOOKSTRUCT *kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        quint32 vk = kb->vkCode;
        bool isExtended = (kb->flags & LLKHF_EXTENDED) != 0;

        // ── Escape key detection: Ctrl+Alt+Del ──
        // Ctrl+Alt+Del is not received in the hook (kernel-level interception),
        // but retained as a safety policy.
        // Actual handling in the hook callback is done by the system automatically.

        // ── Capture all keys: forward signal and swallow event ──
        int qtKey = s_self->nativeToQtKey(vk, isExtended);

        // Collect current modifier key state
        int modifiers = 0;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)    modifiers |= Qt::ShiftModifier;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000)  modifiers |= Qt::ControlModifier;
        if (GetAsyncKeyState(VK_MENU) & 0x8000)     modifiers |= Qt::AltModifier;
        if ((GetAsyncKeyState(VK_LWIN) & 0x8000) ||
            (GetAsyncKeyState(VK_RWIN) & 0x8000))   modifiers |= Qt::MetaModifier;

        // Emit signal (Qt automatically posts across threads to the main event loop)
        emit s_self->keyCaptured(qtKey, modifiers, isKeyDown, vk);

        // ── Swallow all key-down events; OS no longer processes them ──
        // Only key releases are passed through to prevent stuck keys (same as VirtualBox strategy)
        if (isKeyDown) {
            return 1;  // Swallow key-down event
        }
        // Release events pass through to prevent stuck keys at the OS level
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif
```

**Key points**:
- No admin privileges required — `WH_KEYBOARD_LL` hooks can be installed by any user
- `GetModuleHandle(NULL)` obtains the current process module handle; no DLL needed
- The hook callback runs on a **system thread** and must return quickly (< 10ms). All processing is dispatched asynchronously via `emit signal`
- **Release events pass through**: Consistent with VirtualBox — lets the OS see key-up events to prevent stuck keys

### 3.2 Linux X11 (`SystemKeyBlocker_x11.cpp`)

**Technology**: `XGrabKeyboard()` on a separate X Display connection to capture the full keyboard + `QAbstractNativeEventFilter` to track focus changes

> `QAbstractNativeEventFilter` can only see events on Qt's connection, but `XGrabKeyboard` delivers events to the **connection that initiated the grab**. Therefore, a second Display connection must be opened exclusively for grabbing, with a timer polling for key events on that connection and forwarding them. Meanwhile, the nativeEventFilter on Qt's connection monitors `FocusIn`/`FocusOut` to re-grab when the window regains focus.

#### Architecture

```
           ┌────────────────────────────────┐
           │          X Server              │
           └───┬──────────────────┬─────────┘
               │                  │
      Qt Display connection   Grab Display connection (m_grabDpy)
      (event loop driven)     (timer polling, 5ms)
               │                  │
      nativeEventFilter      XGrabKeyboard(owner_events=False)
      monitors FocusIn/       all key events received here
      FocusOut                forwarded via processEvents()
      controls grab/ungrab
```

#### Core Flow

```
startImpl()
  ├─ Open second Display connection (m_grabDpy)
  ├─ Install nativeEventFilter (monitors focus events on Qt's connection)
  └─ Call grabKeyboard()
       └─ XGrabKeyboard(m_grabDpy, m_tlw, False, Async, Async)
            └─ On success, start 5ms timer to poll key events on m_grabDpy

nativeEventFilter (Qt connection)
  ├─ XCB_FOCUS_OUT + NotifyNormal → ungrabKeyboard()
  └─ XCB_FOCUS_IN  + NotifyNormal → grabKeyboard() (only when isActive())

processEvents (timer, 5ms interval)
  └─ XPending(m_grabDpy) loop
       ├─ KeyPress/KeyRelease → convert keysym → emit keyCaptured
       └─ FocusOut(NotifyWhileGrabbed) → set m_grabbed=false, retry grab after 100ms
```

#### ⚠️ Known Pitfall: Focus Event Loop (Fixed)

`XGrabKeyboard` / `XUngrabKeyboard` **automatically generate** `FocusOut` and `FocusIn` events.
If `nativeEventFilter` responds to every `FocusIn`/`FocusOut`, it creates an infinite loop:

```
FocusIn → grabKeyboard()
  → X Server sends FocusOut (mode=NotifyGrab)
    → FocusOut → ungrabKeyboard()
      → X Server sends FocusIn (mode=NotifyUngrab)
        → FocusIn → grabKeyboard()
          → ... infinite loop
```

This loop runs hundreds of times per second, causing 100% CPU usage and a completely frozen application (logs reached 198,000 lines in 18 seconds during testing).

**Fix**: Only respond to focus events with `mode == XCB_NOTIFY_MODE_NORMAL` —
these are genuine user focus changes (clicking another window, WM moving focus, etc.).
Ignore events with `NotifyGrab` / `NotifyUngrab` / `NotifyWhileGrabbed` modes,
because they are generated by our own grab/ungrab operations.
Additionally, use a `m_inFocusTransition` reentrancy guard flag to suppress all focus events during grab/ungrab execution.

```cpp
if (responseType == XCB_FOCUS_OUT) {
    xcb_focus_out_event_t *focusOut = reinterpret_cast<xcb_focus_out_event_t *>(event);
    if (focusOut->event == m_tlw && m_grabbed && !m_inFocusTransition
        && focusOut->mode == XCB_NOTIFY_MODE_NORMAL) {
        m_inFocusTransition = true;
        ungrabKeyboard();
        m_inFocusTransition = false;
        g_modifierState = ModifierKeyState{};
    }
} else if (responseType == XCB_FOCUS_IN) {
    xcb_focus_in_event_t *focusIn = reinterpret_cast<xcb_focus_in_event_t *>(event);
    if (focusIn->event == m_tlw && !m_grabbed && !m_inFocusTransition
        && m_blocker && m_blocker->isActive()
        && focusIn->mode == XCB_NOTIFY_MODE_NORMAL) {
        m_inFocusTransition = true;
        grabKeyboard();
        m_inFocusTransition = false;
    }
}
```

This is a **classic pitfall** of X11 keyboard grabbing. Projects like VirtualBox, dwm, i3, and xmonad have all encountered similar issues.
References: Xlib Programming Manual (Nye/Adrian), libx11 source code (`_XEnq` handling of FocusIn/FocusOut).

---

## 4. File Structure

```
SysKeyBlocker/
├── SystemKeyBlocker.h        # Public interface header
├── SystemKeyBlocker.cpp      # Common logic (singleton, start/stop management, nativeToQtKey)
├── SystemKeyBlocker_win.cpp  # Windows implementation (WH_KEYBOARD_LL hook)
└── SystemKeyBlocker_x11.cpp  # Linux X11 implementation (XGrabKeyboard + nativeEventFilter)
```

---

## 5. Integration with Existing Systems

### 5.1 InputHandler Integration (Focus Coupling)

```cpp
// In InputHandler::eventFilter

case QEvent::FocusIn:
    // VideoPane gained focus → start full keyboard capture
    SystemKeyBlocker::instance().start(m_videoPane->winId());
    break;

case QEvent::FocusOut:
    // VideoPane lost focus → release all capture
    SystemKeyBlocker::instance().stop();
    break;
```

### 5.2 KeyboardManager Signal Connection

```cpp
// In main.cpp or MainWindow initialization

connect(&SystemKeyBlocker::instance(), &SystemKeyBlocker::keyCaptured,
    [](int qtKey, int modifiers, bool isKeyDown, quint32 nativeVk) {
        // Forward all captured keys to KeyboardManager
        KeyboardManager::instance().handleKeyboardAction(
            qtKey, modifiers, isKeyDown, nativeVk);
    });
```

### 5.3 UI Settings

```
[✓] Keyboard capture mode (when enabled, all keys are forwarded to the target;
     the host system does not process them)
    Note: Click the video pane to activate; click elsewhere to release.
    Warning: Ctrl+Alt+Del cannot be captured (kernel-level system protection).
```

A single toggle — no per-key configuration needed.

---

## 6. Estimated Code Size

| File | Estimated Lines |
|------|----------------|
| `SystemKeyBlocker.h` | ~60 lines |
| `SystemKeyBlocker.cpp` | ~120 lines |
| `SystemKeyBlocker_win.cpp` | ~150 lines |
| `SystemKeyBlocker_x11.cpp` | ~130 lines |
| `InputHandler.cpp` changes | ~15 lines |
| UI changes | ~30 lines |
| **Total** | **~505 lines** |

**Dependencies**:
- Windows: `<windows.h>` (system-provided; no admin privileges required)
- Linux: `<X11/XKBlib.h>` + `<xcb/xcb.h>` (installed on most distributions)
- Qt: `QObject` + `QAbstractNativeEventFilter` (already in the project)
- **Zero third-party dependencies**

---

## 7. Known Limitations & Pitfalls

| Key/Scenario | Status | Reason |
|--------------|--------|--------|
| `Ctrl+Alt+Del` | ❌ Cannot capture | Windows kernel-level SAS; user-mode programs have no access |
| `Fn` key combinations | ❌ Cannot capture | Handled at hardware level; OS never sees them |
| Wayland system keys | ⚠️ Not supported in v1 | Requires compositor protocol support |
| Fullscreen game exclusive mode | ⚠️ May partially fail | Games read directly from the device |
| **X11 Focus event loop** | ✅ Fixed | `XGrabKeyboard` generates `FocusOut`/`FocusIn` events. If not filtered by `mode`, this causes an infinite grab/ungrab loop (application freeze). Fix: only respond to `mode == XCB_NOTIFY_MODE_NORMAL` focus events, with `m_inFocusTransition` reentrancy guard. See §3.2. |

---

## 8. Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Hook callback timeout (>10ms) causing system lag | All processing dispatched asynchronously via `emit signal`; callback only does check + emit |
| Antivirus software blocking the hook | Documentation; prompt user to whitelist if necessary |
| Hook not released after focus loss | `QEvent::FocusOut` strictly calls `stop()` |
| Linux X11 grab conflicts | Log warnings; advise user to disable system global shortcuts |
| **X11 Focus event loop** | ✅ Must filter `FocusIn`/`FocusOut` by `mode` (only respond to `NotifyNormal`), with reentrancy guard. See §3.2. |
| User forgets how to exit capture | UI shows current state; Ctrl+Alt+Del auto-releases |

---

## 9. Implementation Plan

| Phase | Task | Estimate | Files |
|-------|------|----------|-------|
| Phase 1 | Public interface + singleton + start/stop | Half day | `SystemKeyBlocker.h`, `.cpp` |
| Phase 2 | Windows `WH_KEYBOARD_LL` implementation | 1 day | `SystemKeyBlocker_win.cpp` |
| Phase 3 | Linux X11 `nativeEventFilter` implementation | 1 day | `SystemKeyBlocker_x11.cpp` |
| Phase 4 | Integration into `InputHandler` (focus coupling) | Half day | `InputHandler.cpp` |
| Phase 5 | UI settings (single toggle) | Half day | `targetcontrolpage.cpp` |
| Phase 6 | Cross-platform testing | Half day | Manual testing |

**Total: ~4 days**

### Test Matrix

| Platform | Environment | Test Content |
|----------|------------|--------------|
| Windows 10 | Regular user | All key capture + focus switching |
| Windows 11 | Regular user | Same as above |
| Ubuntu 22.04 | X11 + GNOME | Super key, PrintScreen capture |
| Ubuntu 22.04 | X11 + KDE | Same as above |
| Ubuntu 22.04 | Wayland | Verify graceful degradation when unsupported |
| Debian 12 | X11 | Basic functionality |

---

## Appendix A: nativeToQtKey Mapping Table (Core Entries)

| Windows VK | Linux KeySym | Qt Key | HID Scancode |
|-----------|-------------|--------|-------------|
| `VK_LWIN` (0x5B) | `XK_Super_L` | `Qt::Key_Meta` | 0xE3 |
| `VK_RWIN` (0x5C) | `XK_Super_R` | `Qt::Key_Meta` | 0xE7 |
| `VK_SNAPSHOT` (0x2C) | `XK_Print` | `Qt::Key_Print` | 0x46 |
| `VK_TAB` (0x09) | `XK_Tab` | `Qt::Key_Tab` | 0x2B |
| `VK_ESCAPE` (0x1B) | `XK_Escape` | `Qt::Key_Escape` | 0x29 |
| `VK_APPS` (0x5D) | `XK_Menu` | `Qt::Key_Menu` | 0x65 |

## Appendix B: Comparison with VirtualBox Strategy

| Aspect | VirtualBox | Our Approach |
|--------|-----------|-------------|
| Windows hook | `WH_KEYBOARD_LL` | Same |
| Linux approach | `xcb_grab_keyboard` | `XGrabKeyboard` + `QAbstractNativeEventFilter` |
| Capture granularity | Capture all (has Host Key concept) | Capture all (simpler) |
| Ctrl+Alt+Del | Used as escape key to release keyboard | Same |
| AltGr handling | Dedicated `WinAltGrMonitor` detects fake LCtrl | Not done in v1 (can be extended later) |
| Key release handling | Pass through to OS to prevent stuck keys | Same |
| Host Key combo | Customizable (e.g. Right Ctrl) as escape key | Not needed (simpler) |
