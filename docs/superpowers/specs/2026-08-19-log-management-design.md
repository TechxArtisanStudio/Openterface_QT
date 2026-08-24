# Log Management Redesign

**Date:** 2026-08-19
**Status:** Draft
**Scope:** Core subsystems (Serial, Keyboard, Mouse, HID/Chip)

## Overview

Redesign the logging infrastructure to fix existing bugs, improve granularity for the heaviest logging subsystems, and provide a tree-view UI for fine-grained runtime control.

## Problem Statement

The current log management has several issues:

1. **Hardcoded debug path** — `loghandler.cpp:120` writes to `C:/openterface_debug.log` unconditionally
2. **File I/O per message** — `fileMessageHandler` opens and closes the log file for every single log line
3. **Duplicate timestamp** — `timestamp` variable computed at line 76 but unused; a second timestamp computed at line 111
4. **Coarse category control** — 7 checkboxes in LogPage cannot precisely control 70+ QLoggingCategory instances
5. **Unclean handler switching** — `enableLogStore()` installs a new handler without closing the previous file, causing handle leaks
6. **Overloaded categories** — `opf.core.serial` has 100+ call sites covering TX hex dumps, command coordination, connection state, watchdog, hotplug, and more — impossible to filter meaningfully

## Goals

1. Fix all LogHandler bugs (hardcoded path, file I/O performance, timestamp duplication, handler switching)
2. Split overloaded categories in core subsystems into fine-grained sub-categories
3. Replace the 7-checkbox LogPage with a tree-view showing all categories with per-category level control
4. Auto-discover categories via a custom macro wrapper
5. **Non-goals:** Log rotation (deferred), network logging, in-app log viewer

## Architecture

### Component Overview

```
┌───────────────────────────────────────────────────────┐
│                  Application Code                      │
│  OPF_LOGGING_CATEGORY(log_serial_tx, "opf.core.serial.tx") │
│  qCDebug(log_serial_tx) << "TX: 57 ab 00 08 ..."    │
└───────────────────────┬───────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────┐
│            QLoggingCategory (filtering)                │
│  Decides: should this message be output?               │
│  Controlled by: QLoggingCategory::setFilterRules()     │
└───────────────────────┬───────────────────────────────┘
                        │ (enabled)
                        ▼
┌───────────────────────────────────────────────────────┐
│         LogHandler (output, single combined handler)   │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │  Console    │  │  File        │  │  (future:    │ │
│  │  Output     │  │  Output      │  │   buffer)    │ │
│  └─────────────┘  └──────────────┘  └──────────────┘ │
└───────────────────────────────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────┐
│         LogCategoryRegistry (auto-discovery)           │
│  Collects all category names at startup                │
│  → feeds LogPage tree view                             │
└───────────────────────────────────────────────────────┘
```

### LogHandler Fix

**File:** `ui/loghandler.h`, `ui/loghandler.cpp`

#### Changes

| Issue | Current | Fixed |
|-------|---------|-------|
| Hardcoded path | `C:/openterface_debug.log` in `customMessageHandler` | Remove debug file entirely |
| File I/O per message | `QFile` opened/closed per log line | `static QFile` opened once, closed on app exit |
| Duplicate timestamp | Computed at line 76, unused; recomputed at line 111 | Single timestamp, single format |
| Handler switching | `enableLogStore()` swaps handlers, leaks file handle | Single `combinedHandler` that writes to both console and file; toggle file output via flag |
| Default log path | Empty string if not configured | `QStandardPaths::writableLocation(AppDataLocation) + "/openterface.log"` |

#### New API

```cpp
class LogHandler : public QObject {
    Q_OBJECT
public:
    static LogHandler& instance();

    /// Install the combined handler (console + optional file)
    void install();

    /// Enable/disable file logging at runtime
    void setFileLoggingEnabled(bool enabled, const QString& path = "");

    /// Get the effective log file path (user-configured or default)
    QString getLogFilePath() const;

    /// Flush and close the log file (call before app exit)
    void shutdown();

private:
    static void combinedHandler(QtMsgType type,
                                const QMessageLogContext &context,
                                const QString &msg);

    static QString formatMessage(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &msg);

    static bool s_fileLoggingEnabled;
    static QFile s_logFile;
    static QMutex s_mutex;
};
```

### Category Registry (Auto-Discovery)

**New files:** `log/logcategoryregistry.h`, `log/opflogging.h`

#### Mechanism

A custom macro `OPF_LOGGING_CATEGORY` wraps `Q_LOGGING_CATEGORY` and additionally registers the category name in a singleton registry at static initialization time:

```cpp
// log/opflogging.h
#include <QLoggingCategory>
#include "logcategoryregistry.h"

#define OPF_LOGGING_CATEGORY(varname, name) \
    Q_LOGGING_CATEGORY(varname, name) \
    static struct OpfCatReg_##varname { \
        OpfCatReg_##varname() { \
            LogCategoryRegistry::instance().registerCategory(name); \
        } \
    } s_opfCatReg_##varname;
```

The registry is a simple singleton that collects category names:

```cpp
// log/logcategoryregistry.h
class LogCategoryRegistry {
public:
    static LogCategoryRegistry& instance();
    void registerCategory(const QString& name);
    QStringList allCategories() const;
    QStringList categoriesByGroup(const QString& group) const;

private:
    QStringList m_categories;
};
```

#### Usage

Replace `Q_LOGGING_CATEGORY` with `OPF_LOGGING_CATEGORY` in core subsystem files:

```cpp
// Before:
Q_LOGGING_CATEGORY(log_core_serial_tx, "opf.core.serial.tx")

// After:
OPF_LOGGING_CATEGORY(log_core_serial_tx, "opf.core.serial.tx")
```

The variable name and category string remain unchanged — only the macro name changes.

### Core Subsystem Category Splits

#### Serial: `opf.core.serial` → 9 sub-categories

| New Category | Variable | What Gets Logged | Current Call Sites |
|---|---|---|---|
| `opf.core.serial.tx` | `log_core_serial_tx` | TX hex dump: `"TX (COM3@115200bps): 57 ab 00 08 ..."` | ~15 |
| `opf.core.serial.rx` | `log_core_serial_rx` | RX parsing, protocol responses, status | ~20 |
| `opf.core.serial.cmd` | `log_core_serial_cmd` | Command coordination, queue, execution results | ~15 |
| `opf.core.serial.conn` | `log_core_serial_conn` | Connection state transitions | ~10 |
| `opf.core.serial.watchdog` | `log_core_serial_watchdog` | Watchdog triggers, auto-recovery attempts | ~10 |
| `opf.core.serial.hotplug` | `log_core_serial_hotplug` | Hotplug detection, auto-reconnect | ~8 |
| `opf.core.serial.config` | `log_core_serial_config` | Chip detection, baudrate config, factory reset | ~15 |
| `opf.core.serial.lockkeys` | `log_core_serial_lockkeys` | NumLock/CapsLock/ScrollLock state | ~5 |
| `opf.core.serial.usbswitch` | `log_core_serial_usbswitch` | USB host/target switching | ~3 |

Existing `opf.serial.statistics` and `opf.serial.state` are unchanged.

**Files to modify:** `serial/SerialPortManager.cpp` (primary), plus any file that references `log_core_serial`.

#### Keyboard: `opf.host.keyboard` → 5 sub-categories

| New Category | Variable | What Gets Logged | Current Call Sites |
|---|---|---|---|
| `opf.host.keyboard.mapping` | `log_host_kb_mapping` | Key code → scancode mapping results | ~25 |
| `opf.host.keyboard.modifiers` | `log_host_kb_modifiers` | Shift/Ctrl/Alt/Win modifier detection | ~20 |
| `opf.host.keyboard.ime` | `log_host_kb_ime` | IME keys (Muhenkan, Henkan, ZenkakuHankaku) | ~5 |
| `opf.host.keyboard.special` | `log_host_kb_special` | Ctrl+Alt+Del, function keys, compose sequences | ~10 |
| `opf.host.keyboard.state` | `log_host_kb_state` | Key down/up state, combined modifiers | ~15 |

Existing `opf.host.layouts` is unchanged.

**Files to modify:** `target/KeyboardManager.cpp`

#### Mouse: `opf.host.mouse` → 4 sub-categories

| New Category | Variable | What Gets Logged | Current Call Sites |
|---|---|---|---|
| `opf.host.mouse.absolute` | `log_mouse_abs` | Absolute coordinate mode events | ~2 |
| `opf.host.mouse.relative` | `log_mouse_rel` | Relative movement mode events | ~2 |
| `opf.host.mouse.scroll` | `log_mouse_scroll` | Scroll wheel direction and lines | ~1 |
| `opf.host.mouse.buttons` | `log_mouse_buttons` | Button press/release state | ~1 |

**Files to modify:** `target/MouseManager.cpp`

#### HID/Chip: split `opf.core.hid` and `opf.core.chip`

| New Category | Variable | What Gets Logged | Current Call Sites |
|---|---|---|---|
| `opf.core.hid.detect` | `log_hid_detect` | Chip detection from device path | ~5 |
| `opf.core.hid.poll` | `log_hid_poll` | Polling, resolution/FPS/HDMI reads | ~15 |
| `opf.core.hid.firmware` | `log_hid_firmware` | Firmware version check, upgrade status | ~10 |
| `opf.core.chip.read` | `log_chip_read` | Register read operations | ~25 |
| `opf.core.chip.write` | `log_chip_write` | Register write operations | ~15 |
| `opf.core.chip.flash` | `log_chip_flash` | Flash erase/write/verify | ~25 |
| `opf.core.chip.gpio` | `log_chip_gpio` | GPIO init/restore for flash ops | ~5 |

Existing `opf.host.win_transport` and `opf.host.linux_transport` are unchanged.

**Files to modify:** `video/videohid.cpp`, `video/videohidchip.cpp`

### Tree-View Log Page UI

**File:** `ui/preferences/logpage.h`, `ui/preferences/logpage.cpp`

#### Structure

Replace the 7 `QCheckBox` widgets with a `QTreeView` backed by a `QStandardItemModel`.

Each row has:
- **Column 0:** `QCheckBox` (enable/disable) + category name label
- **Column 1:** `QComboBox` (level: Off / Debug / Info / Warning / Critical)

Categories are grouped by subsystem (Serial, Keyboard, Mouse, HID/Chip) as top-level tree nodes, with individual categories as children.

#### Data Source

The tree is populated from `LogCategoryRegistry::allCategories()` filtered to core subsystem prefixes:
- `opf.core.serial.*`, `opf.serial.*` → Serial group
- `opf.host.keyboard.*`, `opf.host.layouts` → Keyboard group
- `opf.host.mouse.*` → Mouse group
- `opf.core.hid.*`, `opf.core.chip.*`, `opf.host.*_transport` → HID/Chip group

#### Filter Rule Generation

On Apply, the tree state is converted to `QLoggingCategory::setFilterRules()`:

```cpp
QString LogPage::generateFilterRules() const {
    QString rules;
    // For each checked category with selected level:
    //   opf.core.serial.tx.debug=true
    //   opf.core.serial.rx.info=true
    //   opf.core.serial.watchdog.warning=true
    // For unchecked categories:
    //   opf.core.serial.config=false
    return rules;
}
```

#### Quick-Set Buttons

Three buttons at the bottom:
- **All Debug** — set every category to Debug level
- **All Info** — set every category to Info level (recommended default)
- **All Warning** — set every category to Warning level (minimal noise)

### Settings Persistence

Category levels are saved to `QSettings`:

```
[log]
category/opf.core.serial.tx/level=Debug
category/opf.core.serial.tx/enabled=true
category/opf.core.serial.rx/level=Info
...
```

On startup, saved levels are restored and applied via `QLoggingCategory::setFilterRules()`.

## File Change Summary

| File | Type | Description |
|------|------|-------------|
| `ui/loghandler.h` | Modify | New API: `install()`, `setFileLoggingEnabled()`, `shutdown()` |
| `ui/loghandler.cpp` | Modify | Fix all bugs, single combined handler |
| `log/logcategoryregistry.h` | **New** | Category registry singleton |
| `log/logcategoryregistry.cpp` | **New** | Registry implementation |
| `log/opflogging.h` | **New** | `OPF_LOGGING_CATEGORY` macro |
| `ui/preferences/logpage.h` | Modify | Remove 7 checkboxes, add QTreeView |
| `ui/preferences/logpage.cpp` | Modify | Tree-view UI, filter rule generation |
| `serial/SerialPortManager.cpp` | Modify | Split `log_core_serial` into 9 categories |
| `target/KeyboardManager.cpp` | Modify | Split `log_keyboard` into 5 categories |
| `target/MouseManager.cpp` | Modify | Split `log_core_mouse` into 4 categories |
| `video/videohid.cpp` | Modify | Split `log_host_hid` into 3 categories |
| `video/videohidchip.cpp` | Modify | Split `log_chip` into 4 categories |
| `target/KeyboardLayouts.cpp` | Modify | `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY` |
| `video/transport/WindowsHIDTransport.cpp` | Modify | `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY` |
| `video/transport/LinuxHIDTransport.cpp` | Modify | `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY` |
| `serial/SerialStateManager.cpp` | Modify | `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY` |
| `serial/SerialStatistics.cpp` | Modify | `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY` |

**Total:** ~17 files modified, 3 files created

## Testing

1. **Build verification** — Project compiles without errors after all category splits
2. **Runtime verification** — Launch app, open Log settings, verify tree view populates with all categories
3. **Filter verification** — Toggle individual categories, verify console output matches selection
4. **File logging** — Enable file logging, verify log file is created at default path, format is correct
5. **Persistence** — Change settings, restart app, verify settings are restored
6. **No regression** — Existing functionality (serial communication, keyboard/mouse input, video) works unchanged

## Future Work (Out of Scope)

- Log rotation (file size limit, archive old files)
- In-app log viewer panel
- Extend tree view to cover non-core subsystems (device, server, UI, backend, scripts)
- Network log forwarding
- Structured log format (JSON) for machine parsing
