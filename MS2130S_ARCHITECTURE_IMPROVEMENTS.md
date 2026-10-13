# MS2130S Architecture Improvements — Cooperative Cancellation + RAII Exclusive Access

## Overview

This document describes architectural improvements to the MS2130S firmware flashing pipeline. The changes replace dangerous `terminate()` calls with **cooperative cancellation** and encapsulate exclusive device access management in an **RAII guard class** (`DeviceAccessGuard`).

## Problem Statement

The original implementation had the following issues:

1. **Three `terminate()` call sites**: `stop()`, `stopPollingOnly()`, `writeEeprom()`, and `writeFirmware()` all used `QThread::terminate()` as a fallback when the thread didn't exit within a timeout.
2. **Deadlock risk**: `terminate()` kills a thread immediately, potentially while it holds a mutex, is mid-USB I/O, or is in the middle of a write — leading to deadlocks, data corruption, or unrecoverable device states.
3. **Manual thread lifecycle management**: `writeEeprom()` manually stopped and restarted the polling thread with inline code spread across ~20 lines, mixing thread management with firmware write logic.

## Solution

### 1. `DeviceAccessGuard` RAII Class

**New files:**
- `video/deviceaccessguard.h`
- `video/deviceaccessguard.cpp`

**Purpose:** Encapsulates the "acquire exclusive device access" pattern. On construction, it sets the flash-in-progress flag and cooperatively stops the polling thread. On destruction, it automatically clears the flag and cleans up the thread.

**Usage:**
```cpp
bool VideoHid::writeEeprom(quint16 address, const QByteArray &data,
                           std::function<void(int)> chunkCallback)
{
    // RAII: acquire exclusive access on construction
    DeviceAccessGuard guard(*this);
    if (!guard.isExclusive()) {
        return false;  // Failed to acquire exclusive access
    }
    // ... perform firmware write ...
}  // guard destructor automatically releases exclusive access
```

**Key features:**
- Sets `m_flashInProgress` flag to suppress background register reads from the polling thread and any concurrent QtConcurrent operations.
- Cooperatively stops the polling thread via its existing `m_running` atomic flag (no `terminate()`).
- Waits up to 5 seconds for graceful shutdown.
- Automatically cleans up on destruction via RAII — no manual cleanup needed, even on early returns or exceptions.

### 2. Cooperative Cancellation (Removing All `terminate()` Calls)

#### `video/videohid.cpp`

- **`stop()`**: Removed `terminate()` fallback. Increased wait timeout from 2s to 5s. If the thread doesn't exit within 5s, a warning is logged but the thread is not force-killed.
- **`stopPollingOnly()`**: Removed `terminate()` fallback. Increased wait timeout from 3s to 5s. Same graceful-degradation strategy.

#### `video/videohid_eeprom.cpp`

- **`writeEeprom()`**: Replaced ~20 lines of manual polling thread stop/restart logic with a single `DeviceAccessGuard` instance. The guard's destructor handles cleanup automatically.

#### `video/videohidchip.cpp`

- **`Ms2130sChip::writeFirmware()`**: Removed `terminate()` on the timeout wrapper thread. If `m_transport->open()` times out after 5 seconds, the thread is allowed to continue running in the background (it will eventually complete or be reclaimed by the OS). `requestInterruption()` is called to signal the thread to stop.

### 3. Other Changes

#### `video/videohid.h`
- Added `friend class DeviceAccessGuard;` to allow the guard to access private members (`m_flashInProgress`, `m_pollingThread`).

#### `cmake/SourceFiles.cmake`
- Added `video/deviceaccessguard.cpp` and `video/deviceaccessguard.h` to `VIDEO_SOURCES`.

## Key Design Decisions

### Why Does `PollingThread` Support Cooperative Cancellation?

`PollingThread::run()` already checks `m_running` (an `std::atomic_bool`) every 100ms during its sleep loop:

```cpp
void VideoHid::PollingThread::run() {
    while (m_running) {
        m_owner->pollDeviceStatus();
        // Interruptible sleep in 100ms chunks
        int remainingSleep = m_intervalMs;
        const int sleepChunk = 100;
        while (m_running && remainingSleep > 0) {
            int actualSleep = qMin(sleepChunk, remainingSleep);
            QThread::msleep(actualSleep);
            remainingSleep -= actualSleep;
        }
    }
}
```

When `stop()` sets `m_running = false`, the thread exits within 100ms at worst.

### Why Not Use `terminate()`?

1. **Mutex deadlock**: If the thread holds a `QMutex` when terminated, that mutex is never released — all other threads waiting on it will deadlock.
2. **USB I/O corruption**: Terminating a thread mid-USB transfer can leave the HID device in an inconsistent state, requiring a physical replug.
3. **Data corruption**: If the thread is writing to shared data structures, termination can leave them partially updated.
4. **Resource leaks**: The thread may hold file handles, network connections, or other OS resources that won't be freed.

### Timeout Handling Strategy

If a thread doesn't exit within 5 seconds:
- A warning is logged indicating a possible USB I/O deadlock.
- The thread is **not** force-terminated — it is allowed to continue running (or leak).
- The user is advised to replug the device.
- This is a deliberate tradeoff: a leaked thread is far less damaging than a corrupted mutex or a bricked device.

## Change Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `video/deviceaccessguard.h` | New | RAII guard class declaration |
| `video/deviceaccessguard.cpp` | New | RAII guard class implementation |
| `video/videohid.h` | Modified | Added `DeviceAccessGuard` friend declaration |
| `video/videohid.cpp` | Modified | Removed `terminate()` from `stop()` and `stopPollingOnly()` |
| `video/videohid_eeprom.cpp` | Modified | Replaced manual thread management with `DeviceAccessGuard` in `writeEeprom()` |
| `video/videohidchip.cpp` | Modified | Removed `terminate()` from `writeFirmware()` timeout path |
| `cmake/SourceFiles.cmake` | Modified | Added new files to build system |

**Statistics:**
- New files: 2 (~120 lines)
- Modified files: 5
- Lines removed: ~35 (manual thread management)
- Lines added: ~155 (DeviceAccessGuard + improved thread stop logic)
- Net change: +120 lines

## Testing Recommendations

### Test 1: Normal-Mode Firmware Flash
1. Connect MS2130S in normal mode (VID:PID 345F:2132)
2. Click "Update from Remote"
3. Verify logs contain:
   - `DeviceAccessGuard: Acquiring exclusive device access`
   - `DeviceAccessGuard: Exclusive access acquired successfully`
   - `DeviceAccessGuard: Releasing exclusive device access`
4. Confirm firmware flash succeeds

### Test 2: Recovery-Mode Firmware Flash
1. Connect MS2130S in recovery mode (VID:PID 345F:0001)
2. Click "Update from Remote"
3. Verify logs show:
   - Polling thread stopped cooperatively
   - `writeFirmware()` executes normally
   - No "forcing termination" messages
4. Confirm firmware flash succeeds

### Test 3: Timeout Scenario
1. Disconnect the device mid-flash
2. Verify logs show:
   - Timeout message without `terminate()` calls
   - Clear error messages
3. Confirm application does not crash or deadlock

### Test 4: Regression Testing
1. Test MS2109S device — confirm read/write still works
2. Test MS2109 device — confirm read/write still works
3. Test hotplug/unplug — confirm device discovery still works
4. Confirm no performance degradation in polling

## Expected Outcome

| Aspect | Before | After |
|--------|--------|-------|
| `terminate()` calls | 3 sites | **0 sites** |
| Thread management | Manual, scattered | **RAII, centralized** |
| Wait timeout | 2–3 seconds | **5 seconds** |
| Code complexity | High (inline in writeEeprom) | **Low (single guard object)** |
| Crash risk on timeout | High (dangling mutexes) | **Low (graceful degradation)** |

## Future Improvements

These are not required for the current fix but could be considered as the project grows:

1. **State Machine Pattern**: If device states multiply (e.g., "upgrading", "error recovery", "DFU mode"), a formal state machine would make transitions explicit and testable.
2. **Command Pattern**: If undo/redo or batched operations are needed, encapsulating each operation as a command object would simplify the design.
3. **Service Layer Decomposition**: `VideoHid` is a god object managing polling, chip detection, EEPROM I/O, firmware operations, and hotplug — all in one class. Splitting it into focused services would improve testability, but is a large refactor best done incrementally.

## References

- Original issue document: `MS2130S_RECOVERY_MODE_ISSUES.md`
- Initial fix summary: `MS2130S_FIX_SUMMARY.md`
- Qt thread documentation: https://doc.qt.io/qt-6/qthread.html
- RAII pattern: https://en.cppreference.com/w/cpp/language/raii

---

**Date:** 2026-10-13
**Implemented by:** Claude Code
**Build status:** Passed (162MB executable)
