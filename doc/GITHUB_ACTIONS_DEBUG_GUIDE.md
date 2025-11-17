# GitHub Actions Wayland Platform Detection Debug Guide

## Quick Start: Enable Debug Mode

To see exactly which detection method is working (or failing), run with:

```bash
export OPENTERFACE_DEBUG=1
./openterfaceQT
```

## What You Should See

### If Wayland Detection Works ✅

```
🔍 Platform Detection: Starting comprehensive Wayland detection...
   DISPLAY=:98
   XDG_SESSION_TYPE=not set
   LD_PRELOAD set: YES (49 entries)
  ❌ Method 1 (systemd): wayland-session.target NOT active or systemctl unavailable
  ❌ Method 2 (systemd env): QT_QPA_PLATFORM=wayland NOT found
  ❌ Method 3 (XDG): XDG_SESSION_TYPE='not set' does NOT contain 'wayland'
  ✅ Method 5 (LD_PRELOAD): Found libwayland-client in LD_PRELOAD

✅ Platform Detection: Using Wayland (auto-detected as primary)
   Detection method: LD_PRELOAD detection (CI/CD environment)
   XDG_SESSION_TYPE=unknown
   Setting QT_QPA_PLATFORM=wayland
```

### If Wayland Detection FAILS ❌

```
🔍 Platform Detection: Starting comprehensive Wayland detection...
   DISPLAY=:98
   XDG_SESSION_TYPE=not set
   LD_PRELOAD set: NO
  ❌ Method 1 (systemd): wayland-session.target NOT active or systemctl unavailable
  ❌ Method 2 (systemd env): QT_QPA_PLATFORM=wayland NOT found
  ❌ Method 3 (XDG): XDG_SESSION_TYPE='not set' does NOT contain 'wayland'
  ❌ Method 4 (filesystem): libwayland-client NOT found in system

⚠️  Platform Detection: Using XCB (Wayland not detected by ANY method)
   DISPLAY=:98
   XDG_SESSION_TYPE=unknown
   LD_PRELOAD contains libwayland-client: NO
   Setting QT_QPA_PLATFORM=xcb
```

## Why Detection Might Fail in GitHub Actions

### Problem 1: Wayland Libraries Not Being Preloaded

**Symptom:** 
- LD_PRELOAD set: NO
- Method 5 fails with: "libwayland-client NOT found in LD_PRELOAD"

**Cause:** The launcher script's library preloading is not finding Wayland libraries

**Solution:** Check `/opt/source/Openterface/kevinzjpeng/Openterface_QT/docker/testos/Dockerfile.fedora-test-shared` and ensure:

```dockerfile
RUN dnf install -y \
    libwayland-client \
    libwayland-cursor \
    libwayland-egl \
    libxkbcommon \
    libxkbcommon-x11
```

### Problem 2: Wayland Libraries Installed But Not Found by find Command

**Symptom:**
- You can see in earlier logs that `/usr/lib64/libwayland-egl.so.1.24.0` exists
- But Method 4 fails with: "libwayland-client NOT found in system"

**Cause:** The `find` command with glob patterns `*` sometimes fails in certain environments

**Solution:** We need to verify find is working:

```bash
# Test Method 4 manually
find /usr/lib/openterfaceqt -name "libwayland-client*" 2>/dev/null
find /lib64 /usr/lib64 /usr/lib -name "libwayland-client*" 2>/dev/null

# If nothing returns, try exact filename
ls -la /usr/lib64/libwayland-client* 2>/dev/null
ls -la /usr/lib64/libwayland-egl* 2>/dev/null
```

### Problem 3: Wayland Libraries NOT Being Installed in Docker

**Symptom:**
- LD_PRELOAD shows no Wayland libraries at all
- Earlier warnings showed: "⚠️ Wayland library not found: libwayland-egl"

**Cause:** Docker image doesn't have Wayland libraries installed

**Solution:** Update Dockerfile.fedora-test-shared to include:

```dockerfile
# Add to the RUN dnf install command
libwayland-client \
libwayland-cursor \
libwayland-egl \
libxkbcommon \
libxkbcommon-x11
```

## The 5 Detection Methods (In Order)

### Method 1: systemd wayland-session.target

```bash
systemctl --user is-active --quiet wayland-session.target
```

- ✅ Works in: Standard Fedora/GNOME desktop
- ❌ Fails in: Docker, containers, headless systems
- Why: systemd user session not running in containers

### Method 2: systemd Environment

```bash
systemctl --user show-environment | grep QT_QPA_PLATFORM=wayland
```

- ✅ Works in: Systems with systemd user session
- ❌ Fails in: Docker, containers, minimal systems
- Why: systemd environment service not running

### Method 3: XDG_SESSION_TYPE

```bash
echo "$XDG_SESSION_TYPE" | grep -q "wayland"
```

- ✅ Works in: Desktop sessions with XDG_SESSION_TYPE set
- ❌ Fails in: Docker, SSH, GitHub Actions
- Why: Environment variable not set in non-interactive environments

### Method 4: Filesystem Library Detection

```bash
find /lib64 /usr/lib64 /usr/lib -name "libwayland-client*"
```

- ✅ Works in: Standard systems with Wayland libraries installed
- ❌ Fails in: Docker if libraries not installed, or find command issues
- Why: Universal check - if Wayland libraries exist, Wayland is available

### Method 5: LD_PRELOAD Detection (CRITICAL FOR CI/CD)

```bash
echo "$LD_PRELOAD" | grep -q "libwayland-client"
```

- ✅ Works in: GitHub Actions, Docker, any CI/CD where preload works
- ❌ Fails in: If Wayland libraries not preloaded
- Why: If launcher script successfully preloaded them, they're in LD_PRELOAD already

## Troubleshooting Checklist

### Step 1: Verify Wayland Libraries Installed

```bash
# Inside Docker container
dnf list installed | grep libwayland
dnf list installed | grep libxkbcommon
```

Expected output:
```
libwayland-client.x86_64              0.24.0-1.fc39       @System
libwayland-cursor.x86_64              0.24.0-1.fc39       @System
libwayland-egl.x86_64                 1.24.0-1.fc39       @System
libxkbcommon.x86_64                   1.5.0-2.fc39        @System
libxkbcommon-x11.x86_64               1.5.0-2.fc39        @System
```

### Step 2: Verify Wayland Libraries Are Findable

```bash
# Test find command
find /lib64 /usr/lib64 /usr/lib -name "libwayland-client*" 2>/dev/null | head -5

# If find returns nothing, try direct check
ls /usr/lib64/libwayland-client* 2>/dev/null || echo "NOT FOUND"
ls /usr/lib64/libwayland-egl* 2>/dev/null || echo "NOT FOUND"
```

### Step 3: Run Launcher with Debug Mode

```bash
export OPENTERFACE_DEBUG=1
./openterfaceQT
```

Look for which method succeeds (✅), or if all fail (❌)

### Step 4: Check Actual LD_PRELOAD Content

```bash
# Run launcher to set it up, then check
echo "$LD_PRELOAD" | tr ':' '\n' | grep wayland
```

Expected output:
```
/usr/lib64/libwayland-client.so.0.24.0
/usr/lib64/libwayland-cursor.so.0.24.0
/usr/lib64/libwayland-egl.so.1.24.0
/usr/lib64/libxkbcommon.so.0.8.1
/usr/lib64/libxkbcommon-x11.so.0.8.1
```

## Complete GitHub Actions Workflow Step

```yaml
- name: Install Wayland Libraries
  run: |
    sudo dnf install -y \
      libwayland-client \
      libwayland-cursor \
      libwayland-egl \
      libxkbcommon \
      libxkbcommon-x11

- name: Run OpenterfaceQT with Debug
  env:
    DISPLAY: :98
    OPENTERFACE_DEBUG: "1"
  run: |
    ./openterfaceQT
```

## What Should Happen

1. **Docker image built** with all Wayland libraries installed
2. **Launcher script runs** and preloads Wayland libraries
3. **Method 5 (LD_PRELOAD detection)** succeeds ✅
4. **QT_QPA_PLATFORM=wayland** is set
5. **Qt6 uses Wayland plugin** instead of XCB
6. **Application launches successfully** ✅

## If Still Failing

### Option 1: Force Wayland Platform

Instead of relying on detection, explicitly set:

```bash
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
./openterfaceQT
```

### Option 2: Check Qt Plugin Availability

```bash
# List available Qt6 platform plugins
ls -la /usr/lib/openterfaceqt/qt6/plugins/platforms/ | grep -E "wayland|xcb"
```

Expected:
```
-rwxr-xr-x libqwayland-generic.so
-rwxr-xr-x libqxcb.so
```

### Option 3: Enable Qt Debug Output

```bash
export QT_DEBUG_PLUGINS=1
export OPENTERFACE_DEBUG=1
./openterfaceQT 2>&1 | head -200
```

## Key Insight for CI/CD

In GitHub Actions environments:
- Systemd-based detection (Methods 1-2) will ALWAYS fail
- XDG environment variables will ALWAYS be missing (Method 3)
- Filesystem checks (Method 4) depend on Docker image setup
- **Method 5 (LD_PRELOAD) is the ONLY reliable method**

Therefore, **the most critical requirement is that Wayland libraries are successfully preloaded into LD_PRELOAD**.

## See Also

- `LAUNCHER_V2_COMPLETE_SPEC.md` - Full technical specification
- `LAUNCHER_WAYLAND_OPTIMIZATION_V2.md` - Architecture and design
- `openterfaceQT-launcher.sh` Lines 485-600 - Platform detection code
