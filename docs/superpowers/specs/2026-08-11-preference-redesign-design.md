# Preferences Dialog Redesign — Design Spec

## Overview

Merge `SettingDialog` (Preferences) and `AdvancedSettingsDialog` into a unified Preferences dialog, and replace the three global OK/Apply/Cancel buttons at the bottom with each settings page embedding its own three buttons: **Apply / Revert / Cancel (Close)**.

## Core Decisions

| Decision | Conclusion |
|----------|------------|
| Revert button semantics | **Snapshot mode (Option B)**: record the page's values at open time as the baseline. Revert restores to that snapshot. The snapshot is NOT updated after Apply. |
| Button name | **Revert** |
| Dialog merge approach | **Flatten into a single list** — no longer distinguish General / Advanced |
| Page classification | Settings pages (require three buttons) vs. Operation pages (keep existing buttons) |

## Page Inventory

The merged Preferences dialog contains the following pages:

| Page | Source | Type | Requires Apply / Revert / Cancel |
|------|--------|------|----------------------------------|
| General (formerly LogPage) | SettingDialog | Settings page | ✅ Yes |
| Video | SettingDialog | Settings page | ✅ Yes |
| Audio | SettingDialog | Settings page | ✅ Yes |
| Target Control | SettingDialog | Settings page | ✅ Yes |
| MCP | AdvancedSettingsDialog | Settings page | ✅ Yes |
| Video Firmware | AdvancedSettingsDialog | Operation page | ❌ Keep existing buttons |
| Control Chip Firmware | AdvancedSettingsDialog | Operation page | ❌ Keep existing buttons |
| EDID Configuration | AdvancedSettingsDialog | Operation page | ❌ Keep existing buttons |
| Virtual Keyboard | AdvancedSettingsDialog | Operation page | ❌ Keep existing buttons (drag-edit + auto-save) |

**Classification criteria:**
- **Settings page** = user modifies control values; Apply writes values to GlobalSetting; Revert discards unsaved changes
- **Operation page** = triggers hardware operations (firmware read/write, EDID write, etc.); inherently action-oriented, no "save/revert" semantics needed

## Button Bar Design

### Layout

Each settings page gets a button bar at the bottom:

```
[ Apply ]  [ Revert ]  [ Cancel (Close) ]
```

- Button bar is right-aligned (via `addStretch()` on the left)
- Button size: fixed 80×30 (consistent with existing style)
- The button bar is part of the page's own layout

### Semantics of the Three Buttons

| Button | Function |
|--------|----------|
| **Apply** | Write current control values to GlobalSetting (persist + take effect immediately); does not close the dialog |
| **Revert** | Restore all controls to the snapshot values captured when the page was opened (discard all unsaved changes) |
| **Cancel** | Close the entire dialog without saving any changes that haven't been Applied |

### Snapshot Mechanism

Each settings page must implement three methods:

```cpp
void captureSnapshot();   // Record current values of all controls at page open time
void applySettings();     // Write current control values to GlobalSetting (already exists)
void revertToSnapshot();  // Restore controls to the snapshot values recorded by captureSnapshot
```

**Key rules:**

1. `captureSnapshot()` is called once during page initialization (i.e., at the end of `init*Settings()`)
2. **The snapshot is NOT updated after Apply** — this is the chosen Option B semantics
3. Snapshot values are stored in type-safe member variables for each control

**Example (McpPage snapshot):**

```cpp
// Snapshot members
bool    m_snap_enableChecked;
int     m_snap_transportIndex;
int     m_snap_ssePort;
int     m_snap_sseBindPresetIndex;
QString m_snap_sseBindCustom;
// ... remaining SSE fields

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
    // ... trigger associated UI state updates (e.g., SSE group show/hide)
}
```

### Button Bar Helper Function

To avoid duplicating button bar creation code in every page, `SettingDialog` provides a helper function:

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

Each settings page calls this helper at the end of `setupUI()`, passing its own Apply and Revert callbacks.

### Unified Cancel Button Handling

The Cancel button is connected to `QDialog::reject()`, which closes the dialog. Cancel behavior is consistent across all pages.

**Note:** Cancel does not prompt to ask whether to save. If the user made changes but didn't Apply, they are simply discarded.

## Per-Page Changes

### LogPage (General page)

- Add button bar at the end of `setupUI()`
- Add `captureSnapshot()` and `revertToSnapshot()` methods
- Call `captureSnapshot()` at the end of `initLogSettings()`

**Snapshot fields:**

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

**Apply callback:** call existing `applyLogsettings()`
**Revert callback:** call `revertToSnapshot()`

### VideoPage

- Add button bar at the end of `setupUI()`
- Add `captureSnapshot()` and `revertToSnapshot()` methods
- Call `captureSnapshot()` at the end of `initVideoSettings()`

**Snapshot fields:**

```cpp
int m_snap_videoFormatIndex;
int m_snap_pixelFormatIndex;
int m_snap_fpsIndex;
QSize m_snap_resolution;
bool m_snap_customResolutionChecked;
int m_snap_customWidth, m_snap_customHeight;
// ... remaining video-related controls
```

**Apply callback:** call existing `applyVideoSettings()`
**Revert callback:** call `revertToSnapshot()`

### AudioPage

- Add button bar at the end of `setupUI()`
- Add `captureSnapshot()` and `revertToSnapshot()` methods
- Call `captureSnapshot()` at the end of `loadSettings()`

**Snapshot fields:**

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

**Apply callback:** call existing `saveSettings()`
**Revert callback:** call `revertToSnapshot()`

### TargetControlPage

- Add button bar at the end of `setupUI()`
- Add `captureSnapshot()` and `revertToSnapshot()` methods
- Call `captureSnapshot()` at the end of `initHardwareSetting()`

**Snapshot fields:**

```cpp
bool m_snap_vidChecked, m_snap_pidChecked;
QString m_snap_vidText, m_snap_pidText;
QString m_snap_vidDescriptor, m_snap_pidDescriptor;
bool m_snap_usbSerialNumberChecked;
QString m_snap_serialNumberText;
bool m_snap_usbCustomStringChecked;
int m_snap_operatingMode;  // 0=Full, 1=KeyboardOnly, 2=KeyboardMouse, 3=CustomHID
```

**Apply callback:** call existing `applyHardwareSetting()`
**Revert callback:** call `revertToSnapshot()`

### McpPage

- Add button bar at the end of `setupUI()`
- Add `captureSnapshot()` and `revertToSnapshot()` methods
- Call `captureSnapshot()` at the end of `initMcpSettings()`

**Snapshot fields:**

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

**Apply callback:** call existing `applyMcpSettings()`
**Revert callback:** call `revertToSnapshot()`

### Operation Pages (unchanged)

The following pages do NOT get an Apply/Revert/Cancel button bar; they keep their existing buttons:

- **FirmwarePage** — keep Check for Updates / Backup / Write / Cancel
- **ControlChipFirmwarePage** — keep Scan / Connect / Disconnect / Flash
- **EdidConfigPage** — keep Read / Apply / Cancel Reading (this is a firmware operation, not a settings save)
- **VirtualKeyboardPage** — keep drag-edit + auto-save mechanism

## Dialog Merge Implementation

### SettingDialog Refactoring

The class name remains `SettingDialog` unchanged.

**New members (migrated from AdvancedSettingsDialog):**

```cpp
FirmwarePage *firmwarePage;
ControlChipFirmwarePage *controlChipFirmwarePage;
McpPage *mcpPage;
EdidConfigPage *edidConfigPage;
VirtualKeyboardPage *virtualKeyboardPage;
```

**createSettingTree() modification:**

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

**createPages() modification:** call `addScrollablePage` for all 9 pages in sequence.

**Deletions:**
- `buttonWidget` member
- `createButtons()` method
- `handleOkButton()` method
- `applyAccrodingPage()` method

**changePage() modification:** support index mapping for all 9 pages.

### Public Interface

Keep existing getter methods + add getters for migrated pages:

```cpp
TargetControlPage* getTargetControlPage();
VideoPage* getVideoPage();
LogPage* getLogPage();
McpPage* getMcpPage();                      // New
FirmwarePage* getFirmwarePage();            // New
VirtualKeyboardPage* getVirtualKeyboardPage();  // New
```

Add optional `selectPage(const QString& pageName)` method.

## MainWindow Changes

### Merging Dialog Entry Points

- Keep `configureSettings()` to open the merged `SettingDialog`
- Delete `configureAdvancedSettings()` method
- Menu items that previously called `configureAdvancedSettings()` now call `configureSettings()`

### Signal Connection Refactoring

Merge all signal connections inside `configureSettings()`:

```cpp
void MainWindow::configureSettings() {
    if (!settingDialog) {
        settingDialog = new SettingDialog(m_cameraManager, this);

        // Existing connections (LogPage, VideoPage)
        LogPage* logPage = settingDialog->getLogPage();
        connect(logPage, &LogPage::ScreenSaverInhibitedChanged, ...);
        // ... remaining LogPage connections
        
        VideoPage* videoPage = settingDialog->getVideoPage();
        connect(videoPage, &VideoPage::videoSettingsChanged, ...);
        
        // New connections (migrated from AdvancedSettingsDialog)
        McpPage* mcpPage = settingDialog->getMcpPage();
        connect(mcpPage, &McpPage::mcpSettingsChanged, this, &MainWindow::onMcpSettingsApplied);

        FirmwarePage* firmwarePage = settingDialog->getFirmwarePage();
        connect(firmwarePage, &FirmwarePage::firmwareUpdateCompleted,
                this, []() { QApplication::quit(); });
        
        // Dialog close cleanup
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

### Header File Changes

**mainwindow.h:**
- Delete `class AdvancedSettingsDialog;` forward declaration
- Delete `AdvancedSettingsDialog *advancedSettingsDialog;` member variable
- Delete `void configureAdvancedSettings();` method declaration

**mainwindow.cpp:**
- Delete `#include "ui/preferences/advancedsettingsdialog.h"`
- Delete `configureAdvancedSettings()` method implementation
- Update menu connections

## Build System and File Inventory

### CMakeLists.txt Changes

Remove from source file list:
```cmake
ui/preferences/advancedsettingsdialog.cpp
ui/preferences/advancedsettingsdialog.h
```

### Header File Changes Summary

| File | Changes |
|------|---------|
| `settingdialog.h` | Add 5 page members; delete `buttonWidget`; delete `createButtons()`, `handleOkButton()`, `applyAccrodingPage()`; add `createPageButtons()` helper method; add getter methods; add `selectPage()` |
| `logpage.h` | Add `captureSnapshot()`, `revertToSnapshot()` method declarations; add `m_snap_*` snapshot members |
| `videopage.h` | Same as above |
| `audiopage.h` | Same as above |
| `targetcontrolpage.h` | Same as above |
| `mcppage.h` | Same as above |
| `mainwindow.h` | Delete `AdvancedSettingsDialog`-related declarations |

### Source File Changes Summary

| File | Changes |
|------|---------|
| `settingdialog.cpp` | Merge AdvancedSettingsDialog's page creation and initialization logic; delete global button bar code; expand changePage() to support 9 pages |
| `logpage.cpp` | Add button bar at end of setupUI(); implement captureSnapshot() and revertToSnapshot() |
| `videopage.cpp` | Same as above |
| `audiopage.cpp` | Same as above |
| `targetcontrolpage.cpp` | Same as above |
| `mcppage.cpp` | Same as above |
| `mainwindow.cpp` | Merge all signal connections in configureSettings(); delete configureAdvancedSettings(); update menu connections |

### File Deletion List

```
ui/preferences/advancedsettingsdialog.cpp   # Delete
ui/preferences/advancedsettingsdialog.h     # Delete
```

### Files That Do Not Need Changes

- `firmwarepage.cpp/h` — no changes
- `controlchipfirmwarepage.cpp/h` — no changes
- `edidconfigpage.cpp/h` — no changes
- `virtualkeyboardpage.cpp/h` — no changes

## Edge Cases

### Cancel Does Not Prompt for Confirmation

When the user clicks Cancel on any page with unapplied changes, the dialog closes directly. No "Do you want to save?" prompt is shown.

### Opening the Dialog Multiple Times

Each time the dialog is opened, all settings pages re-run `init*Settings()`, reading the latest values from GlobalSetting and calling `captureSnapshot()`. The snapshot is always the latest on-disk values.

### Revert Immediately After Apply

Apply writes values to GlobalSetting. Revert restores to the snapshot from when the page was opened (the snapshot is not updated). So if the user changes A → Apply → changes B → Revert → returns to A (not A' which was the post-Apply state).

### No Unsaved Prompt When Switching Pages

When switching pages, the switch happens directly. Unapplied changes on the current page remain in the controls. The user can continue editing on the current page or click Revert to discard.

### Closing Dialog During Operation

The block-close logic for hardware operation pages like FirmwarePage is their own responsibility and is outside the scope of this design.

## Testing Strategy

### Manual Testing Checklist

**Basic functionality:**
1. Open Preferences — confirm all 9 pages appear in the left sidebar tree
2. Click each page — confirm it switches correctly
3. Confirm each settings page has three buttons at the bottom
4. Confirm operation pages retain their original buttons

**Apply / Revert / Cancel testing:**
5. Change a value on the General page → Apply → confirm GlobalSetting is updated
6. Change a value on the General page → Revert → confirm controls restore to the values from when the page was opened
7. Change a value on the General page → Cancel → confirm the dialog closes and GlobalSetting is not modified
8. Reopen the dialog — confirm the previous changes were indeed not saved

**MCP page specific tests:**
9. Change transport mode → Apply → confirm `mcpSettingsChanged` signal is triggered
10. Change SSE port → Revert → confirm port value is restored
11. Change multiple values → Cancel → confirm dialog closes and MCP configuration is unchanged

**Cross-page:**
12. Change a value on the Video page → switch to Audio → switch back to Video → confirm Video's changes are still there
13. Change values on multiple pages → Cancel → confirm no pages' changes were saved

**Signal connections:**
14. Modify MCP configuration and Apply → confirm MCP server restarts
15. Modify System Key Blocker and Apply → confirm behavior changes

### Regression Testing

- Existing SettingDialog functionality should not be affected
- Pages from the former AdvancedSettingsDialog should not be affected
