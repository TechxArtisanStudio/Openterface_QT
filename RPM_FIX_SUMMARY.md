# RPM Installation Issue - Fix Summary

## Problem

The RPM package installation on Fedora 42 was failing with:
```
Failed to resolve the transaction:
Problem: conflicting requests
  - nothing provides libbz2.so.1.0()(64bit) needed by openterfaceqt-0.5.4.310-1.x86_64 from @commandline
```

## Root Causes

There were **two critical issues** in the RPM packaging setup:

### Issue 1: Missing `libbz2` Library in RPM Bundle
**File**: `build-script/docker-build-shared.sh`

The `docker-build-shared.sh` script copies various libraries to the RPM SOURCES directory for bundling, but it was **missing the code to bundle `libbz2`** libraries. While the spec file declares that `libbz2*.so*` should be copied, the build script was never actually copying them to the SOURCES directory.

**Impact**: The RPM was built declaring it needs `libbz2.so.1.0`, but it wasn't bundled with the package, causing RPM dependency resolution to fail.

### Issue 2: Overly Strict RPM Dependencies
**File**: `packaging/rpm/spec`

The spec file had:
```bash
Requires: libusb, bzip2-libs
```

This explicitly declared that the RPM requires `bzip2-libs` from the system. However, since we're bundling libraries in `/usr/lib/openterfaceqt/`, the binary should NOT require system package dependencies for bundled libraries.

**Additional Problem**: The spec file was missing `AutoReqProv: no`, which meant RPM's automatic dependency detection was analyzing the binary and discovering that it links against `libbz2.so.1.0`, then creating a hard requirement on `bzip2-libs`. This conflicted with the bundling strategy.

## Solutions Applied

### Fix 1: Add `libbz2` Bundling to `docker-build-shared.sh`

**Location**: Lines 1079-1107 in `build-script/docker-build-shared.sh`

Added comprehensive code to search for and copy `libbz2` libraries to the RPM SOURCES directory:

```bash
# Copy bzip2 libraries to SOURCES for bundling (needed for compression support in FFmpeg)
echo "🔍 Searching for bzip2 libraries to RPM SOURCES..."

# Copy libbz2 libraries - search multiple locations
echo "📋 RPM: Searching for libbz2 libraries..."
LIBBZ2_FOUND=0
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libbz2.so* >/dev/null 2>&1; then
            echo "   ✅ Found libbz2 in $SEARCH_DIR"
            LIBBZ2_FILES=$(ls -la "$SEARCH_DIR"/libbz2.so*)
            echo "   Files found:"
            echo "$LIBBZ2_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libbz2.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ libbz2 libraries copied to ${RPMTOP}/SOURCES"
            LIBBZ2_FOUND=1
            break
        fi
    fi
done
if [ $LIBBZ2_FOUND -eq 0 ]; then
    echo "❌ ERROR: libbz2 libraries not found in any search path!"
else
    echo "✅ libbz2 found and copied"
fi
```

**Why this works**:
- Searches standard library locations: `/opt/ffmpeg/lib` (custom builds), `/usr/lib/x86_64-linux-gnu`, `/usr/lib`
- Copies all `libbz2.so*` files (library and symlinks) to the RPM SOURCES
- When the RPM is built, these libraries are included in the package
- At runtime, the bundled `libbz2` satisfies the binary's linkage requirement

### Fix 2: Disable Automatic Dependency Detection in `packaging/rpm/spec`

**Location**: Lines 8-15 in `packaging/rpm/spec`

**Before**:
```bash
# System-level dependencies only - Qt, FFmpeg and plugins are bundled in /usr/lib/openterfaceqt/
Requires: libusb, bzip2-libs
```

**After**:
```bash
# Disable automatic dependency detection since we bundle most libraries
AutoReqProv: no

# System-level dependencies only - Qt, FFmpeg and plugins are bundled in /usr/lib/openterfaceqt/
# libusb: USB device access (not bundled, system package)
Requires: libusb
```

**Why this works**:
- `AutoReqProv: no` disables automatic dependency detection by RPM
- This prevents RPM from analyzing the binary and discovering `libbz2.so.1.0` dependency
- `Requires: libusb` remains because USB device access isn't bundled
- The spec file already copies `libbz2*.so*` in the `%install` section, ensuring they're in the package
- At installation, the post-install script registers bundled libraries with ldconfig in `/etc/ld.so.conf.d/openterface-libs.conf`

## How It Works End-to-End

1. **Build Phase** (`docker-build-shared.sh`):
   - Binary is compiled against FFmpeg and other libraries (including `libbz2`)
   - `libbz2` libraries are located and copied to RPM SOURCES directory

2. **Package Phase** (`docker-build-shared.sh` + `packaging/rpm/spec`):
   - The spec file copies all libraries from SOURCES to `/usr/lib/openterfaceqt/`
   - Binary is installed to `/usr/bin/openterfaceQT`
   - `AutoReqProv: no` prevents RPM from creating false dependencies

3. **Installation Phase** (user's system):
   - RPM is installed with only `libusb` as a system dependency
   - Post-install script creates `/etc/ld.so.conf.d/openterface-libs.conf` pointing to `/usr/lib/openterfaceqt`
   - `ldconfig` is run to register bundled libraries
   - Binary can now find all bundled libraries at `/usr/lib/openterfaceqt/`

## Library Bundling Strategy

The RPM now properly bundles these libraries in `/usr/lib/openterfaceqt/`:
- **Qt6**: All `libQt6*.so*` libraries
- **Image Processing**: `libjpeg.so*`, `libturbojpeg.so*`
- **USB**: `libusb*.so*` (for USB device access)
- **Compression**: `libbz2.so*` (for FFmpeg compression support), `libz.so*`
- **Hardware Acceleration**: `libva*.so*`, `libvdpau*.so*`
- **Multimedia**: `libgstreamer*.so*`, `libv4l*.so*`, `liborc*.so*`
- **FFmpeg**: `libavdevice.so*`, `libavcodec.so*`, `libavformat.so*`, `libavutil.so*`, `libswscale.so*`, `libswresample.so*`, `libavfilter.so*`

## Testing

To test the fix:

1. **Rebuild the RPM package**:
   ```bash
   bash docker/docker-build-shared.sh
   ```

2. **Install on Fedora 42**:
   ```bash
   sudo dnf install openterfaceQT_0.5.4_x86_64.rpm
   ```

3. **Verify libraries are registered**:
   ```bash
   ldconfig -p | grep openterfaceqt
   ```

4. **Run the application**:
   ```bash
   openterfaceQT
   ```

## Related Files Changed

- `/Users/pengtianyu/projects/kevinzjpeng/Openterface_QT/build-script/docker-build-shared.sh` (29 lines added)
- `/Users/pengtianyu/projects/kevinzjpeng/Openterface_QT/packaging/rpm/spec` (4 lines changed, 1 line removed)

## Notes for Future Maintenance

1. **Keep libraries in sync**: If adding new dependencies to the build, ensure they're copied in both DEB and RPM sections
2. **Monitor FFmpeg updates**: If FFmpeg is updated and requires additional compression libraries, add them to the bundling search
3. **Test on multiple distributions**: Verify RPM packages on Fedora, RHEL, and openSUSE variants
4. **Consider zstd**: Modern systems prefer `zstd` compression; consider bundling if using newer FFmpeg builds
