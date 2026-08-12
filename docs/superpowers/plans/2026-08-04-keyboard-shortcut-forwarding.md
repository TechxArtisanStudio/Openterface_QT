# Keyboard Shortcut Forwarding Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When VideoPane has keyboard focus and SystemKeyBlocker is OFF, disable all MainWindow-level QShortcuts/QActions so that key combos (Ctrl+C, Ctrl+V, etc.) flow through to the target machine instead of being intercepted by the app's own shortcut system.

**Architecture:** Listen to `QApplication::focusChanged` and `SystemKeyBlocker::captureStateChanged`. When VideoPane (or its GStreamer overlay) has focus AND SystemBlocker is inactive, call `setEnabled(false)` on all QShortcut and QAction objects with shortcuts that are children of MainWindow. Re-enable them when focus leaves VideoPane or SystemBlocker activates.

**Tech Stack:** Qt 6, C++17, QMainWindow, QApplication, QShortcut, QAction

**Spec:** `docs/superpowers/specs/2026-08-04-keyboard-shortcut-forwarding-design.md`

## Global Constraints

- Only modify `ui/mainwindow.h` and `ui/mainwindow.cpp`
- No changes to `.ui` files, `InputHandler`, `KeyboardManager`, or `SystemKeyBlocker`
- Existing Shift+Arrow zoom-pan behavior must be preserved
- Existing Escape-key "release mouse" behavior must be preserved
- Must handle both VideoPane and its GStreamer overlay widget for focus checks

---

### Task 1: Add `syncShortcutsState` slot and member to MainWindow header

**Files:**
- Modify: `ui/mainwindow.h`

- [ ] **Step 1: Add the private slot declaration**

In `ui/mainwindow.h`, inside the `private slots:` section (around line 161-255), add:

```cpp
    void syncShortcutsState();
```

Insert it after the existing `onInputResolutionChanged();` slot (line 254), before the `protected:` section.

- [ ] **Step 2: Add the `m_shortcutsDisabled` member variable**

In the `private:` section (around line 284), add:

```cpp
    bool m_shortcutsDisabled = false;
```

Insert it after `bool m_closeEventHandled = false;` (line 313) to keep related boolean flags together.

- [ ] **Step 3: Verify header compiles**

Build the project to confirm no syntax errors:

```bash
cd build && cmake --build . --target openterfaceQT 2>&1 | tail -20
```

Expected: build succeeds (slot is declared but not yet defined — this will cause a linker error, which is expected and will be resolved in Task 2).

- [ ] **Step 4: Commit header changes**

```bash
git add ui/mainwindow.h
git commit -m "feat(keyboard): declare syncShortcutsState slot and m_shortcutsDisabled member"
```

---

### Task 2: Implement `syncShortcutsState` and connect signals in MainWindow

**Files:**
- Modify: `ui/mainwindow.cpp`

**Interfaces:**
- Consumes: `SystemKeyBlocker::instance().isActive()` (from `SysKeyBlocker/SystemKeyBlocker.h`, already included at line 87)
- Consumes: `videoPane` (member, line 287 of header)
- Consumes: `videoPane->getOverlayWidget()` (returns `QWidget*`, defined in `ui/videopane.h:75`)
- Produces: `syncShortcutsState()` — called by `QApplication::focusChanged` and `SystemKeyBlocker::captureStateChanged` signals

- [ ] **Step 1: Add the `connect()` calls in the constructor**

In `ui/mainwindow.cpp`, inside `MainWindow::MainWindow()`, after `m_initializer->initialize();` (line 156), add:

```cpp
    // Disable app shortcuts when VideoPane has focus so key combos forward to target.
    // Also re-sync when SystemBlocker state changes (turning it off while VideoPane
    // has focus must disable shortcuts immediately, even though focus hasn't moved).
    connect(qApp, &QApplication::focusChanged,
            this, &MainWindow::syncShortcutsState);
    connect(&SystemKeyBlocker::instance(), &SystemKeyBlocker::captureStateChanged,
            this, &MainWindow::syncShortcutsState);
```

Note: `SystemKeyBlocker.h` is already included at line 87 of `mainwindow.cpp`.

- [ ] **Step 2: Add the `syncShortcutsState()` implementation**

In `ui/mainwindow.cpp`, add the method. A good location is near the end of the file, before the last `#include` block or after an existing method like `onInputResolutionChanged()`. Use the following implementation:

```cpp
void MainWindow::syncShortcutsState()
{
    // Check if VideoPane or its GStreamer overlay widget has keyboard focus.
    // focusWidget() returns the widget that currently has focus within the application.
    QWidget *focused = focusWidget();
    bool videoHasFocus = (focused == videoPane)
                      || (videoPane && focused == videoPane->getOverlayWidget());

    // Disable shortcuts only when VideoPane has focus AND SystemBlocker is not active.
    // When SystemBlocker IS active, its OS-level hook swallows all key events before
    // Qt sees them, so shortcuts never fire regardless. We still track state so that
    // turning SystemBlocker off restores correct behavior without needing a focus change.
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
    qCDebug(log_ui_mainwindow) << "syncShortcutsState: videoHasFocus=" << videoHasFocus
                               << "systemBlockerActive=" << SystemKeyBlocker::instance().isActive()
                               << "shortcutsDisabled=" << m_shortcutsDisabled;
}
```

- [ ] **Step 3: Build and verify compilation**

```bash
cd build && cmake --build . --target openterfaceQT 2>&1 | tail -20
```

Expected: build succeeds with no errors.

- [ ] **Step 4: Commit implementation**

```bash
git add ui/mainwindow.cpp
git commit -m "feat(keyboard): disable app shortcuts when VideoPane has focus

When VideoPane (or GStreamer overlay) has keyboard focus and SystemKeyBlocker
is inactive, all MainWindow QShortcuts and QActions with shortcuts are disabled.
This allows key combos like Ctrl+C/V to flow through to the target machine
via InputHandler instead of being intercepted by app-level shortcut handlers.

Listens to QApplication::focusChanged and SystemKeyBlocker::captureStateChanged
to keep shortcut state in sync across focus and blocker state transitions.

Fixes: Ctrl+V, Ctrl+Shift+S, and other app shortcuts no longer intercept
keystrokes intended for the target machine when VideoPane has focus."
```

---

### Task 3: Manual verification

This project has no Qt unit test infrastructure. The following manual tests verify the behavior described in the spec.

- [ ] **Step 1: Build and launch the app**

```bash
cd build && cmake --build . --target openterfaceQT
# Launch the app
./openterfaceQT
```

- [ ] **Step 2: Test Ctrl+V forwarding (SystemBlocker OFF)**

1. Ensure SystemKeyBlocker is **OFF** (default state, check in settings → Log page)
2. Click on the VideoPane area to give it keyboard focus
3. Press `Ctrl+V`
4. **Expected:** The `Ctrl+V` keystroke is sent to the target machine (visible in target OS). The host clipboard is NOT pasted.

- [ ] **Step 3: Test Ctrl+C forwarding (SystemBlocker OFF)**

1. With VideoPane focused and SystemBlocker OFF
2. Press `Ctrl+C`
3. **Expected:** The `Ctrl+C` keystroke is sent to the target machine

- [ ] **Step 4: Test other intercepted shortcuts (SystemBlocker OFF)**

1. With VideoPane focused and SystemBlocker OFF
2. Press `Ctrl+Shift+S` (was screenshot shortcut)
3. **Expected:** `Ctrl+Shift+S` is forwarded to the target machine, screenshot is NOT taken
4. Press `Ctrl+Shift+K` (was virtual keyboard shortcut)
5. **Expected:** `Ctrl+Shift+K` is forwarded to target

- [ ] **Step 5: Test Shift+Arrow panning preserved**

1. Zoom into the video (use the zoom controls)
2. With VideoPane focused, press `Shift+Up`
3. **Expected:** Video pans up (local behavior preserved, NOT forwarded to target)

- [ ] **Step 6: Test app shortcuts work when VideoPane does NOT have focus**

1. Click on the menu bar or any non-VideoPane widget
2. Press `Ctrl+P`
3. **Expected:** Preferences dialog opens (shortcut works normally)

- [ ] **Step 7: Test SystemBlocker state transition**

1. Click on VideoPane (give it focus)
2. Turn SystemBlocker ON (via settings)
3. Turn SystemBlocker OFF
4. Press `Ctrl+V`
5. **Expected:** `Ctrl+V` is forwarded to target (not intercepted)

- [ ] **Step 8: Regression — SystemBlocker ON**

1. Turn SystemBlocker ON
2. Press various keys including `Ctrl+V`, `Ctrl+C`, `Ctrl+Shift+S`
3. **Expected:** All keys go to target as before (no regression)

- [ ] **Step 9: Commit any test-related changes (if applicable)**

If no code changes were needed during manual testing:

```bash
# No commit needed — manual testing only
```

---

## Summary of all changes

| File | Change | Lines |
|---|---|---|
| `ui/mainwindow.h` | Add `void syncShortcutsState();` slot declaration | ~line 255 |
| `ui/mainwindow.h` | Add `bool m_shortcutsDisabled = false;` member | ~line 313 |
| `ui/mainwindow.cpp` | Add `connect()` calls in constructor | ~line 157 |
| `ui/mainwindow.cpp` | Add `syncShortcutsState()` implementation | new method |
