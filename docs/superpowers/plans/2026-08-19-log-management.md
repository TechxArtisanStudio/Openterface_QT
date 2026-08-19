# Log Management Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix LogHandler bugs, split overloaded logging categories in core subsystems, and provide a tree-view UI for fine-grained runtime log control.

**Architecture:** A single combined message handler replaces the current dual-handler approach. An `OPF_LOGGING_CATEGORY` macro wraps `Q_LOGGING_CATEGORY` and auto-registers categories in a singleton registry. The LogPage UI replaces 7 checkboxes with a `QTreeView` that reads from the registry and generates `QLoggingCategory::setFilterRules()`.

**Tech Stack:** Qt 6 (QLoggingCategory, QTreeView, QStandardItemModel, QSettings), C++17

**Spec:** `docs/superpowers/specs/2026-08-19-log-management-design.md`

## Global Constraints

- No C++ test framework exists — verification is build-compile + runtime
- Dual build system: both `cmake/SourceFiles.cmake` and `openterfaceQT.pro` must be updated for new files
- New `log/` directory at project root for registry files
- All existing `Q_LOGGING_CATEGORY` variable names and category strings remain unchanged where not being split
- `opf.core.chip.write` and `opf.host.mouse.buttons` sub-categories are dropped (zero log calls exist)

---

### Task 1: Fix LogHandler Bugs

**Files:**
- Modify: `ui/loghandler.h`
- Modify: `ui/loghandler.cpp`

**Interfaces:**
- Consumes: `QSettings("Techxartisan", "Openterface")` for log path/settings
- Produces: `LogHandler::install()`, `LogHandler::setFileLoggingEnabled(bool, QString)`, `LogHandler::getLogFilePath()`, `LogHandler::shutdown()`

- [ ] **Step 1: Rewrite `ui/loghandler.h`**

Replace the entire header with the new API:

```cpp
#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QThread>
#include <QDebug>
#include <QCoreApplication>

class LogHandler : public QObject
{
    Q_OBJECT

public:
    explicit LogHandler(QObject *parent = nullptr);
    static LogHandler& instance();

    /// Install the combined message handler (call once at startup)
    void install();

    /// Enable or disable file logging at runtime
    void setFileLoggingEnabled(bool enabled, const QString& path = QString());

    /// Get the effective log file path
    QString getLogFilePath() const;

    /// Flush and close log file (call before app exit)
    void shutdown();

    // Keep for backward compatibility during transition
    void enableLogStore();

    static void combinedHandler(QtMsgType type,
                                const QMessageLogContext &context,
                                const QString &msg);

private:
    static QString formatMessage(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &msg);

    static bool s_fileLoggingEnabled;
    static QFile s_logFile;
    static QMutex s_mutex;
    static bool s_installed;
};

#endif // LOGHANDLER_H
```

- [ ] **Step 2: Rewrite `ui/loghandler.cpp`**

Replace the entire implementation:

```cpp
#include "loghandler.h"
#include "globalsetting.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Static member initialization
bool LogHandler::s_fileLoggingEnabled = false;
QFile LogHandler::s_logFile;
QMutex LogHandler::s_mutex;
bool LogHandler::s_installed = false;

LogHandler::LogHandler(QObject *parent)
    : QObject(parent)
{
}

LogHandler& LogHandler::instance()
{
    static LogHandler instance;
    return instance;
}

void LogHandler::install()
{
    QMutexLocker locker(&s_mutex);
    if (s_installed) return;

    // Read settings to determine file logging state
    QSettings settings("Techxartisan", "Openterface");
    s_fileLoggingEnabled = settings.value("log/storeLog", false).toBool();

    if (s_fileLoggingEnabled) {
        QString path = getLogFilePath();
        s_logFile.setFileName(path);
        if (!s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            s_fileLoggingEnabled = false;
        }
    }

    qInstallMessageHandler(combinedHandler);
    s_installed = true;
}

void LogHandler::setFileLoggingEnabled(bool enabled, const QString& path)
{
    QMutexLocker locker(&s_mutex);

    // Close existing file if open
    if (s_logFile.isOpen()) {
        s_logFile.flush();
        s_logFile.close();
    }

    s_fileLoggingEnabled = enabled;

    if (enabled) {
        QString filePath = path.isEmpty() ? getLogFilePath() : path;
        s_logFile.setFileName(filePath);
        if (!s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            s_fileLoggingEnabled = false;
        }
    }
}

QString LogHandler::getLogFilePath() const
{
    QSettings settings("Techxartisan", "Openterface");
    QString userPath = settings.value("log/logFilePath").toString();

    if (!userPath.isEmpty()) {
        return userPath;
    }

    // Default: QStandardPaths::AppDataLocation
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty()) {
        dataDir = QCoreApplication::applicationDirPath();
    }
    QDir().mkpath(dataDir);
    return dataDir + "/openterface.log";
}

void LogHandler::shutdown()
{
    QMutexLocker locker(&s_mutex);
    if (s_logFile.isOpen()) {
        s_logFile.flush();
        s_logFile.close();
    }
    s_fileLoggingEnabled = false;
}

void LogHandler::enableLogStore()
{
    // Backward-compatible entry point: re-read settings and update file logging
    QSettings settings("Techxartisan", "Openterface");
    bool storeLog = settings.value("log/storeLog", false).toBool();
    QString logFilePath = settings.value("log/logFilePath").toString();
    setFileLoggingEnabled(storeLog, logFilePath);

    // Ensure handler is installed
    if (!s_installed) {
        qInstallMessageHandler(combinedHandler);
        s_installed = true;
    }
}

QString LogHandler::formatMessage(QtMsgType type,
                                   const QMessageLogContext &context,
                                   const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");

    QThread *currentThread = QThread::currentThread();
    QString threadName;
    if (!currentThread->objectName().isEmpty()) {
        threadName = currentThread->objectName();
    } else if (currentThread == QCoreApplication::instance()->thread()) {
        threadName = "MainThread";
    } else {
        threadName = QString::number(reinterpret_cast<quintptr>(currentThread->currentThreadId()));
    }

    const char* categoryName = context.category;
    QString category = categoryName ? QString(categoryName) : "default";

    QString level;
    switch (type) {
        case QtDebugMsg:    level = "D"; break;
        case QtInfoMsg:     level = "I"; break;
        case QtWarningMsg:  level = "W"; break;
        case QtCriticalMsg: level = "C"; break;
        case QtFatalMsg:    level = "F"; break;
        default:            level = "U"; break;
    }

    return QString("[%1][%2][%3][%4] %5")
        .arg(timestamp, level, threadName, category, msg);
}

void LogHandler::combinedHandler(QtMsgType type,
                                  const QMessageLogContext &context,
                                  const QString &msg)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);

    QString formatted = formatMessage(type, context, msg);

    // 1. Console output (always)
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(formatted.utf16()));
    OutputDebugStringW(L"\n");
#else
    fprintf(stderr, "%s\n", formatted.toUtf8().constData());
    fflush(stderr);
#endif

    // 2. File output (if enabled)
    if (s_fileLoggingEnabled && s_logFile.isOpen()) {
        QTextStream stream(&s_logFile);
        stream << formatted << "\n";
        stream.flush();
    }

    // 3. Fatal → abort
    if (type == QtFatalMsg) {
        std::abort();
    }
}
```

- [ ] **Step 3: Build and verify compilation**

Run: `cmake --build build --config Debug 2>&1 | head -50`
Expected: Compiles without errors. The `enableLogStore()` backward-compatible method ensures existing call sites in `logpage.cpp` still work.

- [ ] **Step 4: Commit**

```bash
git add ui/loghandler.h ui/loghandler.cpp
git commit -m "fix(log): rewrite LogHandler - remove hardcoded path, fix file I/O perf

- Remove hardcoded C:/openterface_debug.log from customMessageHandler
- File opened once (static QFile), not per-message
- Single timestamp computation (was duplicated)
- Combined handler: console + file in one handler, toggle via flag
- Default log path via QStandardPaths::AppDataLocation
- Added shutdown() for clean file close on exit
- Backward-compatible enableLogStore() preserved"
```

---

### Task 2: Create Category Registry and OPF_LOGGING_CATEGORY Macro

**Files:**
- Create: `log/logcategoryregistry.h`
- Create: `log/logcategoryregistry.cpp`
- Create: `log/opflogging.h`
- Modify: `cmake/SourceFiles.cmake`
- Modify: `openterfaceQT.pro`

**Interfaces:**
- Consumes: Nothing (self-contained)
- Produces: `LogCategoryRegistry::instance()`, `LogCategoryRegistry::registerCategory(QString)`, `LogCategoryRegistry::allCategories()`, `OPF_LOGGING_CATEGORY(varname, name)` macro

- [ ] **Step 1: Create `log/logcategoryregistry.h`**

```cpp
#ifndef LOGCATEGORYREGISTRY_H
#define LOGCATEGORYREGISTRY_H

#include <QString>
#include <QStringList>
#include <QMutex>

class LogCategoryRegistry
{
public:
    static LogCategoryRegistry& instance();

    void registerCategory(const QString& name);
    QStringList allCategories() const;
    QStringList categoriesByGroup(const QString& group) const;

private:
    LogCategoryRegistry() = default;
    QStringList m_categories;
    QMutex m_mutex;
};

#endif // LOGCATEGORYREGISTRY_H
```

- [ ] **Step 2: Create `log/logcategoryregistry.cpp`**

```cpp
#include "logcategoryregistry.h"
#include <QMutexLocker>
#include <algorithm>

LogCategoryRegistry& LogCategoryRegistry::instance()
{
    static LogCategoryRegistry instance;
    return instance;
}

void LogCategoryRegistry::registerCategory(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    if (!m_categories.contains(name)) {
        m_categories.append(name);
        m_categories.sort();
    }
}

QStringList LogCategoryRegistry::allCategories() const
{
    QMutexLocker locker(&m_mutex);
    return m_categories;
}

QStringList LogCategoryRegistry::categoriesByGroup(const QString& group) const
{
    QMutexLocker locker(&m_mutex);
    QStringList result;
    for (const QString& cat : m_categories) {
        // group is like "opf.core.serial", match "opf.core.serial" and "opf.core.serial.*"
        if (cat == group || cat.startsWith(group + ".")) {
            result.append(cat);
        }
    }
    return result;
}
```

- [ ] **Step 3: Create `log/opflogging.h`**

```cpp
#ifndef OPFLOGGING_H
#define OPFLOGGING_H

#include <QLoggingCategory>
#include "logcategoryregistry.h"

// OPF_LOGGING_CATEGORY wraps Q_LOGGING_CATEGORY and additionally registers
// the category name in LogCategoryRegistry at static initialization time.
//
// Usage (drop-in replacement for Q_LOGGING_CATEGORY):
//   OPF_LOGGING_CATEGORY(log_serial_tx, "opf.core.serial.tx")
//
// Then use normally:
//   qCDebug(log_serial_tx) << "TX data:" << data.toHex();

#define OPF_LOGGING_CATEGORY(varname, name) \
    Q_LOGGING_CATEGORY(varname, name) \
    static struct OpfCatReg_##varname { \
        OpfCatReg_##varname() { \
            LogCategoryRegistry::instance().registerCategory(QStringLiteral(name)); \
        } \
    } s_opfCatReg_##varname;

#endif // OPFLOGGING_H
```

- [ ] **Step 4: Add new files to build system**

In `cmake/SourceFiles.cmake`, find the `COMMON_SOURCES` or create a new `LOG_SOURCES` set. Add:

```cmake
set(LOG_SOURCES
    log/logcategoryregistry.cpp
)
set(LOG_HEADERS
    log/logcategoryregistry.h
    log/opflogging.h
)
```

Then include `LOG_SOURCES` and `LOG_HEADERS` in the final `SOURCE_FILES` aggregation.

In `openterfaceQT.pro`, add to SOURCES and HEADERS:

```
SOURCES += log/logcategoryregistry.cpp
HEADERS += log/logcategoryregistry.h log/opflogging.h
```

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Compiles cleanly. No existing code references these files yet, so no behavioral change.

- [ ] **Step 6: Commit**

```bash
git add log/ cmake/SourceFiles.cmake openterfaceQT.pro
git commit -m "feat(log): add LogCategoryRegistry and OPF_LOGGING_CATEGORY macro

Auto-discovery mechanism: OPF_LOGGING_CATEGORY wraps Q_LOGGING_CATEGORY
and registers the category name in a singleton registry at startup.
This enables the tree-view UI to discover all available categories."
```

---

### Task 3: Serial Category Split

**Files:**
- Modify: `serial/SerialPortManager.cpp`

**Interfaces:**
- Consumes: `OPF_LOGGING_CATEGORY` macro from `log/opflogging.h`
- Produces: 9 new logging category variables replacing the single `log_core_serial`

This task replaces the single `log_core_serial` (category `opf.core.serial`) with 9 focused sub-categories. There are 388 log call sites to reassign.

- [ ] **Step 1: Replace the category declaration**

In `serial/SerialPortManager.cpp`, find:
```cpp
Q_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")
```

Replace with:
```cpp
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_core_serial_tx, "opf.core.serial.tx")
OPF_LOGGING_CATEGORY(log_core_serial_rx, "opf.core.serial.rx")
OPF_LOGGING_CATEGORY(log_core_serial_cmd, "opf.core.serial.cmd")
OPF_LOGGING_CATEGORY(log_core_serial_conn, "opf.core.serial.conn")
OPF_LOGGING_CATEGORY(log_core_serial_watchdog, "opf.core.serial.watchdog")
OPF_LOGGING_CATEGORY(log_core_serial_hotplug, "opf.core.serial.hotplug")
OPF_LOGGING_CATEGORY(log_core_serial_config, "opf.core.serial.config")
OPF_LOGGING_CATEGORY(log_core_serial_lockkeys, "opf.core.serial.lockkeys")
OPF_LOGGING_CATEGORY(log_core_serial_usbswitch, "opf.core.serial.usbswitch")
```

- [ ] **Step 2: Replace all log call sites**

Perform a systematic replacement of `log_core_serial` with the appropriate sub-category variable. The mapping by line ranges (from the exploration data):

**→ `log_core_serial_tx` (9 calls):** Lines 132, 2101, 2138, 2160, 2182, 2200, 2210, 2224, 2246

**→ `log_core_serial_rx` (15 calls):** Lines 1873, 1896, 1902, 1909, 1926, 1930, 1935, 1942, 1952, 1964, 1974, 1981, 1992, 2000, 2104

**→ `log_core_serial_cmd` (21 calls):** Lines 156, 197, 226, 411, 428, 1788, 1836, 1843, 1855, 1861, 1866, 1869, 3218, 3229, 3240, 3319, 3388, 3410, 3416, 3423, 3441

**→ `log_core_serial_conn` (83 calls):** Lines 66, 123, 146, 380, 384, 396, 456, 461, 465, 468, 475, 477, 485, 495, 505, 510, 520, 568, 572, 577, 591, 595, 602, 608, 614, 621, 626, 656, 661, 669, 709, 719, 756, 777, 975, 986, 991, 992, 1007, 1010, 1018, 1026, 1044, 1048, 1298, 1302, 1303, 1308, 1366, 1378, 1385, 1387, 1418, 1424, 1431, 1452, 1455, 1460, 1464, 1473, 1479, 1499, 1518, 1532, 1537, 1543, 1558, 1563, 1575, 1581, 1586, 1677, 1680, 1692, 1710, 1717, 1722, 1725, 1728, 1738, 1760, 1764, 1766, 3341, 3345

**→ `log_core_serial_watchdog` (45 calls):** Lines 120, 161, 165, 1066, 1077, 1079, 1091, 1093, 1099, 1114, 1123, 1132, 1158, 1169, 2833, 2842, 2851, 2884, 2895, 2903, 2917, 2922, 2931, 2936, 2942, 2963, 2979, 2998, 3004, 3008, 3017, 3098, 3107, 3108, 3115, 3124, 3128, 3132, 3138, 3149, 3168, 3172, 3179, 3189, 3196

**→ `log_core_serial_hotplug` (22 calls):** Lines 218, 268, 270, 295, 302, 304, 315, 323, 332, 334, 343, 346, 377, 1404, 1408, 1409, 1613, 2804, 2807, 2813, 2818, 3882

**→ `log_core_serial_config` (132 calls):** Lines 170, 181, 184, 186, 191, 239, 242, 649, 727, 745, 799, 835, 838, 858, 864, 874, 884, 886, 896, 902, 911, 921, 927, 933, 938, 961, 1144, 1147, 1185, 1203, 1210, 1222, 1239, 1246, 1255, 1266, 1279, 1290, 2066, 2072, 2078, 2085, 2092, 2095, 2109, 2112, 2116, 2117, 2536, 2542, 2549, 2559, 2567, 2572, 2580, 2588, 2589, 2593, 2621, 2628, 2639, 2652, 2654, 2672, 2679, 2684, 2697, 2704, 2707, 2715, 2721, 2726, 2745, 2750, 2753, 2761, 2768, 2783, 2787, 3088, 3090, 3464, 3467, 3471, 3475, 3483, 3500, 3505, 3517, 3522, 3527, 3542, 3545, 3549, 3555, 3568, 3573, 3589, 3594, 3604, 3617, 3633, 3637, 3745, 3746, 3747, 3751, 3752, 3755, 3758, 3764, 3766, 3768, 3770, 3772, 3775, 3776, 3778, 3779, 3783, 3786, 3788, 3790, 3791, 3794, 3796, 3799, 3809, 3823, 3835, 3861, 3873

**→ `log_core_serial_lockkeys` (6 calls):** Lines 3673, 3675, 3702, 3704, 3731, 3733

**→ `log_core_serial_usbswitch` (55 calls):** Lines 1060, 2295, 2299, 2303, 2325, 2328, 2334, 2340, 2345, 2354, 2357, 2363, 2369, 2374, 2381, 2382, 2383, 2384, 2388, 2394, 2397, 2400, 2405, 2410, 2430, 2434, 2437, 2439, 2440, 2441, 2443, 2450, 2451, 2452, 2453, 2467, 2470, 2471, 2480, 2483, 2486, 2490, 2506, 2509, 2511, 2513, 2515, 2517, 2520, 2522, 2524

The replacement is mechanical: for each `qCDebug(log_core_serial)`, `qCWarning(log_core_serial)`, `qCInfo(log_core_serial)`, or `qCCritical(log_core_serial)` call, change the variable name to the appropriate sub-category based on the line number mapping above.

Use search-and-replace in your editor, working through the file from top to bottom. Since line numbers shift as you edit, work by finding each `log_core_serial` reference and replacing it based on the semantic content of the log message.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -50`
Expected: Compiles without errors. All `log_core_serial` references replaced.

- [ ] **Step 4: Runtime verification**

Launch the app. In console output, verify that serial-related logs now show the sub-category names:
- `[opf.core.serial.tx]` for TX hex dumps
- `[opf.core.serial.conn]` for connection state changes
- `[opf.core.serial.config]` for chip detection / baudrate

- [ ] **Step 5: Commit**

```bash
git add serial/SerialPortManager.cpp
git commit -m "refactor(log): split opf.core.serial into 9 sub-categories

opf.core.serial had 388 call sites covering TX, RX, connection, watchdog,
hotplug, config, lock keys, USB switch, and command coordination.
Now each concern has its own filterable category."
```

---

### Task 4: Keyboard Category Split

**Files:**
- Modify: `target/KeyboardManager.cpp`

**Interfaces:**
- Consumes: `OPF_LOGGING_CATEGORY` macro from `log/opflogging.h`
- Produces: 5 new logging category variables replacing `log_keyboard`

- [ ] **Step 1: Replace the category declaration**

In `target/KeyboardManager.cpp`, find:
```cpp
Q_LOGGING_CATEGORY(log_keyboard, "opf.host.keyboard")
```

Replace with:
```cpp
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_host_kb_mapping, "opf.host.keyboard.mapping")
OPF_LOGGING_CATEGORY(log_host_kb_modifiers, "opf.host.keyboard.modifiers")
OPF_LOGGING_CATEGORY(log_host_kb_ime, "opf.host.keyboard.ime")
OPF_LOGGING_CATEGORY(log_host_kb_special, "opf.host.keyboard.special")
OPF_LOGGING_CATEGORY(log_host_kb_state, "opf.host.keyboard.state")
```

- [ ] **Step 2: Replace all log call sites (84 calls)**

Mapping by sub-category:

**→ `log_host_kb_mapping` (34 calls):** Lines 134, 152, 163, 165, 169, 254, 258, 262, 266, 270, 274, 278, 282, 286, 290, 294, 298, 302, 306, 310, 314, 325, 333, 334, 335, 594, 937, 941, 944, 951, 956, 957, 958, 960

**→ `log_host_kb_modifiers` (32 calls):** Lines 174, 338, 344, 350, 356, 362, 368, 374, 380, 386, 392, 395, 406, 411, 416, 421, 426, 431, 436, 441, 446, 451, 477, 482, 488, 493, 500, 513, 518, 523, 528, 534

**→ `log_host_kb_ime` (4 calls):** Lines 194, 199, 204, 214

**→ `log_host_kb_special` (5 calls):** Lines 129, 733, 890, 899, 927

**→ `log_host_kb_state` (9 calls):** Lines 121, 157, 609, 613, 617, 621, 623, 802, 827

Replace each `qCDebug(log_keyboard)`, `qCInfo(log_keyboard)`, `qCWarning(log_keyboard)` with the appropriate sub-category variable. Also fix line 827 which uses `qDebug(log_keyboard)` — change to `qCDebug(log_host_kb_state)`.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add target/KeyboardManager.cpp
git commit -m "refactor(log): split opf.host.keyboard into 5 sub-categories

Mapping (34), modifiers (32), state (9), special (5), IME (4).
Also fixed line 827 using raw qDebug instead of qCDebug."
```

---

### Task 5: Mouse Category Split

**Files:**
- Modify: `target/MouseManager.cpp`

**Interfaces:**
- Consumes: `OPF_LOGGING_CATEGORY` macro from `log/opflogging.h`
- Produces: 3 new logging category variables replacing `log_core_mouse`

- [ ] **Step 1: Replace the category declaration**

In `target/MouseManager.cpp`, find:
```cpp
Q_LOGGING_CATEGORY(log_core_mouse, "opf.host.mouse")
```

Replace with:
```cpp
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_mouse_abs, "opf.host.mouse.absolute")
OPF_LOGGING_CATEGORY(log_mouse_rel, "opf.host.mouse.relative")
OPF_LOGGING_CATEGORY(log_mouse_scroll, "opf.host.mouse.scroll")
```

Note: `opf.host.mouse.buttons` is dropped — no dedicated button log calls exist.

- [ ] **Step 2: Replace all log call sites (4 calls)**

| Line | Old Variable | New Variable | Message |
|------|-------------|-------------|---------|
| 30 | `log_core_mouse` | `log_mouse_abs` | `"MouseManager created"` (lifecycle → assign to absolute as primary mode) |
| 59 | `log_core_mouse` | `log_mouse_abs` | `"mappedWheelMovement:"` (inside handleAbsoluteMouseAction) |
| 86 | `log_core_mouse` | `log_mouse_rel` | `"handleRelativeMouseAction"` |
| 98 | `log_core_mouse` | `log_mouse_rel` | `"mappedWheelMovement:"` (inside handleRelativeMouseAction) |
| 136 | `log_core_mouse` | `log_mouse_scroll` | `"Scroll wheel - direction:..."` |

Also fix `MouseManager.h` line 140: change `qDebug() << "Mouse manager reset"` to `qCDebug(log_mouse_abs) << "Mouse manager reset"`. Add `#include "log/opflogging.h"` to the header if needed, or move the log to the .cpp file.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -20`
Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add target/MouseManager.cpp target/MouseManager.h
git commit -m "refactor(log): split opf.host.mouse into 3 sub-categories

absolute, relative, scroll. Buttons sub-category dropped (zero calls)."
```

---

### Task 6: HID/Chip Category Split

**Files:**
- Modify: `video/videohid.cpp`
- Modify: `video/videohidchip.cpp`

**Interfaces:**
- Consumes: `OPF_LOGGING_CATEGORY` macro from `log/opflogging.h`
- Produces: 7 new logging category variables replacing `log_host_hid` and `log_chip`

- [ ] **Step 1: Replace category declarations in `video/videohid.cpp`**

Find:
```cpp
Q_LOGGING_CATEGORY(log_host_hid, "opf.core.hid")
```

Replace with:
```cpp
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_hid_detect, "opf.core.hid.detect")
OPF_LOGGING_CATEGORY(log_hid_poll, "opf.core.hid.poll")
OPF_LOGGING_CATEGORY(log_hid_firmware, "opf.core.hid.firmware")
OPF_LOGGING_CATEGORY(log_hid_device, "opf.core.hid.device")
```

`opf.core.hid.device` covers the ~65 lifecycle/transaction/hotplug/device-switch calls that don't fit into detect/poll/firmware.

- [ ] **Step 2: Replace all `log_host_hid` call sites in `video/videohid.cpp`**

**→ `log_hid_detect` (10 calls):** Lines 115, 116, 122, 137, 148–149, 341, 472, 476, 479, 777

**→ `log_hid_poll` (7 calls):** Lines 80, 86, 98, 278, 291–293, 296, 310

**→ `log_hid_firmware` (11 calls):** Lines 185, 792, 795, 800, 803, 810, 815, 819–821, 829, 832, 833

**→ `log_hid_device` (all remaining ~65 calls):** Everything else — lifecycle (start/stop), transaction management, hotplug handling, device switching, port chain finding, etc. Lines: 155, 159, 176–177, 189, 195, 200, 206, 210, 217, 222, 235, 239, 241, 244, 247, 249, 254, 257, 259, 263, 321, 328, 336, 347, 353, 363, 378–380, 404–405, 448, 458, 484, 487, 496, 503, 520, 527, 534, 538, 544, 552, 554, 561, 565, 571, 575, 580, 589, 593, 598, 606, 618, 626, 630, 641, 651, 655, 662–663, 669, 679–680, 685–686, 703, 707, 713, 717, 721, 728–729, 734, 750, 753, 773

- [ ] **Step 3: Replace category declarations in `video/videohidchip.cpp`**

Find:
```cpp
Q_LOGGING_CATEGORY(log_chip, "opf.core.chip")
```

Replace with:
```cpp
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_chip_read, "opf.core.chip.read")
OPF_LOGGING_CATEGORY(log_chip_flash, "opf.core.chip.flash")
OPF_LOGGING_CATEGORY(log_chip_gpio, "opf.core.chip.gpio")
```

Note: `opf.core.chip.write` is dropped — chip-level writes are silent.

- [ ] **Step 4: Replace all `log_chip` call sites in `video/videohidchip.cpp`**

**→ `log_chip_read` (9 calls):** Lines 29–30, 35–37, 46, 51, 77, 164, 199, 207, 311

**→ `log_chip_flash` (~60 calls):** Lines 371, 373, 380, 387, 392, 400, 401, 404, 408, 543–544, 547–548, 558, 565, 570, 580–581, 596, 601, 614–615, 636, 645, 667–668, 672, 686, 689, 697, 722, 748, 754–755, 761, 768–769, 773–774, 783, 797, 803, 807, 811, 822, 826–827, 833, 839, 843–844, 855, 861, 869, 871, 899, 900, 902, 905–906, 909–910, 937–938, 948, 950–951, 955, 961, 971, 977, 979, 985–987, 995

**→ `log_chip_gpio` (20 calls):** Lines 416, 421, 433–434, 440–441, 446–447, 457–458, 465–466, 471–472, 479, 480, 481, 482, 484, 486, 488, 489, 491, 494, 501, 506, 527

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Compiles without errors.

- [ ] **Step 6: Commit**

```bash
git add video/videohid.cpp video/videohidchip.cpp
git commit -m "refactor(log): split opf.core.hid and opf.core.chip into sub-categories

hid: detect, poll, firmware, device (lifecycle/hotplug/txn)
chip: read, flash, gpio. Write sub-category dropped (zero calls)."
```

---

### Task 7: Tree-View LogPage UI

**Files:**
- Modify: `ui/preferences/logpage.h`
- Modify: `ui/preferences/logpage.cpp`

**Interfaces:**
- Consumes: `LogCategoryRegistry::allCategories()`, `LogHandler::setFileLoggingEnabled()`
- Produces: Tree-view UI with per-category level control, settings persistence via QSettings

- [ ] **Step 1: Rewrite `ui/preferences/logpage.h`**

Remove the 7 checkbox member variables and replace with tree view members:

```cpp
#ifndef LOGPAGE_H
#define LOGPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QTreeView>
#include <QStandardItemModel>
#include <QComboBox>
#include "fontstyle.h"
#include "preferencepagebase.h"

class LogPage : public PreferencePageBase
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);
    void setupUI();
    void browseLogPath();
    void initLogSettings();
    void applySettings() override;
    void captureSnapshot() override;
    bool valuesMatchSnapshot() const override;
    void revertToSnapshot() override;

signals:
    void ScreenSaverInhibitedChanged(bool inhibited);
    void hideKeyboardInputChanged(bool hide);
    void floatingWindowEnabledChanged(bool enabled);
    void floatingWindowOpacityChanged(double opacity);
    void systemKeyBlockerToggled(bool enabled);

private:
    void populateCategoryTree();
    QString generateFilterRules() const;
    void saveCategorySettings() const;
    void restoreCategorySettings();

    // Log file controls
    QCheckBox *storeLogCheckBox;
    QLineEdit *logFilePathLineEdit;
    QPushButton *browseButton;

    // Category tree view
    QTreeView *categoryTreeView;
    QStandardItemModel *categoryModel;

    // Quick-set buttons
    QPushButton *allDebugButton;
    QPushButton *allInfoButton;
    QPushButton *allWarningButton;

    // Other settings (unchanged from original)
    QCheckBox *screenSaverCheckBox;
    QCheckBox *hideKeyboardInputCheckBox;
    QCheckBox *floatingWindowCheckBox;
    QSlider *floatingWindowOpacitySlider;
    QLabel *floatingWindowOpacityLabel;
    QCheckBox *systemKeyBlockerCheckBox;

    // Snapshot for revert
    bool m_snap_storeLog;
    QString m_snap_logFilePath;
    bool m_snap_screenSaver;
    bool m_snap_hideKeyboardInput;
    bool m_snap_floatingWindow;
    int m_snap_floatingWindowOpacity;
    bool m_snap_systemKeyBlocker;
    // Tree state snapshot: map of category → {enabled, level}
    QMap<QString, QPair<bool, QString>> m_snap_categoryStates;
};

#endif // LOGPAGE_H
```

- [ ] **Step 2: Rewrite `ui/preferences/logpage.cpp`**

Key changes in the implementation:

**`setupUI()`:**
- Remove all 7 checkbox creations (coreCheckBox, serialCheckBox, etc.)
- Create `QTreeView` with `QStandardItemModel`
- Create 3 quick-set buttons (All Debug, All Info, All Warning)
- Connect quick-set buttons to set all categories at once

**`populateCategoryTree()`:**
- Read categories from `LogCategoryRegistry::instance().allCategories()`
- Filter to core subsystem prefixes:
  - `opf.core.serial.*`, `opf.serial.*` → Serial group
  - `opf.host.keyboard.*`, `opf.host.layouts` → Keyboard group
  - `opf.host.mouse.*` → Mouse group
  - `opf.core.hid.*`, `opf.core.chip.*`, `opf.host.*_transport` → HID/Chip group
- Build tree with group parent nodes and category children
- Each child row has: checkbox (column 0) + category name + level combo (column 1)

**`generateFilterRules()`:**
- Iterate tree items
- For each checked category, generate `category.level=true`
- For unchecked, generate `category=false`
- Return as newline-separated string for `QLoggingCategory::setFilterRules()`

**`applySettings()`:**
- Call `generateFilterRules()` and apply via `QLoggingCategory::setFilterRules()`
- Call `saveCategorySettings()` to persist to QSettings
- Call `LogHandler::setFileLoggingEnabled()` for file logging toggle
- Keep existing signal emissions for other settings

**`initLogSettings()`:**
- Uncomment the call in constructor (was `// initLogSettings()`)
- Call `populateCategoryTree()`
- Call `restoreCategorySettings()` to load saved levels
- Apply initial filter rules

**`captureSnapshot()` / `revertToSnapshot()` / `valuesMatchSnapshot()`:**
- Update to capture/restore tree state instead of 7 checkboxes

**`saveCategorySettings()`:**
```cpp
void LogPage::saveCategorySettings() const
{
    QSettings settings("Techxartisan", "Openterface");
    // Iterate all items in the model
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem* group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem* nameItem = group->child(c, 0);
            QStandardItem* levelItem = group->child(c, 1);
            QString category = nameItem->text();
            bool enabled = nameItem->checkState() == Qt::Checked;
            QString level = levelItem->text();
            settings.setValue(QString("log/category/%1/enabled").arg(category), enabled);
            settings.setValue(QString("log/category/%1/level").arg(category), level);
        }
    }
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -50`
Expected: Compiles without errors.

- [ ] **Step 4: Runtime verification**

1. Launch the app
2. Open Preferences → Log page
3. Verify: Tree view shows Serial, Keyboard, Mouse, HID/Chip groups with individual categories
4. Toggle a category off → verify its logs disappear from console
5. Change a category to "Debug" → verify debug logs appear
6. Click "All Debug" → verify everything set to Debug
7. Click Apply → restart app → verify settings persisted
8. Enable file logging → verify log file created at default path

- [ ] **Step 5: Commit**

```bash
git add ui/preferences/logpage.h ui/preferences/logpage.cpp
git commit -m "feat(log): tree-view UI for fine-grained log category control

Replaces 7 checkboxes with QTreeView showing all auto-discovered
categories. Per-category level control (Off/Debug/Info/Warning/Critical).
Quick-set buttons for bulk changes. Settings persisted via QSettings."
```

---

### Task 8: Update Remaining Files to Use OPF_LOGGING_CATEGORY

**Files:**
- Modify: `target/KeyboardLayouts.cpp` — `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY`
- Modify: `serial/SerialStateManager.cpp` — `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY`
- Modify: `serial/SerialStatistics.cpp` — `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY`
- Modify: `video/transport/WindowsHIDTransport.cpp` — `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY`
- Modify: `video/transport/LinuxHIDTransport.cpp` — `Q_LOGGING_CATEGORY` → `OPF_LOGGING_CATEGORY`

This task is a simple drop-in replacement so these categories also appear in the auto-discovery registry.

- [ ] **Step 1: Replace in each file**

For each of the 5 files listed above:

1. Add `#include "log/opflogging.h"` (replace the existing `#include <QLoggingCategory>` if present)
2. Change `Q_LOGGING_CATEGORY(varname, "category.string")` to `OPF_LOGGING_CATEGORY(varname, "category.string")`

The variable name and category string remain unchanged. Only the macro name changes.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Compiles without errors.

- [ ] **Step 3: Runtime verification**

Launch app → open Log settings → verify that `opf.host.layouts`, `opf.serial.state`, `opf.serial.statistics`, `opf.host.win_transport`, `opf.host.linux_transport` appear in the tree view.

- [ ] **Step 4: Commit**

```bash
git add target/KeyboardLayouts.cpp serial/SerialStateManager.cpp serial/SerialStatistics.cpp video/transport/WindowsHIDTransport.cpp video/transport/LinuxHIDTransport.cpp
git commit -m "refactor(log): migrate remaining core categories to OPF_LOGGING_CATEGORY

These categories now auto-register in LogCategoryRegistry for tree-view
discovery. No behavioral change."
```

---

## Task Dependency Graph

```
Task 1 (LogHandler fix)
    │
    ▼
Task 2 (Registry + macro)
    │
    ├──► Task 3 (Serial split)
    ├──► Task 4 (Keyboard split)
    ├──► Task 5 (Mouse split)
    ├──► Task 6 (HID/Chip split)
    │        │
    │        ▼
    │    Task 8 (Migrate remaining files)
    │        │
    ▼        ▼
Task 7 (Tree-view UI) — depends on Task 2 (registry) and Tasks 3-6+8 (categories to display)
```

Tasks 3-6 are independent of each other and can be done in parallel.
Task 7 must come after Task 2 (needs registry) and ideally after Tasks 3-6+8 (so all categories are registered).
