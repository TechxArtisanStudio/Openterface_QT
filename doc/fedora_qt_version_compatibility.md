# Fedora Qt Version Compatibility Solution

## Problem

On Fedora systems with Qt 6.9 or newer installed system-wide, the application would fail to launch with errors like:

```
/lib64/libQt6QmlModels.so.6: version `Qt_6_PRIVATE_API' not found (required by /usr/lib/openterfaceqt/qt6/libQt6Quick.so.6.6.3)
/usr/lib/openterfaceqt/qt6/libQt6Qml.so.6.6.3: version `Qt_6.9_PRIVATE_API' not found (required by /lib64/libQt6QmlModels.so.6)
/usr/lib/openterfaceqt/qt6/libQt6Core.so.6.6.3: version `Qt_6.9' not found (required by /lib64/libQt6QmlModels.so.6)
```

This occurs because:
1. The bundled Qt 6.6.3 libraries have different ABI/symbol versions than Qt 6.9
2. When system Qt 6.9 is installed, the system linker tries to load `libQt6QmlModels.so.6` (system version) which requires Qt 6.9 symbols
3. But the bundled Qt 6.6.3 libraries don't have these symbols, causing version conflicts

## Solution

The enhanced launcher script (`openterfaceQT-launcher.sh`) now:

### 1. **Detects System Qt Version**
- Checks if a system Qt6 installation exists
- Determines the version by:
  - Reading ELF symbols using `readelf`
  - Extracting version from library filenames (e.g., `libQt6Core.so.6.9.3`)

### 2. **Intelligently Selects Qt Version**
- **If system Qt >= 6.9**: Uses system Qt libraries (avoids version conflicts)
- **If system Qt < 6.9**: Uses bundled Qt 6.6.3 (as before)
- **If no system Qt**: Uses bundled Qt 6.6.3 (as before)

### 3. **Configures LD_LIBRARY_PATH Appropriately**
- **For bundled Qt**: Puts `/usr/lib/openterfaceqt/qt6` first in `LD_LIBRARY_PATH`
- **For system Qt**: Puts system paths first (e.g., `/lib64`, `/usr/lib64`), with bundled FFmpeg/GStreamer still included

### 4. **Conditional LD_PRELOAD Setup**
- Only preloads bundled Qt libraries when using bundled Qt
- Skips preload when using system Qt (to avoid forcing incompatible versions)
- This prevents "version `Qt_6_PRIVATE_API' not found" errors

## Implementation Details

### Key Functions

**`get_system_qt_version()`**
- Searches standard library paths for `libQt6Core.so.6`
- Extracts version using `readelf -V` (primary) or filename parsing (fallback)
- Returns version string like "6.9" or "6.6.3"

**`version_gte(v1, v2)`**
- Compares two version strings (e.g., "6.9" >= "6.6")
- Handles major and minor version components
- Returns 0 if v1 >= v2, 1 otherwise

### Environment Variables

When **using system Qt**:
```bash
LD_LIBRARY_PATH=/lib64:/usr/lib64:/lib:/usr/lib:...:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer
LD_BIND_NOW=<unset>  # Not set (to avoid forcing bundled symbols)
LD_PRELOAD=<unset>   # Not preloading bundled Qt
```

When **using bundled Qt** (default):
```bash
LD_LIBRARY_PATH=/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:...
LD_BIND_NOW=1
LD_PRELOAD=/usr/lib/openterfaceqt/qt_version_wrapper.so  # If available
```

## Testing

To test the version detection logic:

```bash
# Enable debug mode
export OPENTERFACE_DEBUG=1
/usr/local/bin/openterfaceQT

# Check the launcher log
cat /tmp/openterfaceqt-launcher-*.log | grep -i "system\|version\|using"
```

Expected output for Fedora with Qt 6.9:
```
System Qt version detected: 6.9
✅ System Qt 6.9 >= 6.9, using system Qt libraries (more compatible)
```

## Fallback Behavior

The solution gracefully handles various scenarios:

1. **Fedora with Qt 6.9+**: Automatically uses system Qt
2. **Fedora with Qt 6.7/6.8**: Uses bundled Qt (more compatible)
3. **Fedora with only bundled Qt**: Uses bundled Qt
4. **No Qt installed**: Uses bundled Qt (fails with clear error if missing)
5. **Detection fails**: Falls back to bundled Qt (conservative approach)

## Files Modified

- **`packaging/rpm/openterfaceQT-launcher.sh`**: Updated with Qt version detection and conditional library loading

## Backward Compatibility

- **DEB packages**: No changes needed (use Debian's consistent Qt versions)
- **RPM packages**: Already implemented in updated launcher script
- **AppImage**: Independent, uses bundled libraries exclusively
- **Windows/macOS**: Not affected

## Notes for Distribution Maintainers

If bundling on a system with Qt 6.9+:

1. The application will automatically detect and use system Qt 6.9
2. Ensure all Qt 6.9 libraries are properly installed
3. Test the application to ensure Qt module compatibility
4. Consider adding a note to users about Qt version requirements

If you want to force bundled Qt even with newer system Qt:

```bash
# Set environment variable to disable system Qt detection
OPENTERFACE_FORCE_BUNDLED_QT=1 /usr/local/bin/openterfaceQT
```

(Future enhancement to add support for this environment variable if needed)
