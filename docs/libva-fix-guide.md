# libva Symbol Conflict Fix Guide

## Problem Description

The following error occurs when running openterfaceQT:
```
./openterfaceQT: symbol lookup error: /lib64/libavutil.so.59: undefined symbol: vaMapBuffer2
```

## Root Cause

1. **Old RPM packaging issue**: The RPM package bundled an old version of libva (2.1400.0) into `/usr/lib/openterfaceqt/`
2. **ldconfig priority issue**: `/etc/ld.so.conf.d/openterface-libs.conf` caused the bundled libva to take priority over the system libva
3. **Symbol incompatibility**: The old libva (2.1400.0) is missing the `vaMapBuffer2` symbol, but the system's libavutil.so.59 requires this symbol

## Solutions

### Solution 1: Fix the current system immediately (Recommended)

Run the cleanup script:

```bash
sudo ./scripts/cleanup-old-libva.sh
```

This script will:
- Back up the old bundled libva libraries
- Remove libva libraries from `/usr/lib/openterfaceqt/`
- Update the ldconfig cache
- Verify that the system libva is available

### Solution 2: Manual fix

```bash
# 1. Remove the leftover ldconfig configuration file
sudo rm -f /etc/ld.so.conf.d/openterface-libs.conf

# 2. Remove the bundled libva libraries
sudo rm -f /usr/lib/openterfaceqt/libva*.so*

# 3. Update the ldconfig cache
sudo ldconfig

# 4. Verify the fix
ldconfig -p | grep "libva.so.2"
```

### Solution 3: Use the launcher script (Temporary workaround)

The launcher script is configured with `LD_PRELOAD` to preload the system libva:

```bash
cd build
./openterfaceQT-launcher.sh
```

## Root Fix (Already Applied)

The following changes have been applied to the codebase to prevent the issue from recurring:

### 1. Build script changes (`build-script/docker-build-rpm.sh`)

No longer bundles libva libraries; uses system libraries instead:

```bash
# Hardware acceleration libraries are NOT bundled - use system libraries instead
# Bundling libva causes symbol incompatibilities with system libavutil (e.g., vaMapBuffer2)
# Users must install system libva packages: libva, libva-drm, libva-x11
# "VA|VA-API|libva.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
# "VADRM|VA-API DRM|libva-drm.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
# "VAX11|VA-API X11|libva-x11.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
```

### 2. RPM Spec file changes (`packaging/rpm/spec`)

- **Added libva as a system dependency**:
  ```spec
  Requires:       libva
  Requires:       libva-drm
  Requires:       libva-x11
  ```

- **Removed the code that bundled libva**:
  ```spec
  # Hardware acceleration libraries - use system libraries, do NOT bundle
  # libva is now a system dependency (see Requires: section above)
  ```

- **Removed the code that created libva symlinks**

### 3. Installation script changes (`docker/install-openterface.sh`)

Automatically removes the leftover `openterface-libs.conf` file during installation (lines 622-636)

## System Requirements

After the fix, the system must have libva libraries installed:

### Fedora/RHEL
```bash
sudo dnf install libva libva-drm libva-x11
```

### Ubuntu/Debian
```bash
sudo apt install libva2 libva-drm2 libva-x11-2
```

### Arch Linux
```bash
sudo pacman -S libva libva-utils
```

## Verifying the Fix

1. **Check libva library location**:
   ```bash
   ldconfig -p | grep libva.so.2
   ```
   It should show the system library path (e.g., `/usr/lib64/libva.so.2`), not `/usr/lib/openterfaceqt/`

2. **Check the vaMapBuffer2 symbol**:
   ```bash
   nm -D /usr/lib64/libva.so.2 | grep vaMapBuffer2
   ```
   It should display the `vaMapBuffer2` symbol

3. **Run the application**:
   ```bash
   ./build/openterfaceQT
   ```
   It should start normally without symbol errors

## Technical Details

### Why no longer bundle libva?

1. **System-level library**: libva is a hardware acceleration library closely tied to the system GPU driver
2. **Symbol compatibility**: Different versions of libva may have different symbol sets
3. **Dependency chain**: The system libavutil depends on specific symbols from the system libva
4. **Maintenance cost**: Bundling libva requires tracking system updates, which is costly to maintain

### Why use the system libva?

1. **Version consistency**: The system libva matches the system libavutil version
2. **Driver compatibility**: The system libva matches the system GPU driver version
3. **Automatic updates**: The system package manager handles updates automatically
4. **Reduced conflicts**: Avoids having multiple versions of the library coexisting

## Troubleshooting

### Issue: Application reports libva not found after launch

**Solution**: Install the system libva packages
```bash
# Fedora
sudo dnf install libva libva-drm libva-x11

# Ubuntu
sudo apt install libva2 libva-drm2 libva-x11-2
```

### Issue: vaMapBuffer2 error still occurs

**Solution**:
1. Confirm all bundled libva libraries have been removed:
   ```bash
   ls -la /usr/lib/openterfaceqt/libva*
   ```
   It should be empty or the files should not exist

2. Confirm the ldconfig cache has been updated:
   ```bash
   sudo ldconfig
   ```

3. Confirm the system libva has the vaMapBuffer2 symbol:
   ```bash
   nm -D /usr/lib64/libva.so.2 | grep vaMapBuffer2
   ```

### Issue: Need to restore the old bundled libva

**Solution**: Restore from backup
```bash
sudo cp /usr/lib/openterfaceqt/backup-libva-*/*.so* /usr/lib/openterfaceqt/
sudo ldconfig
```

Note: This will cause the original issue to reappear. Only use this for debugging purposes.

## Related Files

- Build script: `build-script/docker-build-rpm.sh`
- RPM Spec: `packaging/rpm/spec`
- Installation script: `docker/install-openterface.sh`
- Cleanup script: `scripts/cleanup-old-libva.sh`
- Launcher script: `build/openterfaceQT-launcher.sh`

## Changelog

- 2026-08-18: Removed libva bundling, switched to system dependency
- 2026-08-18: Added cleanup script
- 2026-08-18: Updated RPM spec file
- 2026-08-18: Updated build script
