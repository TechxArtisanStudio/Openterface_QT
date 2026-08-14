# Preference Dialog Improvements — Design Spec

**Date:** 2026-08-14
**Scope:** SystemKeyBlocker input fix, button styling, dirty-state tracking, unsaved-changes dialog

---

## 1. SystemKeyBlocker: Only Block Keys When VideoPane Has Focus

### Problem
When SystemBlocker is enabled (swallow=true), the X11 native event filter and Windows keyboard hook swallow ALL key events whenever any window of the app has focus. This means input fields in the preference dialog cannot receive keyboard input.

### Solution
Restrict key swallowing to only when the focus is inside the VideoPane widget.

### Interface Changes

**SystemKeyBlocker.h** — new methods:
```cpp
void setFocusTarget(QWidget *target);
QWidget *focusTarget() const;
```
Add `QWidget *m_focusTarget = nullptr;` member.

### X11 Filter Changes (SystemKeyBlocker_x11.cpp)

In `nativeEventFilter()`, replace:
```cpp
if (!QGuiApplication::focusWindow()) return false;
```
With:
```cpp
if (!m_blocker->focusTarget()) return false;
QWidget *fw = QApplication::focusWidget();
if (!fw || !m_blocker->focusTarget()->isAncestorOf(fw)) return false;
```
Note: `isAncestorOf` returns true if `fw == focusTarget` or `fw` is a child/descendant.

### Windows Hook Changes (SystemKeyBlocker_win.cpp)

In `lowLevelKeyboardProc()`, replace the `GetForegroundWindow()` / `IsChild()` checks with:
```cpp
if (!s_self->m_focusTarget) {
    return CallNextHookEx(...);  // no target set, pass through
}
HWND focusHwnd = GetFocus();
HWND targetHwnd = (HWND)s_self->m_focusTarget->winId();
if (focusHwnd != targetHwnd && !IsChild(targetHwnd, focusHwnd)) {
    return CallNextHookEx(...);  // focus not in VideoPane, pass through
}
```

### Initialization (mainwindow.cpp)

After creating `videoPane`, call:
```cpp
SystemKeyBlocker::instance().setFocusTarget(videoPane);
```

### Edge Cases
- `focusTarget` is null → never swallow (safe fallback)
- VideoPane child widgets (overlay, etc.) count as "inside VideoPane"
- When preference dialog opens → focus moves to dialog → swallow stops automatically
- When preference dialog closes and VideoPane regains focus → swallow resumes

---

## 2. PreferencePageBase — Shared Base Class

### Goal
Extract common Apply/Revert/Cancel button creation, styling, and dirty-state tracking into a base class. Only used by settings pages with apply/revert semantics.

### Pages That Use PreferencePageBase
- LogPage
- VideoPage
- AudioPage
- TargetControlPage
- McpPage

### Pages NOT Modified (direct-operation pages)
- FirmwarePage
- ControlChipFirmwarePage
- EdidConfigPage
- VirtualKeyboardPage

### Class Definition

```cpp
// ui/preferences/preferencepagebase.h
class PreferencePageBase : public QWidget
{
    Q_OBJECT

public:
    explicit PreferencePageBase(QWidget *parent = nullptr);

    // Subclasses must implement
    virtual void captureSnapshot() = 0;
    virtual void applySettings() = 0;
    virtual void revertToSnapshot() = 0;

    bool isDirty() const { return m_isDirty; }

signals:
    void dirtyChanged(bool dirty);

protected:
    QPushButton *m_applyButton;
    QPushButton *m_revertButton;
    QPushButton *m_cancelButton;

    void createButtonBar(QLayout *parentLayout);
    void markDirty();
    void clearDirty();
    void updateButtonStyles();

private:
    bool m_isDirty = false;

    static QString defaultButtonStyle();
    static QString dirtyButtonStyle();
};
```

### Button Styles

**Default (white with shadow):**
```css
QPushButton {
    background-color: #ffffff;
    border: 1px solid #cccccc;
    border-radius: 4px;
    padding: 4px 16px;
    min-width: 80px;
    min-height: 28px;
    color: #333333;
}
QPushButton:hover {
    background-color: #f0f0f0;
    border-color: #999999;
}
QPushButton:pressed {
    background-color: #e0e0e0;
}
```

**Dirty Apply button (orange):**
```css
QPushButton#applyButton[dirty="true"] {
    background-color: #ff8c00;
    border: 1px solid #e07000;
    color: white;
    font-weight: bold;
}
QPushButton#applyButton[dirty="true"]:hover {
    background-color: #ff9920;
}
```

### Button Behavior (connected in createButtonBar)
- **Apply** → `applySettings()` → `captureSnapshot()` → `clearDirty()`
- **Revert** → `revertToSnapshot()` → `markDirty()`
- **Cancel** → `QDialog::reject()`

### Subclass Migration Pattern
1. Change inheritance: `QWidget` → `PreferencePageBase`
2. Remove local button creation code (applyButton, revertButton, cancelButton)
3. Call `createButtonBar(layout)` at end of `setupUI()`
4. Connect all settings widget change signals to `markDirty()`
5. Rename apply method to `applySettings()` (override)
6. Add `override` to `captureSnapshot()` and `revertToSnapshot()`

---

## 3. SettingDialog — Unsaved Changes Protection

### Goal
When the user switches pages or closes the dialog with unsaved changes, show a save/discard/cancel prompt.

### SettingDialog Changes

**New members:**
```cpp
QList<PreferencePageBase*> m_pages;  // unified list of all settings pages
```

**New methods:**
```cpp
bool hasUnsavedChanges() const;
void applyAllDirtyPages();
QMessageBox::StandardButton promptSaveDiscardCancel();
void closeEvent(QCloseEvent *event) override;
```

### Page Switch Guard (in changePage)
```
if (hasUnsavedChanges()) {
    result = promptSaveDiscardCancel()
    Save   → applyAllDirtyPages(), then switch
    Discard → switch directly
    Cancel → restore previous tree selection, return
}
```

### Close Guard (closeEvent override)
```
if (hasUnsavedChanges()) {
    result = promptSaveDiscardCancel()
    Save   → applyAllDirtyPages(), accept close
    Discard → accept close
    Cancel → ignore close event (prevent closing)
}
```

### Unsaved Changes Dialog
```
Title: "未保存的更改"
Text: "你有未保存的更改。"
Info: "是否保存更改？"
Buttons: Save | Discard | Cancel
Default: Save
```

### applyAllDirtyPages
Only calls `applySettings()` + `captureSnapshot()` + `clearDirty()` on pages where `isDirty() == true`.

---

## 4. Migration Checklist

| Page | File | Changes |
|------|------|---------|
| LogPage | logpage.h/cpp | Inherit base, remove local buttons, use createButtonBar(), rename applyLogsettings→applySettings, connect changes to markDirty() |
| VideoPage | videopage.h/cpp | Same pattern |
| AudioPage | audiopage.h/cpp | Same pattern |
| TargetControlPage | targetcontrolpage.h/cpp | Same pattern |
| McpPage | mcppage.h/cpp | Same pattern |

**Unchanged pages:** FirmwarePage, ControlChipFirmwarePage, EdidConfigPage, VirtualKeyboardPage

---

## 5. File Changes Summary

| File | Action |
|------|--------|
| SysKeyBlocker/SystemKeyBlocker.h | Add setFocusTarget/focusTarget + m_focusTarget member |
| SysKeyBlocker/SystemKeyBlocker_x11.cpp | Change focus check to use focusTarget |
| SysKeyBlocker/SystemKeyBlocker_win.cpp | Change focus check to use focusTarget |
| ui/mainwindow.cpp | Call setFocusTarget(videoPane) |
| ui/preferences/preferencepagebase.h | NEW — base class header |
| ui/preferences/preferencepagebase.cpp | NEW — base class implementation |
| ui/preferences/logpage.h/cpp | Migrate to base class |
| ui/preferences/videopage.h/cpp | Migrate to base class |
| ui/preferences/audiopage.h/cpp | Migrate to base class |
| ui/preferences/targetcontrolpage.h/cpp | Migrate to base class |
| ui/preferences/mcppage.h/cpp | Migrate to base class |
| ui/preferences/settingdialog.h/cpp | Add m_pages list, dirty check, closeEvent, prompt dialog |
| ui/preferences/fontstyle.h | Add button style constants (optional) |
