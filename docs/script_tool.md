# Script Tool — Commands & Examples

This document describes the scripting subset supported by the Script Tool (Autohotkey-format scripts, *.ahk) in the Openterface QT app. The Script Tool accepts simple AutoHotkey-style lines and executes a limited command set (see implementation links below).

## Overview
- Script files: Autohotkey format, extension `*.ahk`.
- Editing: the Script Tool provides an editor and Save/Run buttons (see UI: "Autohotkey Script Tool").
- Limitations: only a subset of AutoHotkey functionality is supported — the app parses and executes the commands listed below. For exact behaviour and key mappings, see the implementation in the repository.

## Supported Commands

- Sleep <milliseconds>
  - Pause execution for the given number of milliseconds.
  - Example: `Sleep 1000` (pause 1 second)

- Send <keys>
  - Send keystrokes to the target.
  - Modifier prefixes: `^` = Ctrl, `+` = Shift, `!` = Alt, `#` = Win
  - Common special keys use braces (typical AutoHotkey style): `{Enter}`, `{Tab}`, `{Space}`, etc.
  - Example: `Send ^c` (Ctrl+C), `Send Hello{Enter}`

- Click [x, y] [, Button] [, Count]
  - Simulate mouse clicks. If coordinates provided, they specify the click position on the target screen.
  - Button can be `Left` (default) or `Right`. Count defaults to 1.
  - Examples: `Click` (left click at current position)
              `Click 100,200` (left click at x=100,y=200)
              `Click 100,200, Right, 2` (two right-clicks at the coordinate)

- SetCapsLockState On|Off|Toggle
  - Set or toggle the Caps Lock state on the target.
  - Example: `SetCapsLockState On`

- SetNumLockState On|Off|Toggle
  - Example: `SetNumLockState Toggle`

- SetScrollLockState On|Off|Toggle
  - Example: `SetScrollLockState Off`

- FullScreenCapture "output_path"
  - Capture the full screen of the target and save it to `output_path` on the host.
  - If a path is not provided, the default media directory is used.
  - Example: `FullScreenCapture "C:\Users\Public\Pictures\cap.png"`

- AreaScreenCapture x, y, width, height "output_path"
  - Capture a rectangular area of the target screen and save it to `output_path` on the host.
  - Example: `AreaScreenCapture 10, 20, 640, 480 "/tmp/area.png"`

## Example Script

Sleep 500
; select all and copy
Send ^a
Send ^c
Sleep 200
; move and click absolute position
Click 150,300
Sleep 100
; capture area and full screen
AreaScreenCapture 100,100,800,600 "/tmp/region.png"
FullScreenCapture "/tmp/full.png"

## Notes & Implementation
- The Script Tool is intended to run simple, linear scripts (one command per line).
- Advanced AutoHotkey flow control (functions, hotkeys, loops) is not guaranteed to be supported.
- Key and modifier mappings are implemented in `scripts/KeyboardMouse.h` and the sending logic in `scripts/KeyboardMouse.cpp`.

See also:
- [doc/feature.md](doc/feature.md) — feature overview and brief supported-commands list.
- [scripts/KeyboardMouse.h](scripts/KeyboardMouse.h)
- [scripts/KeyboardMouse.cpp](scripts/KeyboardMouse.cpp)

If you want, I can add a short cheatsheet in the app's help or expand this doc with a full list of supported special keys (e.g. `{Enter}`, `{Tab}`, `{Esc}`, `{Up}`, `{Down}`, etc.) by extracting them from the implementation.
