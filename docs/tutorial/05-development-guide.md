# Tutorial 05 — Development Guide

This page helps new contributors set up a development environment, build the project, run tests, and contribute code.

## Table of Contents

| Section | Covers |
|---------|--------|
| [Setting Up the Dev Environment](#setting-up-the-dev-environment) | Qt6, system dependencies, IDE setup |
| [Building & Running](#building--running) | CMake workflow, qmake, debug vs release |
| [Testing](#testing) | Current coverage, adding tests |
| [Adding a Keyboard Layout](#adding-a-keyboard-layout) | JSON format, scancode mapping |
| [Adding a Language Translation](#adding-a-language-translation) | .ts files, lupdate/lrelease, Qt Linguist |
| [Adding a Video Resolution](#adding-a-video-resolution) | EDID tables, UI wiring |
| [Contributing Workflow](#contributing-workflow) | Git branching, PRs, code style |
| [Debugging Tips](#debugging-tips) | Logging, serial debug, common pitfalls |

---

## Setting Up the Dev Environment

### Qt6 Installation

The project requires **Qt 6.6.3 or later** (the custom CMake finder enforces this). Install Qt6 via your platform's package manager or the official Qt installer.

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install -y qt6-base-dev qt6-multimedia-dev qt6-serialport-dev \
    qt6-svg-dev qt6-tools-dev qt6-wayland
```

**Linux (Fedora/RHEL):**
```bash
sudo dnf install -y qt6-qtbase-devel qt6-qtmultimedia-devel \
    qt6-qtserialport-devel qt6-qtsvg-devel qt6-qttools-devel
```

**Windows:**
Download the Qt online installer from https://www.qt.io/download-qt-installer-oss. Recommended version: 6.4.3+. Use Qt Maintenance Tool to add QtMultiMedia and QtSerialPort components.

### System Dependencies

Full list of dependencies per platform is documented in [`docs/BUILD.md`](../BUILD.md). Key libraries:

| Library | Purpose | Ubuntu Package |
|---------|---------|---------------|
| FFmpeg 6.1.1+ | Video decoding (FFmpeg backend) | `libavformat-dev`, `libavcodec-dev`, `libavdevice-dev`, etc. |
| libusb-1.0 | WCH ISP flashing (Linux) | `libusb-1.0-0-dev` |
| libudev | USB hotplug detection | `libudev-dev` |
| GStreamer 1.0 | Video decoding (GStreamer backend) | `libgstreamer1.0-dev`, `libgstreamer-plugins-base1.0-dev` |
| TurboJPEG | Fast MJPEG frame decoding | `libturbojpeg0-dev` |
| libva | Hardware acceleration (VAAPI) | `libva-dev` |
| OpenSSL | TLS/network firmware client | `libssl-dev` |

### IDE Setup

**VS Code:**
- Install the C/C++ extension (ms-vscode.cpptools)
- Install the CMake Tools extension (ms-vscode.cmake-tools)
- The project includes `.vscode/` configuration files for IntelliSense

**Qt Creator:**
- Open the project root or `openterfaceQT.pro`
- Qt Creator auto-detects CMake and Qt components
- Use the Kits panel to configure the compiler and Qt version

---

## Building & Running

### CMake Workflow

```bash
mkdir build && cd build

# Ubuntu/Debian (x86_64):
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6

# Fedora/RHEL (x86_64):
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib64/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr

# ARM64 (Raspberry Pi):
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/aarch64-linux-gnu/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib/aarch64-linux-gnu

make -j$(nproc)
```

### Debug vs Release

- **Debug:** `-DCMAKE_BUILD_TYPE=Debug` — includes symbols, no optimization, enables Qt debug assertions
- **Release:** `-DCMAKE_BUILD_TYPE=Release` — LTO and `-Os` optimization on amd64 (disabled on arm64 to avoid segfaults)

### Running from Build Directory

```bash
cd build
./openterfaceQT
```

### qmake Alternative

For environments where CMake is not preferred:

```bash
# Generate translation files first
lrelease openterfaceQT.pro

qmake6 ..
make -j$(nproc)
```

---

## Testing

### Current Test Coverage

The project currently has **limited automated test coverage**. There are no dedicated unit test suites or integration test frameworks configured in the CI pipeline. Testing is primarily manual.

### Running Tests

There is no `ctest` or `make test` target currently defined. To verify a build:

1. Build the project
2. Run the application: `./openterfaceQT`
3. Verify core features: video stream, serial console, keyboard/mouse passthrough, recording

### Adding New Tests

To add automated tests, you would:

1. Create a `tests/` directory
2. Add a `CMakeLists.txt` with `enable_testing()` and `add_executable()` for test binaries
3. Use Qt Test framework (`Qt6::Test`) for unit tests
4. Include the test directory from the root `CMakeLists.txt`

Suggested testing priorities:
- `SerialProtocol` parsing (checksum validation, packet extraction)
- `ChipStrategyFactory` VID/PID detection
- `MultimediaBackendFactory` type detection
- Script interpreter `Lexer` and `Parser` tokenization
- `GlobalSetting` persistence round-trips

---

## Adding a Keyboard Layout

Keyboard layouts are defined as JSON files in [`config/keyboards/`](config/keyboards/).

### JSON Format

Each layout file maps AHK-style key names to HID scancodes. The layout system is managed by `KeyboardLayoutManager` which loads all JSON files from the config directory at startup.

### Steps

1. Create a new JSON file in `config/keyboards/` with your layout name (e.g., `de_qwertz.json`)
2. Map each key to its HID scancode using the format expected by `Keymapping`
3. The layout will be auto-detected at startup when `KeyboardLayoutManager::getInstance().loadLayouts()` is called
4. Users can select the layout from **Preferences → Target Control → Keyboard Layout**

### Testing

- Switch to the new layout in preferences
- Test all modifier keys (Ctrl, Alt, Shift, Win/Command)
- Test special characters and dead keys
- Verify the serial console shows correct key data being sent

---

## Adding a Language Translation

The project uses Qt's standard i18n workflow with `.ts` translation files.

### Files

- Translation source files: [`config/languages/*.ts`](config/languages/)
- CMake integration: [`cmake/Internationalization.cmake`](../cmake/Internationalization.cmake)

### Workflow

1. **Update source strings:** Run `lupdate` on the project to extract translatable strings:
   ```bash
   lupdate openterfaceQT.pro
   ```

2. **Add a new language:** Create a new `.ts` file for your locale (e.g., `openterface_ja.ts` for Japanese)

3. **Translate:** Open the `.ts` file in **Qt Linguist** and translate all strings

4. **Compile:** Run `lrelease` to generate `.qm` binary files:
   ```bash
   lrelease openterfaceQT.pro
   ```

5. **Register:** Ensure the new language appears in the `LanguageManager` list

### Supported Languages

The project currently supports 12 languages. Check [`config/languages/`](config/languages/) for the full list.

---

## Adding a Video Resolution

### EDID Tables

Resolutions are managed through the EDID (Extended Display Identification Data) system. The EDID parser extracts supported resolutions from the device's EDID data.

### Steps

1. **EDID constants:** Resolution timing constants are defined in the EDID-related source files. Each resolution has standard VESA timing parameters.

2. **UI wiring:** The resolution selection dropdown is populated from `GlobalVar` input resolution values, which are derived from the EDID parser output.

3. **GlobalSetting integration:** Add the new resolution to the supported resolution list in `GlobalSetting::getSupportedResolutions()` or equivalent.

### Testing

- Verify the new resolution appears in the preferences dropdown
- Test video stream at the new resolution
- Check aspect ratio calculations are correct
- Verify recording works at the new resolution

---

## Contributing Workflow

### Git Branching

- **Feature branches:** `feature/description` — e.g., `feature/add-japanese-translation`
- **Bug fixes:** `fix/description` — e.g., `fix/serial-port-crash`
- **Documentation:** `docs/description` — e.g., `docs/architecture-diagram`

### Pull Requests

1. Fork the repository or create a branch on the main repo
2. Make your changes on a feature branch
3. Build and test locally
4. Open a PR with:
   - A clear title describing the change
   - Description of what changed and why
   - Screenshots for UI changes
   - Testing notes

### Commit Style

Follow the existing commit message style (check `git log --oneline` for examples):

- Short summary line (under 72 characters)
- Optional detailed description
- Reference issue numbers if applicable (e.g., `(#123)`)

### Code Style

- **C++17** standard
- Qt naming conventions (PascalCase for classes, camelCase for methods)
- Use Qt smart pointers (`QScopedPointer`, `QSharedPointer`) or `std::unique_ptr` for owned objects
- Use Qt signals/slots for cross-component communication
- Avoid raw `new` without parent or smart pointer

---

## Debugging Tips

### Logging Levels

The application has a configurable logging system. Access it via **Preferences → Log**:

- **Debug:** Verbose output for development
- **Info:** Normal operational messages
- **Warning:** Potential issues
- **Error:** Actual errors

Log settings are loaded at startup via `GlobalSetting::loadLogSettings()`.

### Serial Debug Dialog

Open the serial console from the main window to see raw serial data being sent/received. Message filters (Keyboard, Mouse, HID, Media) let you isolate specific traffic types.

### Environment Variables Dialog

The `EnvironmentSetupDialog` (shown 500ms after startup) checks for required drivers, udev rules, and system dependencies. Useful for diagnosing platform-specific issues.

### Common Pitfalls

| Problem | Likely Cause | Fix |
|---------|-------------|-----|
| "Qt platform plugin" error | Missing Qt plugins | Set `QT_PLUGIN_PATH` environment variable |
| Serial port not detected | `brltty` claiming port | Remove `brltty` service |
| No video stream | Wrong backend selected | Switch to FFmpeg backend in preferences |
| Permission denied on USB | Missing udev rules | Run udev setup from BUILD.md |
| FFmpeg not found at build | Wrong prefix path | Use `-DFFMPEG_PREFIX=/usr` (Fedora) or system lib path |
| ARM64 build crashes | LTO optimization bug | LTO is already disabled for arm64 in CMakeLists.txt |

### GStreamer Debug

If using the GStreamer backend, enable verbose logging:

```bash
GST_DEBUG=3 ./openterfaceQT
```

Or set the sink element explicitly:

```bash
OPENTERFACE_GST_SINK=glimagesink ./openterfaceQT
```

### Device Detection

Check if the device is properly enumerated:

```bash
# Check USB devices
lsusb | grep -E "534d|1a86"

# Check serial port
ls /dev/ttyUSB*

# Check HID devices
ls /dev/hidraw*

# Check kernel messages
dmesg | tail -20
```
