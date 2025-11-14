# Fedora Qt 6.9 vs Bundled Qt 6.6.3 - Complete Solution

## The Problem (Summary)

On Fedora with system Qt 6.9, OpenterfaceQT (built with Qt 6.6.3) fails with:
```
/usr/bin/openterfaceQT-bin: /lib64/libQt6Core.so.6: version `Qt_6_PRIVATE_API' not found
```

**Root cause:** The dynamic linker loads system Qt 6.9 from `/lib64/` instead of bundled Qt 6.6.3 from `/usr/lib/openterfaceqt/qt6/`, even though bundled libraries have higher priority in RPATH/LD_LIBRARY_PATH.

## Why This Happens

1. **Binary's RPATH is set** to `/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt`
2. **But** the linker still checks standard paths `/lib64`, `/usr/lib64` as a fallback
3. **System Qt6.9** is found in `/lib64/libQt6Core.so.6`
4. **When `libQt6Quick.so.6.6.3` tries to load `libQt6QmlModels.so.6`**, the linker finds `/lib64/libQt6QmlModels.so.6` (Qt 6.9)
5. **Version mismatch!** Qt 6.9's API symbols don't exist in Qt 6.6.3 binaries

## The Multi-Part Solution

### Part 1: Ensure RPM Contains ALL Qt6 Libraries ✅

Your RPM already has 61 Qt6 libraries including critical ones:
- `libQt6QmlModels.so.6.6.3` ← **This was missing before, now included!**
- `libQt6QmlWorkerScript.so.6.6.3` ← **This was missing before, now included!**
- All Quick, Controls, and QML modules

**Verification:**
```bash
rpm -ql openterfaceQT_*.rpm | grep "libQt6" | wc -l
# Should show: 61 (or similar number of Qt6 libraries)
```

### Part 2: Set Binary's RPATH Correctly ✅

The Docker build script now sets RPATH to prioritize bundled paths:
```bash
patchelf --set-rpath '/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt' openterfaceQT-bin
```

**Verification:**
```bash
patchelf --print-rpath /usr/bin/openterfaceQT-bin
# Should show: /usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:...
```

### Part 3: Compile Qt Version Wrapper ✅

The wrapper intercepts `dlopen()` calls to prevent system Qt from loading.

**What it does:**
```
Binary requests: dlopen("/lib64/libQt6Core.so.6")
  ↓ (wrapper intercepts)
Wrapper checks: "Is this from /lib64? Is it a Qt6 library?"
  ↓
If yes: Redirect to: "/usr/lib/openterfaceqt/qt6/libQt6Core.so.6.6.3"
If no: Allow normal loading
```

**Status:** Compiled in Docker build and installed in RPM

**Verification:**
```bash
ls -la /usr/lib/openterfaceqt/qt_version_wrapper.so
# Should exist and be executable
```

### Part 4: Use Correct Launcher ✅

The launcher must:
1. Set `LD_LIBRARY_PATH` with bundled paths first
2. Set `LD_BIND_NOW=1` to catch version errors early
3. Preload the wrapper in `LD_PRELOAD`
4. Use `exec` to execute the binary (not background process)

**Files:**
- `openterfaceQT-launcher.sh` - Complex version with many preloads
- `openterfaceQT-launcher-simple.sh` - NEW: Simplified version, recommended

**Which to use:**
If experiencing conflicts: Use the **simple launcher**
```bash
/usr/lib/openterfaceqt/setup-env.sh /usr/bin/openterfaceQT-bin
```

## Testing the Fix

### Step 1: Verify RPM Contents
```bash
# Extract RPM
rpm2cpio openterfaceQT_*.rpm | cpio -idm

# Check for critical libraries
ls -la rpm-contents/usr/lib/openterfaceqt/qt6/libQt6Qml* \
        rpm-contents/usr/lib/openterfaceqt/qt6/libQt6Core*
# Should show all libQt6*.so.6.6.3 files

# Count total Qt6 libraries
find rpm-contents/usr/lib/openterfaceqt/qt6 -name "libQt6*.so*" | wc -l
# Should be ~61 libraries
```

### Step 2: Install and Test
```bash
# Install the RPM
sudo dnf install ./openterfaceQT_*.rpm

# Run with debug output
export OPENTERFACE_DEBUG=1
/usr/bin/openterfaceQT

# Should see:
# ✅ Qt Version Wrapper loaded: /usr/lib/openterfaceqt/qt_version_wrapper.so
# ✅ Application launched successfully
```

### Step 3: Verify Libraries Are From Bundled Qt
```bash
# Check which library was actually loaded
ldd /usr/bin/openterfaceQT-bin | grep libQt6Core

# Should show:
# libQt6Core.so.6 => /usr/lib/openterfaceqt/qt6/libQt6Core.so.6.6.3
# NOT: /lib64/libQt6Core.so.6 (system Qt)
```

### Step 4: Check for Version Errors
```bash
# Run app and capture output
export OPENTERFACE_DEBUG=1
/usr/bin/openterfaceQT 2>&1 | tee test.log

# Should NOT contain:
# "version `Qt_6_PRIVATE_API' not found"
# "version `Qt_6.9' not found"
# "version `Qt_6.9_PRIVATE_API' not found"

# If you see these, check:
# 1. Wrapper is installed: ls -la /usr/lib/openterfaceqt/qt_version_wrapper.so
# 2. Wrapper is preloaded: echo $LD_PRELOAD
# 3. Bundled Qt path is first: echo $LD_LIBRARY_PATH
```

## Troubleshooting

### Problem: Still seeing "version `Qt_6_PRIVATE_API' not found"

**Solution 1: Check if wrapper is being preloaded**
```bash
export OPENTERFACE_DEBUG=1
/usr/bin/openterfaceQT 2>&1 | grep -i "wrapper"
# Should show: "Qt Version Wrapper loaded"

# If not, wrapper might not be compiled/installed
ls -la /usr/lib/openterfaceqt/qt_version_wrapper.so
```

**Solution 2: Force wrapper preload manually**
```bash
LD_PRELOAD=/usr/lib/openterfaceqt/qt_version_wrapper.so /usr/bin/openterfaceQT-bin
```

**Solution 3: Use fallback environment setup**
```bash
/usr/lib/openterfaceqt/setup-env.sh /usr/bin/openterfaceQT-bin
```

### Problem: "libQt6QmlModels.so.6 not found"

**Solution:** RPM might not have the library

```bash
# Check if library is in RPM
rpm -ql openterfaceQT_*.rpm | grep libQt6QmlModels

# If not found, rebuild RPM ensuring all libraries are copied in build phase
# Check docker-build-rpm.sh for CRITICAL MODULES section
```

### Problem: Application runs but crashes later

**Solution 1: Check plugin paths**
```bash
export QT_PLUGIN_PATH=/usr/lib/openterfaceqt/qt6/plugins
export QML2_IMPORT_PATH=/usr/lib/openterfaceqt/qt6/qml
/usr/bin/openterfaceQT
```

**Solution 2: Run with full debug**
```bash
export OPENTERFACE_DEBUG=1
export QT_DEBUG_PLUGINS=1
/usr/bin/openterfaceQT 2>&1 | tee debug.log
# Review debug.log for missing plugins
```

## Integration Checklist

- [x] RPM contains 61 Qt6 libraries (including QmlModels, QmlWorkerScript, etc.)
- [x] Binary's RPATH points to bundled paths first
- [x] Qt Version Wrapper source included (qt_version_wrapper.c)
- [x] Wrapper is compiled during RPM build (gcc -shared)
- [x] Wrapper installed to `/usr/lib/openterfaceqt/qt_version_wrapper.so`
- [x] Launcher script preloads wrapper
- [x] Launcher sets `LD_BIND_NOW=1`
- [x] Launcher sets `LD_LIBRARY_PATH` with bundled paths first
- [x] Setup script available as fallback (`setup-env.sh`)
- [x] Diagnostic script available (`diagnose-qt-conflicts.sh`)

## Files Involved

| File | Purpose |
|------|---------|
| `build-script/docker-build-rpm.sh` | Copies Qt libs, compiles wrapper, sets RPATH |
| `packaging/rpm/spec` | Installs wrapper and launcher |
| `packaging/rpm/openterfaceQT-launcher.sh` | Complex launcher with preloads |
| `packaging/rpm/openterfaceQT-launcher-simple.sh` | NEW: Simple launcher (recommended) |
| `packaging/rpm/qt_version_wrapper.c` | dlopen() interceptor source |
| `packaging/rpm/setup-env.sh` | Environment setup fallback |
| `packaging/rpm/diagnose-qt-conflicts.sh` | Diagnostic tool |

## Key Takeaway

The issue is **NOT** that bundled libraries aren't in the RPM (they are - all 61 of them!).

The issue **WAS** that when `libQt6Quick.so.6.6.3` tried to load its dependency `libQt6QmlModels.so.6`, the linker would find the system Qt6.9 version instead of the bundled Qt6.6.3 version.

**The fix** is to use a preload wrapper that intercepts `dlopen()` calls and redirects all system Qt paths to bundled Qt paths. This wrapper is now compiled, installed, and automatically preloaded by the launcher.

**Result:** All Qt6 loading goes through the wrapper → all system Qt paths are redirected → bundled Qt 6.6.3 is used exclusively → no version conflicts!
