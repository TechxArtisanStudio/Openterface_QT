# Qt 6.6 Bundling Changes for DEB Installation

## Summary

The `docker-build-shared.sh` script has been updated to properly bundle Qt 6.6 libraries and plugins with your application for standalone DEB installations.

## Changes Made

### 1. **New Directory Structure**

The bundled application now uses the following directory structure in the DEB:

```
/usr/local/openterface/
├── lib/                          # Qt libraries
│   ├── libQt6Core.so.*
│   ├── libQt6Gui.so.*
│   ├── libQt6Widgets.so.*
│   ├── libQt6Network.so.*
│   ├── libQt6Concurrent.so.*
│   └── ... (all libQt6*.so files)
├── plugins/                       # Qt plugins
│   ├── platforms/
│   │   ├── libqxcb.so
│   │   ├── libqwayland-generic.so
│   │   └── ...
│   ├── iconengines/
│   ├── imageformats/
│   ├── styles/
│   └── ... (all plugin categories)
└── qml/                          # Qt QML imports (if needed)
    └── ...
```

### 2. **Qt Library Bundling**

**File:** `docker-build-shared.sh` (lines 142-216)

- All `libQt6*.so*` libraries are copied to `/usr/local/openterface/lib`
- Includes symlinks (`.so` files) and versioned libraries (`.so.6`, etc.)
- Fallback paths checked:
  - `/opt/Qt6/lib` (custom Qt build)
  - `/usr/lib/x86_64-linux-gnu` (Debian/Ubuntu)
  - `/usr/lib` (fallback)

### 3. **Qt Plugin Bundling**

**File:** `docker-build-shared.sh` (lines 218-235)

- All plugin directories copied to `/usr/local/openterface/plugins`
- Includes:
  - **platforms/** - Platform abstraction (X11, Wayland, etc.)
  - **iconengines/** - Icon engine support
  - **imageformats/** - Image format support
  - **styles/** - GUI styles
  - **platformthemes/** - Platform theme integration

### 4. **Qt QML Support**

**File:** `docker-build-shared.sh` (lines 237-243)

- Optional Qt QML imports bundled to `/usr/local/openterface/qml`
- Enables QML-based UI components if used

### 5. **Updated Binary RPATH**

**File:** `docker-build-shared.sh` (lines 271-275)

Before:
```bash
patchelf --set-rpath '/usr/lib:/usr/lib/ffmpeg-plugins' ...
```

After:
```bash
patchelf --set-rpath '$ORIGIN/../openterface/lib:/usr/lib:/usr/lib/ffmpeg-plugins' ...
```

**Key points:**
- Uses `$ORIGIN` to make the path relative to the binary location
- `../openterface/lib` resolves to the bundled Qt libraries
- Maintains fallback to system libraries (`/usr/lib`)

### 6. **Enhanced Launcher Script**

**File:** `docker-build-shared.sh` (lines 293-322)

The launcher script (`start-openterface.sh`) now:

1. **Dynamically calculates paths** relative to its location:
   ```bash
   SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
   APPDIR="$(cd "${SCRIPT_DIR}/../openterface" && pwd)"
   ```

2. **Sets `LD_LIBRARY_PATH`** to prioritize bundled Qt libraries:
   ```bash
   export LD_LIBRARY_PATH="${APPDIR_LIB}:/usr/lib:..."
   ```

3. **Configures Qt environment variables:**
   ```bash
   export QT_PLUGIN_PATH="${APPDIR}/plugins:..."
   export QML2_IMPORT_PATH="${APPDIR}/qml:..."
   export QT_QPA_PLATFORM_PLUGIN_PATH="${APPDIR}/plugins/platforms:..."
   ```

4. **Ensures proper Qt platform selection:**
   ```bash
   export QT_QPA_PLATFORM="xcb"  # or wayland as needed
   export QT_X11_NO_MITSHM=1     # Prevents MIT-SHM issues
   ```

## Deployment Flow

When the application is installed via DEB:

1. Binary: `/usr/local/bin/openterfaceQT`
2. Launcher: `/usr/local/bin/start-openterface.sh` (used as Exec in .desktop file)
3. Bundled Qt: `/usr/local/openterface/lib`, `/usr/local/openterface/plugins`
4. Startup sequence:
   - User/system calls `start-openterface.sh`
   - Script calculates paths dynamically
   - Script sets up environment variables
   - Script executes binary with bundled Qt libraries

## Benefits

✅ **No Qt dependency conflicts** - Uses bundled Qt instead of system Qt  
✅ **Portable across distributions** - Works on Debian, Ubuntu, Fedora, etc.  
✅ **Relative paths** - Works regardless of installation location  
✅ **Fallback mechanism** - Falls back to system libraries if needed  
✅ **Plugin discovery** - Qt automatically finds bundled plugins  
✅ **Clean environment** - Only loads necessary Qt components  

## Testing

After installation, verify bundled libraries are used:

```bash
# Check library load path
ldd /usr/local/bin/openterfaceQT | grep Qt6

# Check plugin path when running
QT_DEBUG_PLUGINS=1 start-openterface.sh 2>&1 | grep plugin
```

Expected output: Libraries should load from `/usr/local/openterface/lib`

## Compatibility

- **Qt Version:** Qt 6.6+
- **Architectures:** x86_64, aarch64, armv7l
- **Platforms:** Debian, Ubuntu, RHEL, Fedora
- **Supported since:** Docker build with this updated script
