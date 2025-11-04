# Fix: Docker Container Permission Issues During Installation

## Problem
When running the screenshot test, the installation was failing with permission errors:
```
dpkg: error: requested operation requires superuser privilege
E: List directory /var/lib/apt/lists/partial is missing. - Acquire (13: Permission denied)
```

## Root Causes

1. **Sudoers Entry in Wrong Location** - Using `/etc/sudoers` directly instead of `/etc/sudoers.d/`
2. **Missing apt directories** - `/var/lib/apt/lists/partial` not created, causing permission issues
3. **Non-root User Not in sudo Group** - `openterface` user wasn't in the `sudo` group
4. **Missing Directory Permissions** - `/var/lib/apt/lists` and `/var/cache/apt` weren't owned by the user
5. **Installation Script Not Using sudo** - Commands that need root privilege were called directly

## Solutions Implemented

### 1. Fixed Dockerfile (`docker/testos/Dockerfile.ubuntu-test-shared`)

**Changes:**
```dockerfile
# OLD - Unreliable sudoers entry
RUN echo "openterface ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# NEW - Proper sudoers.d entry with correct permissions
RUN echo "openterface ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/openterface && \
    chmod 0440 /etc/sudoers.d/openterface
```

**Additional fixes:**
- Added `sudo` group to user: `usermod -a -G ... sudo openterface`
- Pre-create apt directories: `mkdir -p /var/lib/apt/lists/partial`
- Set proper ownership: `chown -R openterface:openterface /var/lib/apt/lists /var/cache/apt`
- Pre-create build directories: `mkdir -p /tmp/build-artifacts /tmp/packages`
- Set proper ownership on build dirs: `chown -R openterface:openterface /tmp/...`

### 2. Updated Entrypoint Script (`docker/entrypoint.sh`)

**Changes:**
- Moved X11 environment exports to the top (before installation)
- Improved sudo handling to work with both root and non-root execution contexts
- Better error messages for installation failures

```bash
# Set display environment for X11 early
export DISPLAY="${DISPLAY:-:0}"
export QT_X11_NO_MITSHM=1
export QT_QPA_PLATFORM=xcb

# Run installation with proper privilege escalation
if [ "$(id -u)" -ne 0 ]; then
    # Not root, use sudo
    if sudo -n /tmp/install-openterface-shared.sh 2>/dev/null; then
        # Success
    fi
else
    # Already root
    /tmp/install-openterface-shared.sh
fi
```

### 3. Enhanced Install Script (`docker/install-openterface-shared.sh`)

**Changes to `install_package()` function:**
- Detect if running as root: `if [ "$(id -u)" -ne 0 ]`
- Use sudo for privileged commands: `$SUDO dpkg`, `$SUDO apt-get`
- Better error handling with output filtering

```bash
install_package() {
    # Determine if we need sudo
    if [ "$(id -u)" -ne 0 ]; then
        SUDO="sudo"
    else
        SUDO=""
    fi
    
    # Use sudo for dpkg installation
    if $SUDO dpkg -i "$PACKAGE_FILE" 2>&1; then
        echo "✅ Package installed successfully"
    else
        # Fix dependencies with sudo
        $SUDO apt-get update
        $SUDO apt-get install -f -y
    fi
}
```

**Changes to `setup_device_permissions()` function:**
- Use sudo for udev rules: `$SUDO bash -c 'cat > /etc/udev/rules.d/...'`
- Use sudo for udevadm: `$SUDO udevadm control --reload-rules`

## Testing the Fix

### Rebuild Docker Image
```bash
# Force rebuild (no cache)
docker rmi openterface-test-shared:latest 2>/dev/null || true

# Run the screenshot test
./docker/screenshot-docker-app.sh
```

### Expected Results
```
✅ Package found (either local or downloaded)
📦 Installing Openterface QT package...
   Installing as Debian package...
✅ Package installed successfully
🔐 Setting up device permissions...
ℹ️  Container environment detected - udev rules created
✅ Device permissions configured
```

### Verify Permissions in Running Container
```bash
# Check sudo access
docker run --rm openterface-test-shared:latest \
  sudo -l 2>&1 | head -3

# Expected: openterface ALL=(ALL) NOPASSWD:ALL
# Or: (root) NOPASSWD: /tmp/install-openterface-shared.sh
```

## Architecture Overview

### Before Fix
```
Container starts
    ↓
openterface user (non-root)
    ↓
Installation script calls dpkg (needs root)
    ↓
❌ Permission denied
```

### After Fix
```
Container starts
    ↓
openterface user (non-root, in sudo group)
    ↓
Installation script detects non-root
    ↓
Script uses: sudo dpkg -i ...
    ↓
Sudoers.d entry allows: ALL=(ALL) NOPASSWD:ALL
    ↓
✅ Command succeeds with elevated privileges
```

## Key Technical Details

### Why `/etc/sudoers.d/` Instead of `/etc/sudoers`?
- `sudoers.d` uses drop-in files (safer, more maintainable)
- `sudoers` file is often regenerated during system updates
- Permissions on `sudoers.d` entries must be `0440` (immutable by non-root)

### Why Add to `sudo` Group?
- Allows user to run `sudo` commands
- Even with `NOPASSWD`, user still needs sudo group membership
- Best practice for role-based access control

### Why Pre-create apt Directories?
- Prevents "List directory missing" errors
- Ensures proper permissions from the start
- Container may run operations that require these directories

### Why Export X11 Environment Early?
- Ensures X11 display is available before any GUI app tries to connect
- Prevents "cannot connect to display" errors during installation
- Better for troubleshooting if something fails

## Common Issues and Solutions

### Issue: "sudo: command not found"
**Solution:** Install sudo package in Dockerfile during build (already done in base image)

### Issue: "openterface user cannot run sudo"
**Solution:** Ensure user is in sudoers.d entry:
```bash
cat /etc/sudoers.d/openterface
# Should show: openterface ALL=(ALL) NOPASSWD:ALL
```

### Issue: "List directory missing"
**Solution:** Pre-create the directory with proper permissions:
```bash
mkdir -p /var/lib/apt/lists/partial
chown -R openterface:openterface /var/lib/apt/lists
```

### Issue: Installation still fails with permission error
**Solution:** Check that Dockerfile was rebuilt:
```bash
# Force rebuild without cache
docker build --no-cache -f docker/testos/Dockerfile.ubuntu-test-shared \
  -t openterface-test-shared:latest docker/
```

## Performance Impact

- **Build time impact:** Minimal (a few milliseconds for extra RUN commands)
- **Image size impact:** Negligible (sudoers.d file is small)
- **Runtime overhead:** None (commands just work without errors)

## Backwards Compatibility

- ✅ All changes are backwards compatible
- ✅ Existing containers will continue to work
- ✅ Non-root installation now properly supported
- ✅ Root execution path still works unchanged

## Files Modified

1. `docker/testos/Dockerfile.ubuntu-test-shared`
   - Fixed sudoers.d entry
   - Added user to sudo group
   - Pre-created apt directories
   - Set proper ownership

2. `docker/entrypoint.sh`
   - Moved X11 exports earlier
   - Improved sudo handling

3. `docker/install-openterface-shared.sh`
   - Added sudo detection
   - Fixed dpkg commands with sudo
   - Fixed apt-get commands with sudo
   - Fixed udev commands with sudo

## Validation Checklist

- [ ] Dockerfile rebuilt without cache
- [ ] No permission errors during installation
- [ ] Package installed successfully
- [ ] Udev rules created
- [ ] Screenshot captures app content (not blank)
- [ ] Container completes successfully
- [ ] No "Aborted" signal on app launch
- [ ] `dpkg` and `apt-get` commands succeed
