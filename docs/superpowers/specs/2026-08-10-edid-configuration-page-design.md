# EDID Configuration Page — Design Spec

**Date:** 2026-08-10
**Status:** Draft
**Feature:** Integrate display name (EDID) editing into Advanced Settings as a new page, renamed to "EDID Configuration"

---

## 1. Motivation

Currently, changing the display name of the KVM's virtual monitor (which is actually done by modifying the video chip's EDID data) is only available through a standalone dialog (`UpdateDisplaySettingsDialog`) launched from a menu action (`actionRenameDisplay`).

This change:
1. Integrates the feature into the existing **Advanced Settings** dialog as a new page.
2. Renames the feature to **"EDID Configuration"** to accurately reflect what it does (modify the chip's EDID to change the monitor name shown by the OS).
3. **Scopes the feature down to Display Name only** — the Serial Number and Resolution Table sections of the old dialog are removed.

## 2. Current State

### Architecture today

```
MainWindow
├── actionRenameDisplay (menu) → opens standalone UpdateDisplaySettingsDialog
└── actionAdvancedSettings (menu) → opens AdvancedSettingsDialog
    ├── Video Firmware (FirmwarePage)
    ├── Control Chip Firmware (ControlChipFirmwarePage)
    └── MCP (McpPage)
```

The `UpdateDisplaySettingsDialog` (in `ui/advance/updatedisplaysettingsdialog.h/cpp`) currently supports:
- Display Name update (via EDID modification)
- Serial Number update (via EDID modification)
- Resolution enable/disable (via EDID CEA-861 extension block)

Its flow:
1. Read firmware from EEPROM (via `FirmwareOperationManager` → `FirmwareReader`, async).
2. Parse the EDID block, populate current display name / serial number.
3. On Apply: stop HID polling, read firmware, modify EDID via `EDIDUtils`, write back via `FirmwareOperationManager` → `FirmwareWriter`.
4. On success: show message, quit the app; user must disconnect/reconnect the device.

### Existing reusable building blocks

| Component | Location | Role |
|-----------|----------|------|
| `EDIDUtils` | `ui/advance/edid/edidutils.h/cpp` | Static helpers: `updateEDIDDisplayName()`, `parseEDIDDescriptors()`, checksum calc, etc. |
| `FirmwareOperationManager` | `video/firmwareoperationmanager.h/cpp` | Async firmware read/write with progress signals |
| `FirmwareReader` / `FirmwareWriter` | `video/firmwarereader.*` / `video/firmwarewriter.*` | Worker threads performing the actual I/O |
| `AdvancedSettingsDialog` | `ui/preferences/advancedsettingsdialog.h/cpp` | Left-tree + stacked-page dialog hosting pages |

## 3. Target State

```
MainWindow
└── actionAdvancedSettings (menu) → opens AdvancedSettingsDialog
    ├── Video Firmware (FirmwarePage)
    ├── Control Chip Firmware (ControlChipFirmwarePage)
    ├── MCP (McpPage)
    └── EDID Configuration (EdidConfigPage)  ← NEW
```

### Removals

- Menu action `actionRenameDisplay` (text "Update Display Settings") — removed from `ui/mainwindow.ui`.
- Slot `MainWindow::showUpdateDisplaySettingsDialog()` — removed from `ui/mainwindow.h/cpp`.
- Member `UpdateDisplaySettingsDialog *updateDisplaySettingsDialog` — removed from `ui/mainwindow.h`.
- Serial Number UI/logic and Resolution Table UI/logic — NOT carried over to the new page.
- The old `UpdateDisplaySettingsDialog` class is **left in place** but becomes unreachable from the UI. Cleanup of the old files is out of scope for this spec (kept to minimize diff risk; a later cleanup task can delete it).

## 4. New Page: EdidConfigPage

### File locations

- `ui/preferences/edidconfigpage.h`
- `ui/preferences/edidconfigpage.cpp`
- Registered in `ui/preferences/advancedsettingsdialog.h/cpp`

### Class shape

```cpp
class EdidConfigPage : public QWidget
{
    Q_OBJECT

public:
    explicit EdidConfigPage(QWidget *parent = nullptr);
    ~EdidConfigPage();

private slots:
    void onDisplayNameCheckChanged(bool checked);
    void onApplyButtonClicked();
    void onCancelReadingClicked();

    // Firmware operation signals
    void onFirmwareReadProgress(int percent);
    void onFirmwareReadFinished(bool success);
    void onFirmwareReadError(const QString &errorMessage);
    void onFirmwareWriteFinished(bool success);

private:
    // UI
    QGroupBox *displayNameGroup;
    QLabel *currentNameLabel;          // shows current name from EDID (read-only)
    QCheckBox *displayNameCheckBox;    // "Update display name"
    QLineEdit *displayNameLineEdit;    // new name, max 13 ASCII chars
    QPushButton *applyButton;          // page-internal Apply
    QGroupBox *progressGroup;          // hidden by default
    QProgressBar *progressBar;
    QLabel *progressLabel;
    QPushButton *cancelReadingButton;
    QLabel *infoLabel;                 // note about app exit + reconnection

    // Firmware operation
    class FirmwareOperationManager *firmwareOperationManager;
    QString m_tempFirmwarePath;
    QByteArray m_pendingFirmwareData;
    bool m_operationFinished;
    bool m_updateMode;

    // Helpers (ported from UpdateDisplaySettingsDialog)
    void setupUI();
    void ensureFirmwareOperationManager();
    void loadCurrentEDIDSettings();
    bool processFirmwareData(const QByteArray &firmwareData);
    bool updateDisplayName(const QString &newName);
    bool processAndWriteFirmware();
    void enableApplyButton();
    void setControlsEnabled(bool enabled);
    void setProgressState(bool active, const QString &labelText);
    bool validateAsciiInput(const QString &text, int maxLen, const QString &fieldName, QString &errorMessage) const;
    void shutdownFirmwareOperation();
    void restartPollingDelayed(const QString &reason);
    void showErrorAndRestart(const QString &title, const QString &message, const QString &reason);
    void stopAllDevices();

    // UI construction
    void buildDisplayNameSection();
    void buildProgressSection();
    void buildButtonSection();
    void connectUiSignals();
};
```

### UI layout

```
┌─────────────────────────────────────────────────┐
│  EDID Configuration                             │
│                                                 │
│  ┌─ Display Name ────────────────────────────┐  │
│  │                                           │  │
│  │  Current name:  [________________]        │  │
│  │                                           │  │
│  │  ☐ Update display name                    │  │
│  │  New name:      [________________]        │  │
│  │                 (max 13 characters)       │  │
│  │                                           │  │
│  └───────────────────────────────────────────┘  │
│                                                 │
│  ┌─ Progress ───────────────────────────────┐   │
│  │  [████████████████░░░░░░░░] 65%          │   │
│  │  Reading firmware...                     │   │
│  │                                          │   │
│  │                          [Cancel Reading]│   │
│  └───────────────────────────────────────────┘   │
│                                                 │
│  [Apply]                                        │
│                                                 │
│  Note: After updating, the application will     │
│  exit. Please disconnect and reconnect the      │
│  device to apply changes.                       │
│                                                 │
└─────────────────────────────────────────────────┘
```

### Interaction details

- **On page construction:** `loadCurrentEDIDSettings()` starts a firmware read (as the old dialog did) to populate the current name. Checkbox unchecked, line edit disabled, Apply disabled.
- **Checkbox toggled ON:** line edit enabled, Apply enabled (if input valid).
- **Checkbox toggled OFF:** line edit disabled and cleared, Apply disabled.
- **Apply clicked:**
  1. Validate input (ASCII only, max 13 chars — matches EDID monitor name descriptor length).
  2. Confirm dialog: "The following changes will be applied: … Do you want to continue?"
  3. `updateDisplayName()`: stop polling → read firmware → process → write back.
- **On write success:** `QMessageBox::information` ("Display settings updated successfully! The application will now exit. Please disconnect and reconnect the entire device to apply the changes.") → `QApplication::quit()`.
- **On any failure:** error message, restart polling after delay, return to IDLE (controls re-enabled).

## 5. AdvancedSettingsDialog Integration

### Header (`advancedsettingsdialog.h`)

```cpp
#include "edidconfigpage.h"
// member:
EdidConfigPage *edidConfigPage;
```

### `createSettingTree()`

```cpp
QStringList names = {tr("Video Firmware"), tr("Control Chip Firmware"), tr("MCP"), tr("EDID Configuration")};
```

### `createPages()`

```cpp
addScrollablePage(edidConfigPage);   // appended after mcpPage
```

### `changePage()`

```cpp
else if (itemText == tr("EDID Configuration")) {
    newPageIndex = 3;
}
// bottom OK/Apply/Cancel button bar stays hidden for this page:
buttonWidget->setVisible(newPageIndex == 2);   // unchanged: only MCP shows the bar
```

The page owns its own Apply button (bottom-right, inside the page), so the dialog-level button bar remains hidden on the EDID page — same pattern as the Firmware pages.

### Constructor ordering

`edidConfigPage` is created alongside the other pages. Its constructor calls `loadCurrentEDIDSettings()` (fires a firmware read), which is consistent with `FirmwarePage` doing async work in its constructor. The dialog's `buttonWidget->setVisible(false)` default on first page is unaffected.

## 6. Error Handling & Edge Cases

| Scenario | Handling |
|----------|----------|
| Firmware read fails | Error message → `restartPollingDelayed()` → IDLE |
| EDID parse fails | Error message → IDLE; placeholder text on name field |
| Firmware write fails | Error message → `restartPollingDelayed()` → IDLE |
| Invalid input (non-ASCII, >13 chars) | Warning dialog; no update starts |
| Apply with nothing checked | Warning "No updates selected" (defensive; Apply is disabled anyway) |
| User cancels reading | Stop read → restart polling → IDLE |
| Operation in progress + page switch | Controls disabled during operation; switching away mid-write is acceptable since write is a worker thread — but to be safe, the dialog hides the bottom bar anyway. Page navigation is NOT blocked (matches old dialog's modal behavior loosely); the write continues and quits the app on success. |
| Duplicate Apply clicks | Apply disabled during operation |
| Current name read fails | Placeholder "Failed to read firmware — enter display name"; user can still enter a new name |

## 7. Code Porting Notes (from `UpdateDisplaySettingsDialog`)

Port the following methods verbatim/adapted (they contain the working firmware + EDID logic):

- `ensureFirmwareOperationManager()` — adapt signal connections to new slot names; keep write-finished success path (`m_operationFinished = true; setProgressState(false,...); QMessageBox + QApplication::quit()`).
- `loadCurrentEDIDSettings()` — keep (reads firmware to populate current name).
- `processFirmwareData()` — strip serial-number handling; keep display name population.
- `updateDisplaySettings()` → rename `updateDisplayName()` — strip serial handling; keep stop-polling + read flow.
- `processAndWriteFirmware()` — unchanged.
- `validateAsciiInput()`, `restartPollingDelayed()`, `showErrorAndRestart()`, `stopAllDevices()`, `shutdownFirmwareOperation()`, `setProgressState()`, `enableUpdateButton()` → `enableApplyButton()`, `setDialogControlsEnabled()` → `setControlsEnabled()`.
- `parseEdidBlock()` — used by `processFirmwareData()`; keep as private helper.

Not ported: serial number methods, resolution table methods, `buildSettingsSection()` (replaced by `buildDisplayNameSection()`), backup button, `closeEvent`/`accept`/`reject` overrides, `hideMainWindow()` (page lives inside the dialog; the old dialog hid the main window — no longer needed since the app quits on success).

## 8. Out of Scope

- Deleting the old `UpdateDisplaySettingsDialog` files (left in place, unreachable).
- Serial Number / Resolution editing.
- Localization/translation file updates (`config/languages/*.ts`) — will be regenerated by the project's normal translation workflow.
- Any EDID parsing algorithm changes (pure UI/flow integration).

## 9. Verification

Manual test checklist (no automated UI tests exist for this area):

1. Open Advanced Settings → "EDID Configuration" page appears in the left tree.
2. Current name loads from device (or placeholder on failure).
3. Enter invalid name (non-ASCII, >13 chars) → warning, no update.
4. Check "Update display name", enter valid name, Apply → confirmation dialog → progress bar → success message → app exits.
5. Reconnect device; OS shows new monitor name.
6. Cancel reading path works and polling resumes.
7. Old menu action "Update Display Settings" is gone from the File menu.
8. Advanced Settings other pages (Firmware, Control Chip Firmware, MCP) still work.

Build: `cmake --build build` (project standard build).
