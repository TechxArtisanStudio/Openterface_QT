# Keyboard Shortcut Forwarding When SystemKeyBlocker Is Off

**Date:** 2026-08-04
**Status:** Design — approved for implementation

---

## Problem Statement

The Openterface Mini KVM app has a `SystemKeyBlocker` toggle that, when **ON**, installs an OS-level `WH_KEYBOARD_LL` hook (Windows) to intercept **all** keystrokes and forward them to the target machine. When **OFF**, keys flow through Qt's normal event pipeline.

When SystemKeyBlocker is **OFF**, the user cannot correctly use `Ctrl+C` / `Ctrl+V` (and many other combination keys) to send those keystrokes to the target machine. The root cause:

- `MainWindow` defines many `QShortcut` and `QAction` objects with shortcuts whose default context is `Qt::WindowShortcut`.
- `Qt::WindowShortcut` fires whenever the top-level window has focus — including when `VideoPane` (a child widget) has focus.
- Qt's shortcut dispatch intercepts the key event **before** it reaches `VideoPane`'s `InputHandler`, so the key event is consumed by the app's own action and never forwarded to the target.

Example: pressing `Ctrl+V` with SystemBlocker OFF triggers `MainWindow::onActionPasteToTarget()` (reads host clipboard and pastes text to target) instead of sending the `Ctrl+V` keystroke to the target machine.

### Affected shortcuts (non-exhaustive)

**mainwindow.ui QActions:**
| Shortcut | Action |
|---|---|
| Ctrl+V | Paste clipboard to target |
| Ctrl+Q | Close app |
| Ctrl+P | Preferences |
| Ctrl+U | Update dialog |
| Ctrl+E | Environment check |
| Ctrl+T | Relative mouse mode |
| Ctrl+Shift+L | Script tool |
| Ctrl+Shift+R | Recording settings |
| Ctrl+Shift+D | Hardware diagnostics |
| Ctrl+Shift+C | TCP server |
| Ctrl+Shift+E/F/I/U/W | Various Advanced features |
| Ctrl+Shift+H/T | Switch USB to host/target |
| Ctrl+Alt+A/H/S | Mouse display modes |
| F1 | About dialog |

**mainwindowinitializer.cpp QShortcuts:**
| Shortcut | Action |
|---|---|
| Alt+F11 | Toggle fullscreen |
| Ctrl+Shift+A | Screen aspect ratio |
| Ctrl+Plus/Minus/0 | Zoom in/out/reset |
| Ctrl+Shift+S | Screenshot |
| Ctrl+Shift+V | Paste (duplicate) |
| Ctrl+Shift+F9/F10/F11 | Mute / Mouse dance / Recording |
| Ctrl+Shift+K | Virtual keyboard |

`Ctrl+C` is not defined as a shortcut in the app, so it should theoretically reach `VideoPane`. In practice it may fail when focus is on a widget that handles `Ctrl+C` internally (e.g. `QLineEdit`), or when focus is not on `VideoPane`.

---

## Desired Behavior

- **VideoPane (or its GStreamer overlay) has keyboard focus:** ALL keyboard events should be forwarded to the target machine via `InputHandler` → `HostManager` → `KeyboardManager`. Application shortcuts must **not** fire.
- **VideoPane does NOT have focus** (menus, dialogs, status bar, corner widgets): Application shortcuts work normally.
- **Shift+Arrow keys** in zoom mode: Continue to be used for local video panning (existing behavior preserved, not forwarded to target).
- **Escape key** handling: Existing "hold to release mouse" logic preserved.

---

## Approach: Focus-Based Shortcut Disabling

### Design

Listen to `QApplication::focusChanged`. When focus enters `VideoPane` (or its GStreamer overlay widget), disable all `QShortcut` and `QAction` objects with shortcuts that are children of `MainWindow`. When focus leaves, re-enable them.

### Implementation

**Files modified:**
1. `ui/mainwindow.h` — add a private slot and a `bool m_shortcutsDisabled = false;` member
2. `ui/mainwindow.cpp` — implement the slot, connect it in initialization

**Pseudocode:**

```cpp
// Member in MainWindow:
bool m_shortcutsDisabled = false;

// In MainWindow initialization (after VideoPane is constructed):
connect(qApp, &QApplication::focusChanged,
        this, &MainWindow::syncShortcutsState);
connect(&SystemKeyBlocker::instance(), &SystemKeyBlocker::captureStateChanged,
        this, &MainWindow::syncShortcutsState);

void MainWindow::syncShortcutsState()
{
    bool videoHasFocus =
        (focusWidget() == m_videoPane) ||
        (m_videoPane && focusWidget() == m_videoPane->getOverlayWidget());

    // Disable shortcuts only when VideoPane has focus AND SystemBlocker is not active.
    // When SystemBlocker is active, the OS hook swallows all events before Qt
    // sees them, so shortcuts never fire regardless — but we still track state
    // so that turning SystemBlocker off restores the correct behavior.
    bool shouldDisable = videoHasFocus && !SystemKeyBlocker::instance().isActive();

    if (shouldDisable == m_shortcutsDisabled) return; // no change

    for (QAction *action : findChildren<QAction*>()) {
        if (!action->shortcut().isEmpty()) {
            action->setEnabled(!shouldDisable);
        }
    }
    for (QShortcut *shortcut : findChildren<QShortcut*>()) {
        shortcut->setEnabled(!shouldDisable);
    }
    m_shortcutsDisabled = shouldDisable;
}
```

**Why also listen to `captureStateChanged`:** If VideoPane already has focus and the user turns SystemBlocker OFF, the `focusChanged` signal does not fire (focus hasn't moved). Without the `captureStateChanged` connection, shortcuts would remain enabled and would intercept keys — the very bug we're fixing. Listening to both signals covers all transitions.

### Event flow comparison

**Before fix (SystemBlocker OFF, Ctrl+V pressed, VideoPane has focus):**
```
OS → Qt → QApplication::notify()
        → shortcut map match: actionPaste (Ctrl+V)
        → MainWindow::onActionPasteToTarget()
        → event consumed; VideoPane never sees the key
```

**After fix (SystemBlocker OFF, Ctrl+V pressed, VideoPane has focus):**
```
OS → Qt → QApplication::notify()
        → shortcut map: actionPaste is disabled → no match
        → event delivered to focused widget (VideoPane)
        → InputHandler::eventFilter() captures KeyPress
        → HostManager::handleKeyPress()
        → KeyboardManager::handleKeyboardAction()
        → serial data sent to target machine
```

### What is NOT changed

- `SystemKeyBlocker` code — unchanged
- `InputHandler` code — unchanged (existing Shift+Arrow panning logic preserved)
- `KeyboardManager` code — unchanged
- `mainwindow.ui` — shortcut definitions unchanged
- `mainwindowinitializer.cpp` — programmatic shortcuts unchanged
- Mouse event handling — unchanged

### Why this approach over alternatives

| Approach | Why chosen / not chosen |
|---|---|
| **Focus-based disabling (chosen)** | Minimal code, uses Qt's built-in `setEnabled()`, `findChildren` auto-discovers all shortcuts (future additions covered), no `.ui` file changes |
| Change shortcut context to `Qt::WidgetShortcut` | Too restrictive — shortcuts wouldn't fire when focus is on any child widget (including status bar, corner widgets). Would require individually changing every shortcut. |
| Subclass `QApplication` and override `notify()` | Too invasive — requires changing `main()`, careful handling of all event types. |
| Application-level event filter | Won't work — `QApplication::notify()` processes shortcuts **before** calling `notify_helper()`, so event filters run too late. |

### Performance

`QApplication::focusChanged` fires only on actual focus transitions. Iterating ~30 `QShortcut`/`QAction` objects and setting `enabled` is sub-millisecond. Negligible.

### Edge cases

1. **GStreamer overlay widget:** When `VideoPane::isDirectGStreamerModeEnabled()` is true, a separate overlay widget receives events. Both `VideoPane` and its overlay must be checked for focus.
2. **Dialogs:** Modal dialogs are separate top-level windows (`QDialog`), not children of `MainWindow`. Their shortcuts are independent and unaffected.
3. **SystemBlocker state transitions:** The handler listens to both `focusChanged` and `captureStateChanged`. This covers: (a) VideoPane has focus → SystemBlocker turned off — shortcuts correctly disabled; (b) SystemBlocker is ON → turned off → VideoPane has focus — shortcuts disabled via the `captureStateChanged` callback; (c) VideoPane has focus → SystemBlocker turned on — shortcuts re-enabled (hook now swallows events, so it doesn't matter, but state stays consistent).

---

## Testing

- **Test 1:** Start app with SystemBlocker OFF. Click on VideoPane. Press `Ctrl+V`. Verify: `Ctrl+V` keystroke is sent to target machine (not clipboard paste).
- **Test 2:** Start app with SystemBlocker OFF. Click on VideoPane. Press `Ctrl+C`. Verify: `Ctrl+C` keystroke is sent to target.
- **Test 3:** Open Preferences dialog (`Ctrl+P` from main window). Press `Ctrl+P` again inside the dialog. Verify: shortcut still works (dialog is separate window).
- **Test 4:** Click on VideoPane. Press `Shift+Up` in zoom mode. Verify: video pans (not forwarded to target).
- **Test 5:** Click on VideoPane. Press `Ctrl+Shift+S`. Verify: `Ctrl+Shift+S` is forwarded to target (not intercepted as screenshot shortcut).
- **Test 6:** Click away from VideoPane (e.g., on status bar). Press `Ctrl+P`. Verify: Preferences dialog opens.
- **Test 7:** Toggle SystemBlocker ON. Verify: all keys go to target as before (regression check).
- **Test 8:** Click on VideoPane (focus on VideoPane). With SystemBlocker ON, turn SystemBlocker OFF. Press `Ctrl+V`. Verify: `Ctrl+V` is forwarded to target (not intercepted by app shortcut).
