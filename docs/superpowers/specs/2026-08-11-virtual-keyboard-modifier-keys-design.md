# Virtual Keyboard Modifier Keys & SystemKeyBlocker Focus Fix

**Date**: 2026-08-11  
**Status**: Approved  
**Approach**: Hybrid (Approach A)

## Problem

1. The virtual keyboard lost toggle-able modifier keys (Ctrl, Alt, Shift, Win) that existed in a previous version (commit 34d7ace). Users cannot easily send combinations like Ctrl+F1.
2. The virtual keyboard lacks preset combo buttons for common use cases like Ctrl+Alt+F1 (TTY switch).
3. SystemKeyBlocker does not auto-disable when the OpenterfaceQT window loses focus, causing issues where the host OS intercepts keys the user intends for the target.

## Solution Overview

Restore toggle-able modifier buttons in the toolbar, add preset combo buttons to the default config, and fix SystemKeyBlocker to auto-disable on focus loss.

## Section 1: Toggle-able Modifier Buttons

**Files**: `ui/toolbar/toolbarmanager.cpp`, `ui/toolbar/toolbarmanager.h`

### Changes

1. Add a list of modifier keys at the start of the toolbar (before F-keys):
   - Ctrl, Alt, Shift, Win
   - Each button is checkable (`setCheckable(true)`)
   - Each button has `MODIFIER_PROPERTY` set to the Qt modifier flag
   - Visual highlight when checked via stylesheet

2. Modify `onKeyButtonClicked()` to:
   - Iterate through all toolbar buttons
   - Collect modifier flags from checked modifier buttons
   - Pass modifiers to `HostManager::handleFunctionKey` or `HostManager::handleKeyCombo`
   - Auto-uncheck modifier buttons after sending (one-shot behavior)

3. Add stylesheet rules to `commonButtonStyle`:
   ```cpp
   "QPushButton[openterface_modifier] { color: palette(highlight); }"
   "QPushButton[openterface_modifier]:checked { background-color: palette(dark); }"
   ```

### Behavior

- User clicks Ctrl button → Ctrl toggles on (highlighted)
- User clicks F1 → Ctrl+F1 is sent to target, Ctrl auto-unchecks
- User clicks Ctrl again → Ctrl toggles off

This matches the old virtual keyboard behavior from commit 34d7ace.

## Section 2: Preset Combo Buttons

**File**: `config/customkeys/default.json`

### Changes

Add preset combo entries after the F-keys section:

```json
{"displayName": "Ctrl+Alt+F1", "keyCodes": [16777248, 16777249, 63], "isSeparator": false},
{"displayName": "Ctrl+Alt+F2", "keyCodes": [16777248, 16777249, 64], "isSeparator": false},
{"displayName": "Ctrl+Alt+F3", "keyCodes": [16777248, 16777249, 65], "isSeparator": false},
{"displayName": "Ctrl+Alt+F4", "keyCodes": [16777248, 16777249, 66], "isSeparator": false},
{"displayName": "Ctrl+Alt+F5", "keyCodes": [16777248, 16777249, 67], "isSeparator": false},
{"displayName": "Ctrl+Alt+F6", "keyCodes": [16777248, 16777249, 68], "isSeparator": false},
{"displayName": "Ctrl+Alt+F7", "keyCodes": [16777248, 16777249, 69], "isSeparator": false}
```

### Key Codes

- 16777248 = Qt::Key_Control
- 16777249 = Qt::Key_Alt
- 63 = Qt::Key_F1, 64 = F2, 65 = F3, 66 = F4, 67 = F5, 68 = F6, 69 = F7

### Behavior

These buttons use the existing `handleKeyCombo` path:
- Press Ctrl and Alt
- Press the F-key
- Hold for 50ms
- Release F-key
- Release Alt and Ctrl (in reverse order)

## Section 3: SystemKeyBlocker Auto-disable on Focus Loss

**Files**: `SysKeyBlocker/SystemKeyBlocker_win.cpp`, `SysKeyBlocker/SystemKeyBlocker_x11.cpp`, `SysKeyBlocker/SystemKeyBlocker.h`

### Changes

1. Add a `QTimer` (500ms interval) to each platform implementation that checks if the OpenterfaceQT window is in the foreground

2. When focus is lost:
   - Set `m_swallowEnabled = false`
   - Reset all modifier state in `g_modifierState`
   - Log the event

3. When focus returns:
   - Restore the previous swallow state (if it was enabled before losing focus)

### Implementation Details

**Windows**:
```cpp
// In lowLevelKeyboardProc, already checks foreground window
// Add timer to monitor focus changes
QTimer *focusTimer = new QTimer(this);
connect(focusTimer, &QTimer::timeout, this, [this]() {
    HWND foregroundWnd = GetForegroundWindow();
    HWND hookedWnd = (HWND)m_hookedHwnd;
    bool isOurWindowFocused = (foregroundWnd == hookedWnd) || 
                              IsChild(hookedWnd, foregroundWnd) || 
                              IsChild(foregroundWnd, hookedWnd);
    if (!isOurWindowFocused && m_swallowEnabled) {
        m_swallowEnabled = false;
        // Reset modifier state
        g_modifierState = ModifierKeyState();
        qCInfo(log_syskey_win) << "Window lost focus, disabled swallowing";
    }
});
focusTimer->start(500);
```

**X11**:
```cpp
// Add timer to monitor focus changes
QTimer *focusTimer = new QTimer(this);
connect(focusTimer, &QTimer::timeout, this, [this]() {
    // Query _NET_ACTIVE_WINDOW to get the currently focused window
    // Compare with our window's X11 ID
    bool isOurWindowFocused = checkX11FocusWindow();
    if (!isOurWindowFocused && m_swallowEnabled) {
        m_swallowEnabled = false;
        // Reset modifier state
        g_modifierState = ModifierKeyState();
        qCInfo(log_syskey_x11) << "Window lost focus, disabled swallowing";
    }
});
focusTimer->start(500);
```

Helper function for X11 focus check:
```cpp
bool SystemKeyBlocker::checkX11FocusWindow() {
    // Use XGetWindowProperty to query _NET_ACTIVE_WINDOW
    // Return true if it matches our window or a child
}
```

### Trade-offs

- **Polling (500ms timer)**: Simple, lightweight, works across platforms
- **Event-driven**: More efficient but requires native window hooks (complex)

Given the low cost of a 500ms timer, polling is acceptable.

## Testing

1. **Modifier toggle test**:
   - Click Ctrl → verify it highlights
   - Click F1 → verify Ctrl+F1 is sent to target
   - Verify Ctrl auto-unchecks

2. **Preset combo test**:
   - Click Ctrl+Alt+F1 button
   - Verify the sequence is sent to target
   - Verify host TTY switch occurs (if SystemKeyBlocker is off)

3. **Focus loss test**:
   - Enable SystemKeyBlocker
   - Press Ctrl+Alt+F1 → verify it's blocked
   - Alt-Tab to another window
   - Verify SystemKeyBlocker auto-disables
   - Press Ctrl+Alt+F1 → verify host TTY switch occurs
   - Alt-Tab back to OpenterfaceQT
   - Verify SystemKeyBlocker re-enables

## Files Changed

- `ui/toolbar/toolbarmanager.cpp`
- `ui/toolbar/toolbarmanager.h`
- `config/customkeys/default.json`
- `SysKeyBlocker/SystemKeyBlocker_win.cpp`
- `SysKeyBlocker/SystemKeyBlocker_x11.cpp`
- `SysKeyBlocker/SystemKeyBlocker.h`

## Migration

No migration needed. Users with existing custom key configs will get the new preset combos only if they reset to default or manually add them. The modifier toggle buttons are hardcoded and appear for all users.
