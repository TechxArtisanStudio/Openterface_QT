# Target Control Chip Firmware — Advanced Settings Integration

**Date:** 2026-08-06
**Status:** Design — approved for implementation

---

## Problem Statement

The Openterface Mini KVM app has a "WCH Flash" menu item in the Advanced menu that opens a standalone `WCHFlashDialog` for flashing firmware to the keyboard/mouse control chip (WCH microcontroller) via USB ISP bootloader.

Issues:
1. The name "WCH Flash" is not descriptive — users don't know it's for the keyboard/mouse control chip firmware.
2. It's a separate menu item and standalone dialog, while other similar tools (video chip firmware, MCP settings) are consolidated inside the Advanced Settings dialog.

The user wants to:
1. **Rename** "WCH Flash" to a more descriptive name.
2. **Integrate** it into the Advanced Settings dialog as a new page.
3. **Rename** the existing "Firmware" page to "Video Firmware" for clarity.

---

## Desired Behavior

### Naming and Page Order

| Before | After |
|--------|-------|
| Firmware | Video Firmware |
| _(standalone "WCH Flash" menu item)_ | Control Chip Firmware |
| MCP | MCP |

The sidebar in Advanced Settings will have three pages in order:
1. **Video Firmware** — main video chip firmware management (existing `FirmwarePage`)
2. **Control Chip Firmware** — keyboard/mouse control chip firmware flashing (new page, migrated from `WCHFlashDialog`)
3. **MCP** — MCP server settings (existing `McpPage`)

### Menu Cleanup

- Remove `actionWCHFlash` from the Advanced menu in `mainwindow.ui`
- Remove `showWCHFlashDialog()` slot and related code from `MainWindow`
- Access to the control chip firmware tool is only through Advanced > Settings

### Button Visibility

- Video Firmware page: hide OK/Apply/Cancel buttons (same as current behavior)
- Control Chip Firmware page: hide OK/Apply/Cancel buttons (self-contained operations)
- MCP page: show OK/Apply/Cancel buttons (existing behavior)

---

## Architecture

### New File: `ui/preferences/targetcontrolfirmwarepage.h` / `.cpp`

A `QWidget` subclass that encapsulates all control chip firmware UI and logic. Refactored from `WCHFlashDialog` (QDialog → QWidget).

```
TargetControlFirmwarePage : QWidget
├── Device 区域
│   ├── m_scanBtn (QPushButton)           — "Scan Devices"
│   ├── m_deviceCombo (QComboBox)         — device list
│   ├── m_connectBtn (QPushButton)        — "Connect"
│   └── m_disconnectBtn (QPushButton)     — "Disconnect"
│
├── Chip Information 区域
│   └── m_chipInfoLabel (QLabel)          — chip details, Monospace font
│
├── Firmware 区域
│   ├── m_firmwareLabel (QLabel)          — selected file path
│   └── m_browseBtn (QPushButton)         — "Browse..."
│
├── m_flashBtn (QPushButton)              — "Flash, Verify && Reset", bold
├── m_progressBar (QProgressBar)          — 0-100%
└── m_logEdit (QTextEdit)                 — read-only log, Monospace font
```

**Key changes from WCHFlashDialog:**
- Base class: `QDialog` → `QWidget`
- Constructor: `explicit TargetControlFirmwarePage(QWidget* parent = nullptr)`
- Removed: window title, close button, dialog flags
- Preserved: all functionality — scan, connect, disconnect, browse, flash, progress, log, USB permission error dialog
- Still uses `WCHFlashWorker` (on a QThread) for all backend operations

**Signals:** None needed — the page is self-contained, no app-exit behavior.

### Modified File: `ui/preferences/advancedsettingsdialog.h` / `.cpp`

**Changes:**

1. Add `#include "targetcontrolfirmwarepage.h"` and `TargetControlFirmwarePage *targetControlFirmwarePage` member.
2. `createSettingTree()` — names change from `{tr("Firmware"), tr("MCP")}` to `{tr("Video Firmware"), tr("Control Chip Firmware"), tr("MCP")}`.
3. `createPages()` — insert `addScrollablePage(targetControlFirmwarePage)` between firmwarePage and mcpPage.
4. `changePage()` — add `if (itemText == tr("Control Chip Firmware")) newPageIndex = 1;`, MCP becomes index 2.
5. `applyAccordingPage()` — add case 1 (Control Chip Firmware) as no-op, MCP becomes case 2.
6. Button visibility — `buttonWidget->setVisible(newPageIndex == 2)` (only MCP page shows buttons).

### Modified File: `ui/mainwindow.h` / `.cpp`

**Removals:**
- `#include "ui/advance/wchflash/WCHFlashDialog.h"`
- `void showWCHFlashDialog();` slot declaration
- `WCHFlashDialog *wchFlashDialog = nullptr;` member
- `showWCHFlashDialog()` method implementation

### Modified File: `ui/mainwindow.ui`

**Remove:**
- `<action name="actionWCHFlash">` definition
- `<addaction name="actionWCHFlash"/>` from menu
- Connection block for `actionWCHFlash` → `showWCHFlashDialog()`
- `<slot>showWCHFlashDialog()</slot>` declaration

### Files Deleted

| File | Reason |
|------|--------|
| `ui/advance/wchflash/WCHFlashDialog.h` | Functionality moved to `TargetControlFirmwarePage` |
| `ui/advance/wchflash/WCHFlashDialog.cpp` | Functionality moved to `TargetControlFirmwarePage` |

### Files Preserved

| File | Reason |
|------|--------|
| `ui/advance/wchflash/WCHFlashWorker.h` | Still used by `TargetControlFirmwarePage` |
| `ui/advance/wchflash/WCHFlashWorker.cpp` | Still used by `TargetControlFirmwarePage` |

### Build System Changes

**CMakeLists.txt:**
- Add `ui/preferences/targetcontrolfirmwarepage.cpp` and `ui/preferences/targetcontrolfirmwarepage.h` to `UI_PREFERENCES_SOURCES`
- Remove `ui/advance/wchflash/WCHFlashDialog.cpp` and `ui/advance/wchflash/WCHFlashDialog.h`

**openterfaceQT.pro:**
- Add `ui/preferences/targetcontrolfirmwarepage.cpp` to `SOURCES`
- Add `ui/preferences/targetcontrolfirmwarepage.h` to `HEADERS`
- Remove `ui/advance/wchflash/WCHFlashDialog.cpp` from `SOURCES`
- Remove `ui/advance/wchflash/WCHFlashDialog.h` from `HEADERS`

---

## Testing Checklist

- [ ] Advanced > Settings opens dialog with three sidebar items: Video Firmware, Control Chip Firmware, MCP
- [ ] Video Firmware page shows correct current firmware version and buttons
- [ ] Control Chip Firmware page shows device scan, connect, firmware browse, flash, progress, log
- [ ] Scan Devices finds WCH ISP devices
- [ ] Connect/Disconnect works correctly
- [ ] Browse selects firmware file (.hex/.bin)
- [ ] Flash operation shows progress and log output
- [ ] USB permission error shows detailed dialog with fix commands
- [ ] OK/Apply/Cancel buttons hidden on Video Firmware and Control Chip Firmware pages
- [ ] OK/Apply/Cancel buttons visible on MCP page
- [ ] Advanced menu no longer shows "WCH Flash" item
- [ ] All existing functionality preserved
