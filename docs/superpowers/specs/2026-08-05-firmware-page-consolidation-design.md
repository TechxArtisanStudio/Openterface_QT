# Firmware Page Consolidation — Advanced Settings

**Date:** 2026-08-05
**Status:** Design — approved for implementation

---

## Problem Statement

The Openterface Mini KVM app has two separate firmware-related dialogs in the Advanced menu:

| Menu Item | Dialog | Functionality |
|---|---|---|
| Firmware Manager | `FirmwareManagerDialog` | Displays current firmware version; provides "Restore firmware" (read EEPROM → .bin) and "Write firmware from bin" (write .bin → EEPROM) buttons. Each operation shows a modal `QProgressDialog`. |
| Firmware Update | `FirmwareUpdateDialog` + `FirmwareUpdateConfirmDialog` | Checks network for latest version, shows confirmation, stops all services, downloads and flashes firmware via progress dialog, exits app on success. |

These are scattered across two separate menu entries and two separate dialog classes. The user wants to:

1. **Consolidate** both into a single unified page within the recently-created Advanced Settings dialog (`AdvancedSettingsDialog`).
2. **Place it above** the existing MCP page in the sidebar (order: Firmware → MCP).
3. **Remove** both menu entries from the Advanced menu — access is only through Advanced > Settings.
4. **Embed all UI inline** — version label, operation buttons, and progress bar all live directly on the page rather than popping out separate dialogs.

---

## Desired Behavior

- **Page layout**: A single `FirmwarePage` widget with three areas:
  1. **Version info** — shows current firmware version from `VideoHid::getFirmwareVersion()`.
  2. **Operation buttons** — three buttons: "Check for Updates and Install", "Backup Firmware to .bin", "Write Firmware from .bin".
  3. **Progress area** — progress bar + status label + cancel button, shown only during an active operation.
- **Operation mutual exclusion**: Only one operation runs at a time. Other buttons are disabled while an operation is in progress.
- **No auto-check on page load**: The page does not automatically check for updates when opened. The user must click the button.
- **Inline progress**: All operations show progress directly on the page (no modal `QProgressDialog`).
- **Window hiding**: During operations, both `AdvancedSettingsDialog` and `MainWindow` are hidden (same as current behavior with the separate dialogs).
- **App exit on success**: After online update or local write completes successfully, the app exits (same as current behavior).
- **Menu cleanup**: Both `actionFirmwareManager` and `actionUpdateFirmware` are removed from the Advanced menu.

---

## Architecture

### New File: `ui/preferences/firmwarepage.h` / `firmwarepage.cpp`

A `QWidget` subclass that encapsulates all firmware management UI and logic.

```
FirmwarePage : QWidget
├── versionLabel (QLabel)          — displays "Current Version: vX.Y.Z"
├── updateButton (QPushButton)     — "Check for Updates and Install"
├── backupButton (QPushButton)     — "Backup Firmware to .bin File"
├── writeButton (QPushButton)      — "Write Firmware from .bin File"
├── progressGroupBox (QGroupBox)   — hidden by default
│   ├── progressBar (QProgressBar)
│   ├── statusLabel (QLabel)
│   └── cancelButton (QPushButton)
└── currentOperation (enum)        — None / Update / Backup / Write
```

**Signals:**
- `firmwareOperationStarted()` — emitted when an operation begins; connected to hide `AdvancedSettingsDialog` and `MainWindow`.
- `firmwareOperationFinished()` — emitted when a non-fatal operation ends; connected to show the dialogs again.
- `firmwareUpdateCompleted()` — emitted only after a successful online update or local write; `MainWindow` connects this to `QApplication::quit()`.

**Internal flow per operation:**

| Operation | Trigger | Backend | Thread Model |
|---|---|---|---|
| Check for Updates and Install | `VideoHid::isLatestFirmware()` → confirm → stop services → `VideoHid::loadFirmwareToEeprom()` | `FirmwareOperationManager` signals: `progress()`, `writeCompleted()` | Runs on GUI thread via `FirmwareOperationManager` (existing pattern) |
| Backup to .bin | `QFileDialog::getSaveFileName()` → `FirmwareReader` | `FirmwareReader::progress()`, `FirmwareReader::finished()` | Background `QThread` with `FirmwareReader` worker (existing pattern) |
| Write from .bin | `QFileDialog::getOpenFileName()` → `FirmwareWriter` | `FirmwareWriter::progress()`, `FirmwareWriter::finished()` | Background `QThread` with `FirmwareWriter` worker (existing pattern) |

All three operations reuse the **exact same backend classes** as the existing dialogs — only the UI presentation layer changes.

### Modified File: `ui/preferences/advancedsettingsdialog.h` / `.cpp`

**Changes:**

1. Add `#include "firmwarepage.h"` and `FirmwarePage *firmwarePage` member.
2. Add `FirmwarePage* getFirmwarePage()` public method.
3. Tree names change from `{tr("MCP")}` to `{tr("Firmware"), tr("MCP")}`.
4. `createPages()`: add `addScrollablePage(firmwarePage)` before `addScrollablePage(mcpPage)`.
5. `changePage()`: add `if (itemText == tr("Firmware")) newPageIndex = 0;`, MCP becomes index 1.
6. `applyAccordingPage()`: case 0 (Firmware) → no-op; case 1 (MCP) → `mcpPage->applyMcpSettings()`.
7. Constructor: connect `firmwarePage->firmwareOperationStarted()` to hide the dialog; connect `firmwarePage->firmwareOperationFinished()` to show the dialog.

### Modified File: `ui/mainwindow.h` / `.cpp`

**Removals:**
- `#include "ui/advance/firmwareupdatedialog.h"` (if present)
- `#include "ui/advance/firmwaremanagerdialog.h"` (if present)
- `void showFirmwareManagerDialog();` slot declaration
- `void updateFirmware();` slot declaration
- `FirmwareManagerDialog *firmwareManagerDialog = nullptr;` member
- `showFirmwareManagerDialog()` method implementation
- `updateFirmware()` method implementation

**Modifications to `configureAdvancedSettings()`:**
```cpp
void MainWindow::configureAdvancedSettings() {
    if (!advancedSettingsDialog) {
        advancedSettingsDialog = new AdvancedSettingsDialog(this);

        // MCP signal connection (existing)
        McpPage* mcpPage = advancedSettingsDialog->getMcpPage();
        connect(mcpPage, &McpPage::mcpSettingsChanged,
                this, &MainWindow::onMcpSettingsApplied);

        // Firmware signal connection (new)
        FirmwarePage* firmwarePage = advancedSettingsDialog->getFirmwarePage();
        connect(firmwarePage, &FirmwarePage::firmwareUpdateCompleted,
                this, []() { QApplication::quit(); });

        connect(advancedSettingsDialog, &QDialog::finished, this, [this]() {
            advancedSettingsDialog->deleteLater();
            advancedSettingsDialog = nullptr;
        });

        advancedSettingsDialog->show();
    } else {
        advancedSettingsDialog->raise();
        advancedSettingsDialog->activateWindow();
    }
}
```

**Destructor cleanup:**
- Remove `firmwareManagerDialog` deletion block (the one that checks and deletes `firmwareManagerDialog`).

### Modified File: `ui/mainwindow.ui`

**Remove from `menuAdvance`:**
```xml
<addaction name="actionFirmwareManager"/>
<addaction name="actionUpdateFirmware"/>
```

**Remove action definitions:**
```xml
<action name="actionFirmwareManager">...</action>
<action name="actionUpdateFirmware">...</action>
```

**Remove connections:**
```xml
<connection>
    <sender>actionFirmwareManager</sender>
    <signal>triggered()</signal>
    <receiver>MainWindow</receiver>
    <slot>showFirmwareManagerDialog()</slot>
</connection>
<connection>
    <sender>actionUpdateFirmware</sender>
    <signal>triggered()</signal>
    <receiver>MainWindow</receiver>
    <slot>updateFirmware()</slot>
</connection>
```

**Remove slot declarations:**
```xml
<slot>showFirmwareManagerDialog()</slot>
<slot>updateFirmware()</slot>
```

### Modified File: `cmake/SourceFiles.cmake`

Add to `UI_PREFERENCES_SOURCES`:
```cmake
ui/preferences/firmwarepage.cpp ui/preferences/firmwarepage.h
```

---

## Page UI Layout

```
┌─────────────────────────────────────────────────────┐
│ Firmware Management                                 │
│                                                     │
│ Current Firmware Version: v1.2.3                   │
│                                                     │
│ ┌─── Operations ──────────────────────────────────┐ │
│ │ [Check for Updates and Install]                │ │
│ │ [Backup Firmware to .bin File]                 │ │
│ │ [Write Firmware from .bin File]                │ │
│ └────────────────────────────────────────────────┘ │
│                                                     │
│ ┌─── Progress ────────────────────────────────────┐ │
│ │ ████████████████░░░░░░░░░░░░░░░░░░  60%        │ │
│ │ Writing firmware to EEPROM...                   │ │
│ │ [Cancel]                                        │ │
│ └────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

- The Progress group box is **hidden by default** and shown only when an operation starts.
- After an operation finishes, the progress area remains visible for ~2 seconds so the user can read the result, then auto-hides.
- During an operation, all three operation buttons are disabled.

---

## Operation Flows

### Online Update

```
User clicks "Check for Updates and Install"
  │
  ├─ VideoHid::isLatestFirmware()  (network request, may block briefly)
  │
  ├─ FirmwareResult::Latest
  │    → QMessageBox: "Firmware is up to date. Current: vX.Y.Z"
  │    → Done (no progress shown)
  │
  ├─ FirmwareResult::Timeout
  │    → QMessageBox: "Failed to check for updates. Check network."
  │    → Done
  │
  └─ FirmwareResult::Upgradable
       → QMessageBox::question: confirm with version details
       │
       └─ User confirms
            → startOperation(Update)
            → emit firmwareOperationStarted()
               (AdvancedSettingsDialog + MainWindow hide)
            → VideoHid::stop()
            → VideoHid::stopPollingOnly()
            → SerialPortManager::closePort()
            → QThread::msleep(300) + processEvents
            → VideoHid::loadFirmwareToEeprom()
               (FirmwareOperationManager emits progress / writeCompleted)
            │
            ├─ Success
            │    → progressBar 100%
            │    → QMessageBox: "Completed. Restart application."
            │    → emit firmwareUpdateCompleted()
            │    → MainWindow calls QApplication::quit()
            │
            └─ Failure
                 → QMessageBox: "Update failed."
                 → finishOperation(false)
                 → emit firmwareOperationFinished()
                    (AdvancedSettingsDialog + MainWindow show)
```

### Backup Firmware

```
User clicks "Backup Firmware to .bin File"
  │
  → QFileDialog::getSaveFileName() → path
  │  (user cancels → return, no operation started)
  │
  → startOperation(Backup)
  → emit firmwareOperationStarted()
     (AdvancedSettingsDialog + MainWindow hide)
  │
  → VideoHid::readFirmwareSize() → size
  → new QThread + new FirmwareReader(videoHid, ADDR_EEPROM, size, path)
  → worker.moveToThread(thread)
  → connect: progress → progressBar, finished → onOperationComplete
  → thread.start()
  │
  ├─ Success
  │    → QMessageBox: "Firmware saved to <path>"
  │    → finishOperation(true)
  │    → emit firmwareOperationFinished()
  │       (AdvancedSettingsDialog + MainWindow show)
  │
  ├─ Failure
  │    → QMessageBox: "Failed to read and save firmware."
  │    → finishOperation(false)
  │    → emit firmwareOperationFinished()
  │
  └─ Cancelled (user clicks Cancel button)
       → thread.requestInterruption()
       → QMessageBox: "Operation cancelled."
       → finishOperation(false)
       → emit firmwareOperationFinished()
```

### Write Firmware from .bin

```
User clicks "Write Firmware from .bin File"
  │
  → QFileDialog::getOpenFileName() → path
  │  (user cancels → return)
  │
  → readBinFileToByteArray(path) → firmware
  │  (empty → QMessageBox error, return)
  │
  → startOperation(Write)
  → emit firmwareOperationStarted()
     (AdvancedSettingsDialog + MainWindow hide)
  │
  → new QThread + new FirmwareWriter(videoHid, ADDR_EEPROM, firmware)
  → worker.moveToThread(thread)
  → connect: progress → progressBar, finished → onOperationComplete
  → thread.start()
  │
  ├─ Success
  │    → progressBar 100%
  │    → QMessageBox: "Completed. Restart application."
  │    → emit firmwareUpdateCompleted()
  │    → MainWindow calls QApplication::quit()
  │
  ├─ Failure
  │    → QMessageBox: "Failed to write firmware."
  │    → finishOperation(false)
  │    → emit firmwareOperationFinished()
  │
  └─ Cancelled
       → thread.requestInterruption()
       → QMessageBox: "Operation cancelled."
       → finishOperation(false)
       → emit firmwareOperationFinished()
```

---

## Error Handling

- **Network timeout** during version check: Show warning message box, do not start any operation.
- **File read failure** (backup path not writable, bin file unreadable): Show critical message box, do not start operation.
- **Operation failure** (EEPROM read/write error): Show critical message box, restore UI, show windows again.
- **Operation cancelled** (user clicks Cancel): Request thread interruption, show warning message box, restore UI.
- **Exception during update process**: Catch `std::exception` and `...`, show critical message box, restore window.

All error paths call `finishOperation(false)` to restore button states and emit `firmwareOperationFinished()`.

---

## Files Deleted (After Migration)

The following files are no longer needed after the migration and should be deleted:

| File | Reason |
|---|---|
| `ui/advance/firmwareupdatedialog.h` | Functionality moved to `FirmwarePage` |
| `ui/advance/firmwareupdatedialog.cpp` | Functionality moved to `FirmwarePage` |
| `ui/advance/firmwaremanagerdialog.h` | Functionality moved to `FirmwarePage` |
| `ui/advance/firmwaremanagerdialog.cpp` | Functionality moved to `FirmwarePage` |

Also remove from `cmake/SourceFiles.cmake`:
- `ui/advance/firmwareupdatedialog.cpp` / `.h`
- `ui/advance/firmwaremanagerdialog.cpp` / `.h`

---

## Testing Checklist

- [ ] Advanced > Settings opens dialog with two sidebar items: Firmware, MCP
- [ ] Firmware page shows correct current firmware version
- [ ] "Check for Updates" with latest firmware → shows "up to date" message
- [ ] "Check for Updates" with older firmware → shows confirmation, proceeds to update on confirm
- [ ] "Check for Updates" with no network → shows timeout error
- [ ] "Backup Firmware" → file dialog opens, saves .bin file, shows progress, shows success message
- [ ] "Write Firmware from .bin" → file dialog opens, writes firmware, shows progress, exits app on success
- [ ] During any operation, other operation buttons are disabled
- [ ] Cancel button interrupts the operation
- [ ] After operation completes, windows are restored (on failure) or app exits (on success)
- [ ] Advanced menu no longer shows Firmware Manager or Firmware Update items
- [ ] MCP page still works correctly (apply settings, OK, Cancel)
