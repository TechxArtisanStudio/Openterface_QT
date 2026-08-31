Factory Reset Refactor
======================

Summary
-------
- The factory reset logic has been extracted from `SerialPortManager` into a new component `FactoryResetManager`.
- This centralizes the RTS-based reset and V1.9.1 command-based reset behaviors and makes compatibility handling explicit and testable.

Compatibility rules
-------------------
- CH32V208: RTS-based reset only (115200). Commands for V1.9.1 may not be supported.
- CH9329: Supports RTS-based reset and supports V1.9.1 command-based reset at both baudrates. The code will attempt current baudrate then fallback to the alternative baudrate.
- Unknown chips: safe fallback to CH9329-style attempts and RTS where available.

Design notes
------------
- `FactoryResetManager` exposes async and sync methods (for diagnostics) and emits `factoryReset(bool)` and `factoryResetCompleted(bool)` signals.
- `SerialPortManager` forwards the signals (for backwards compatibility) and delegates reset tasks to `FactoryResetManager`.
- The extraction simplifies unit testing and future strategy implementations (e.g., per-chip strategy classes).

How to test
-----------
- Manual: Use UI or CLI commands that trigger factory resets for supported devices and confirm device reconnects and reports expected info.
- Automated: Consider adding QtTest-based unit tests that mock `SerialPortManager` behaviour and validate the manager's responses to simulated serial port operations.

Notes
-----
- The public API of `SerialPortManager` is unchanged: callers still use the same methods and signals, so existing code remains compatible.
