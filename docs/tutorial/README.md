# Tutorial — Openterface Mini-KVM

> Control your target computer remotely with keyboard, video, and mouse over a single USB connection.

## What is Openterface Mini-KVM?

Openterface Mini-KVM is a **compact hardware KVM (Keyboard-Video-Mouse) switch** — a dedicated USB device that sits between your host computer and a target computer. It captures the target's HDMI video output and relays your keyboard and mouse input through HID emulation, all over a single USB cable. No network required.

This is the **Qt desktop application** (C++/Qt6) that runs on your host machine and provides the user interface for controlling the device.

### Host vs Target Terminology

Throughout this tutorial, you'll see two terms used consistently:

- **Host** — The computer running this application (your workstation)
- **Target** — The computer you want to control remotely (server, Raspberry Pi, embedded device)

### Physical Device

The Openterface Mini-KVM hardware device has:
- **USB connection to host** — Plugs into your computer, providing power and data
- **HDMI input from target** — Captures the target's video output
- **USB switchable port** — Connects your physical keyboard/mouse, switchable between host and target control
- **Internal chips** — CH9329 or CH32V208 (serial/HID controller), MS2109/MS2109S/MS2130S (HDMI capture)

---

## Tutorial Series

| # | Title | Audience | Status |
|---|-------|----------|--------|
| [01 — Getting Started](01-getting-started.md) | Installation, first launch, basic KVM control | Beginners | Complete |
| [02 — Core Features](02-core-features.md) | Video, audio, serial console, scripts, recording | Beginners → Intermediate | Complete |
| [03 — Advanced Features](03-advanced-features.md) | EDID, firmware, diagnostics, multi-device, preferences | Intermediate → Expert | Complete |
| [04 — Architecture Deep Dive](04-architecture.md) | Module structure, design patterns, data flow | Experts / Contributors | Complete |
| [05 — Development Guide](05-development-guide.md) | Build, test, contribute, extend | Contributors | Complete |
| [06 — Platform Guides](06-platform-guides.md) | Linux, Windows, Raspberry Pi, NixOS specifics | Platform users | Complete |
| [07 — Troubleshooting](07-troubleshooting.md) | Common problems and solutions | Everyone | Complete |

---

## Quick Links to Existing Docs

- [Build from Source](../BUILD.md) — Detailed compilation instructions
- [Features List](../feature.md) — Complete feature checklist
- [Preferences Pages](../preferences_pages_documentation.md) — Settings reference
- [TCP Protocol](../tcp_protocol.md) — Remote control protocol spec
- [Supported Resolutions](../resolutions.md) — Video resolution tables
- [Keyboard Layouts](../keyboard_layout_adding.md) — Adding new layouts
- [Multi-Language](../multi_language.md) — Translation workflow
- [Script Tool](../script_tool.md) — Script automation guide
- [EDID Display Settings](../edid_display_settings_update.md) — EDID management
- [USB3 PortChain](../USB3_Companion_PortChain_Implementation.md) — USB3 device topology
- [TCP Server Guide](../tcp_server_guide.md) — Server usage and examples

Contributions: if you'd like to expand any section, open a PR updating the matching file under `docs/tutorial/`.
