# Fix: Launcher Script Permission Denied Error

## Problem
When creating the launcher script, you were getting:
```
/tmp/install-openterface-shared.sh: line 394: /usr/local/bin/start-openterface.sh: Permission denied
start-openterface.sh: Permission denied
```

## Root Cause
The installation script was trying to create and modify files in `/usr/local/bin/` as a non-root user, which requires root privileges:
1. `cat > /usr/local/bin/start-openterface.sh` - Failed (permission denied)
2. `chmod +x /usr/local/bin/start-openterface.sh` - Failed (file not created)

## Solution

### Fix 1: Use sudo for File Creation in Install Script

**File:** `docker/install-openterface-shared.sh`

**Change:**
```bash
# OLD - Failed without sudo
cat > /usr/local/bin/start-openterface.sh << 'EOF'
...
EOF
chmod +x /usr/local/bin/start-openterface.sh

# NEW - Uses sudo for privileged operations
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
else
    SUDO=""
fi

$SUDO bash -c 'cat > /usr/local/bin/start-openterface.sh << '"'"'EOF'"'"'
...
EOF'

$SUDO chmod +x /usr/local/bin/start-openterface.sh
```

**Why this works:**
- Detects if running as non-root user
- Uses `sudo` to run `cat` with root privileges
- `bash -c` wrapper allows complex heredoc with `sudo`
- Quote escaping preserves the script inside the heredoc

### Fix 2: Improved App Launch in Screenshot Script

**File:** `docker/screenshot-docker-app.sh`

**Changes:**
- Moved `export DISPLAY` before installation
- Added small delay after installation (2 seconds)
- Direct app launch (no launcher script - simpler path)
- Better error diagnostics

```bash
# Environment setup first
export DISPLAY=$DISPLAY QT_X11_NO_MITSHM=1 QT_QPA_PLATFORM=xcb

# Then installation
/tmp/install-openterface-shared.sh

# Then small delay
sleep 2

# Then app launch
exec /usr/local/bin/openterfaceQT

# Error handling with diagnostics
ls -la /usr/local/bin/openterface*
which openterfaceQT
/usr/local/bin/openterfaceQT --version
```

## Testing the Fix

```bash
# 1. Rebuild Docker image (no cache)
docker rmi openterface-test-shared:* 2>/dev/null || true

# 2. Run screenshot test
./docker/screenshot-docker-app.sh
```

### Expected Output

#### Success ✅
```
🚀 Creating launcher script...
✅ Launcher script created at /usr/local/bin/start-openterface.sh
🎉 Installation Summary
======================
✅ Openterface QT version: 
✅ Runtime dependencies: Installed
✅ Device permissions: Configured
✅ Launcher script: /usr/local/bin/start-openterface.sh

🚀 Ready for testing!
✅ Installation completed successfully!
```

#### Error Resolution ✅
If app launch still fails:
```
App launch failed - checking details...
-rwxr-xr-x 1 root root 27867400 /usr/local/bin/openterfaceQT
/usr/local/bin/openterfaceQT
Binary exists but cannot run (might need display)
```

## Key Technical Details

### Why Heredoc Escaping is Complex

When using `sudo bash -c` with a heredoc:

```bash
# This doesn't work:
sudo bash -c 'cat > /usr/local/bin/script.sh << EOF
...
EOF'

# This works (quotes are escaped):
sudo bash -c 'cat > /usr/local/bin/script.sh << '"'"'EOF'"'"'
...
EOF'
```

**Breaking down the quote escaping:**
1. `'...'` - Single-quoted string (most of the bash code)
2. `'"'"'` - Exit single quotes, add escaped single quote, re-enter single quotes
3. `EOF` - The heredoc delimiter (now part of the content, not the shell)

### Why Direct App Launch Works

In Docker containers for screenshot testing:
- The launcher script is designed for manual/interactive use
- It sets up USB device permissions (not needed for GUI test)
- It includes udev management (overkill for container)
- Direct app launch is simpler and faster

### When to Use the Launcher Script

Use the launcher script when:
1. Running Openterface interactively
2. You need actual device permissions set up
3. You're in a system with real USB devices
4. You want centralized device permission management

Don't use it when:
1. Testing in Docker containers (no real devices)
2. You just need the GUI to render
3. You're in a headless/CI environment
4. Performance is critical

## Debugging Tips

### If launcher script still has permission issues:
```bash
# Check if file was created
docker exec <container> ls -la /usr/local/bin/start-openterface.sh

# Check permissions
docker exec <container> stat /usr/local/bin/start-openterface.sh

# Try running it
docker exec <container> /usr/local/bin/start-openterface.sh 2>&1 | head -10
```

### If app still doesn't launch:
```bash
# Check binary exists and is executable
docker exec <container> ls -la /usr/local/bin/openterfaceQT

# Check if it's a valid ELF binary
docker exec <container> file /usr/local/bin/openterfaceQT

# Check dependencies
docker exec <container> ldd /usr/local/bin/openterfaceQT | grep -i "not found"

# Try running directly
docker exec <container> /usr/local/bin/openterfaceQT 2>&1 | head -5
```

## Related Fixes

This fix is part of a series of permission-related improvements:

1. **Dockerfile Sudoers Setup** - Allows non-root user to use sudo
2. **apt Directory Permissions** - Pre-creates and owns apt directories
3. **Installation Script sudo Usage** - Uses sudo for dpkg/apt-get
4. **File Creation with sudo** - This fix
5. **App Launch** - Final piece of the puzzle

## Validation Checklist

- [ ] Docker image rebuilt without cache
- [ ] Installation completes without "Permission denied" errors
- [ ] Launcher script is created at `/usr/local/bin/start-openterface.sh`
- [ ] Launcher script has execute permissions (755)
- [ ] App launches without errors
- [ ] Screenshot captures full GUI content
- [ ] Container completes successfully

## Files Modified

1. `docker/install-openterface-shared.sh`
   - Updated `create_launcher()` function
   - Now uses sudo for file creation
   - Proper quote escaping for heredoc

2. `docker/screenshot-docker-app.sh`
   - Improved bash -c command
   - Better environment setup order
   - Enhanced error diagnostics

## Performance Impact

- Minimal (additional `sudo` calls are < 100ms)
- File creation is still atomic
- No performance degradation

## Backwards Compatibility

- ✅ Still creates launcher script properly
- ✅ Can be used later if needed
- ✅ All paths preserved
- ✅ No breaking changes
