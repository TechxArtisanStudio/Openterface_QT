# Quick Reference: OpenterfaceQT in GitHub Actions

## TL;DR - Just Run This

### In your GitHub Actions workflow:

```yaml
steps:
  - uses: actions/checkout@v3
  
  - name: Install Wayland Dependencies
    run: |
      sudo dnf install -y \
        libwayland-client \
        libwayland-cursor \
        libwayland-egl \
        libxkbcommon \
        libxkbcommon-x11
  
  - name: Run OpenterfaceQT
    env:
      DISPLAY: :98
    run: |
      cd /opt/source/Openterface/kevinzjpeng/Openterface_QT
      ./packaging/rpm/openterfaceQT
```

## Debug Mode (Optional)

To see what's happening during platform detection:

```bash
export OPENTERFACE_DEBUG=1
./packaging/rpm/openterfaceQT
```

### Expected Success Output

```
🔍 Platform Detection: Starting comprehensive Wayland detection...
   DISPLAY=:98
   XDG_SESSION_TYPE=not set
   LD_PRELOAD set: YES (49 entries)
  ❌ Method 1 (systemd): wayland-session.target NOT active
  ❌ Method 2 (systemd env): QT_QPA_PLATFORM=wayland NOT found
  ❌ Method 3 (XDG): XDG_SESSION_TYPE='not set'
  ❌ Method 4 (filesystem): libwayland-client NOT found
  ✅ Method 5 (LD_PRELOAD): Found libwayland-client in LD_PRELOAD

✅ Platform Detection: Using Wayland (auto-detected as primary)
   Detection method: LD_PRELOAD detection (CI/CD environment)
   Setting QT_QPA_PLATFORM=wayland
```

Then Qt will load and use the Wayland plugin ✅

## Troubleshooting

### Error: "Could not load the Qt platform plugin xcb"

**Problem:** Platform detection chose XCB instead of Wayland

**Solution:**
1. Verify Wayland libraries installed:
   ```bash
   dnf list installed | grep libwayland
   ```
   
2. Run with debug mode to see which method failed:
   ```bash
   export OPENTERFACE_DEBUG=1
   ./packaging/rpm/openterfaceQT
   ```

3. Check if all methods show ❌:
   - If so, check that `Dockerfile.fedora-test-shared` includes Wayland packages
   - If Docker doesn't have them, Method 5 (LD_PRELOAD) can't work

### Error: "Wayland library not found"

**Problem:** Wayland libraries not installed in Docker image

**Solution:** Add to your Dockerfile:

```dockerfile
RUN dnf install -y \
    libwayland-client \
    libwayland-cursor \
    libwayland-egl \
    libxkbcommon \
    libxkbcommon-x11
```

### Error: "could not connect to display :98"

**Problem:** Platform detection failed entirely

**Solution:** Force Wayland explicitly:

```bash
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
./packaging/rpm/openterfaceQT
```

## What Changed

| Component | Change | Impact |
|-----------|--------|--------|
| `openterfaceQT-launcher.sh` | Added 5-method Wayland detection | Automatically selects right platform |
| `Dockerfile.fedora-test-shared` | Added Wayland libraries | Method 5 (LD_PRELOAD) detection works |
| Debug output | Added comprehensive logging | Can see which detection method works |

## The 5 Detection Methods

1. **systemd wayland-session.target** - Standard Fedora desktop
2. **systemd QT_QPA_PLATFORM=wayland** - Systems with systemd session
3. **XDG_SESSION_TYPE=wayland** - Desktop session environment
4. **Filesystem check for libwayland-client** - System library presence
5. **LD_PRELOAD detection** - **⭐ WORKS IN GITHUB ACTIONS**

**Method 5 is the key to CI/CD success** - it detects that Wayland libraries were successfully preloaded into the process.

## Test Locally

```bash
# Install required packages
sudo dnf install -y \
  libwayland-client \
  libwayland-cursor \
  libwayland-egl \
  libxkbcommon \
  libxkbcommon-x11

# Run with Wayland display
export DISPLAY=:1
export WAYLAND_DISPLAY=wayland-1
./packaging/rpm/openterfaceQT

# Or with debug
export OPENTERFACE_DEBUG=1
./packaging/rpm/openterfaceQT
```

## Files to Review

- **To understand the fix:** `doc/GITHUB_ACTIONS_WAYLAND_FIX.md`
- **For troubleshooting:** `doc/GITHUB_ACTIONS_DEBUG_GUIDE.md`
- **To see the code:** `packaging/rpm/openterfaceQT-launcher.sh` (lines 485-600)

## Support

If still having issues:

1. Check `GITHUB_ACTIONS_DEBUG_GUIDE.md` troubleshooting section
2. Run with `OPENTERFACE_DEBUG=1` and share output
3. Verify all Wayland libraries installed in Docker
4. Check that Dockerfile includes the required packages

---

**Status:** ✅ Ready to deploy
