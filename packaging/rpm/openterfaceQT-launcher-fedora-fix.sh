#!/bin/bash
# OpenterfaceQT Launcher - Fedora Qt Conflict Resolution
# Aggressively overrides LD_LIBRARY_PATH to prevent system Qt from loading

set -e

# ============================================
# Logging Setup
# ============================================
LAUNCHER_LOG="/tmp/openterfaceqt-launcher-$(date +%s).log"
{
    echo "=== OpenterfaceQT Launcher Started at $(date) ==="
    echo "Script PID: $$"
    echo "Arguments: $@"
} | tee "$LAUNCHER_LOG"

# ============================================
# CRITICAL: Override Library Search Paths
# ============================================
# The KEY insight: LD_LIBRARY_PATH takes precedence over RPATH and default paths
# By setting it EXCLUSIVELY to bundled paths FIRST, we force the linker to find
# our Qt libraries before it ever checks /lib64 or /usr/lib64

echo "Setting up Qt6 environment..." | tee -a "$LAUNCHER_LOG"

# Build the new LD_LIBRARY_PATH with BUNDLED paths FIRST
# This ensures bundled Qt6.6.3 is found before system Qt6.9
BUNDLED_PATHS="/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt"

# Add back system paths for non-Qt libraries (glibc, etc)
# But they come AFTER our bundled paths, so they have lower priority
SYSTEM_PATHS="/lib64:/usr/lib64:/lib:/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu/lib"

# Combine them: bundled paths first (HIGH priority), then system paths (LOW priority)
export LD_LIBRARY_PATH="${BUNDLED_PATHS}:${SYSTEM_PATHS}"

# ============================================
# Force Early Symbol Resolution
# ============================================
# LD_BIND_NOW=1 tells the linker to resolve all symbols at load time
# This catches version mismatches immediately with clear error messages
export LD_BIND_NOW=1

# ============================================
# Qt Plugin Paths
# ============================================
# Use bundled plugins if available, fall back to system
if [ -d "/usr/lib/openterfaceqt/qt6/plugins" ]; then
    export QT_PLUGIN_PATH="/usr/lib/openterfaceqt/qt6/plugins"
elif [ -z "$QT_PLUGIN_PATH" ]; then
    export QT_PLUGIN_PATH="/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins"
fi

# ============================================
# QML Import Paths
# ============================================
if [ -d "/usr/lib/openterfaceqt/qt6/qml" ]; then
    export QML2_IMPORT_PATH="/usr/lib/openterfaceqt/qt6/qml"
elif [ -z "$QML2_IMPORT_PATH" ]; then
    export QML2_IMPORT_PATH="/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml"
fi

# ============================================
# GStreamer Plugin Path
# ============================================
if [ -z "$GST_PLUGIN_PATH" ]; then
    export GST_PLUGIN_PATH="/usr/lib/openterfaceqt/gstreamer"
fi

# ============================================
# Preload Qt Version Wrapper
# ============================================
# The wrapper intercepts dlopen() calls to redirect system Qt to bundled Qt
WRAPPER_LIB="/usr/lib/openterfaceqt/qt_version_wrapper.so"
if [ -f "$WRAPPER_LIB" ]; then
    export LD_PRELOAD="$WRAPPER_LIB"
    echo "✅ Qt Version Wrapper preloaded" | tee -a "$LAUNCHER_LOG"
else
    echo "⚠️  Qt Version Wrapper not found (may still work with LD_LIBRARY_PATH)" | tee -a "$LAUNCHER_LOG"
fi

# ============================================
# Debug Output
# ============================================
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    {
        echo ""
        echo "========================================" 
        echo "Environment Configuration (Debug Mode)" 
        echo "========================================" 
        echo "LD_LIBRARY_PATH (bundled first):"
        echo "  ${BUNDLED_PATHS}"
        echo ""
        echo "LD_LIBRARY_PATH (system paths after):"
        echo "  ${SYSTEM_PATHS}"
        echo ""
        echo "LD_BIND_NOW=$LD_BIND_NOW"
        echo "LD_PRELOAD=$LD_PRELOAD"
        echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
        echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH"
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
        echo ""
        echo "System Qt6 libraries (should NOT be used):" 
        find /lib64 /usr/lib64 -name "libQt6Core*.so*" 2>/dev/null | head -3 || echo "(checking...)"
        echo ""
        echo "Bundled Qt6 libraries (should be used):" 
        ls -1 /usr/lib/openterfaceqt/qt6/libQt6Core*.so* 2>/dev/null | head -3 || echo "(not found)"
        echo "========================================" 
    } | tee -a "$LAUNCHER_LOG"
fi

# ============================================
# Binary Execution
# ============================================

# Find the application binary
OPENTERFACE_BIN=""
for bin_path in \
    "/usr/bin/openterfaceQT-bin" \
    "/usr/local/bin/openterfaceQT-bin" \
    "/opt/openterface/bin/openterfaceQT" \
    "/opt/openterface/bin/openterfaceQT-bin"; do
    if [ -f "$bin_path" ] && [ -x "$bin_path" ]; then
        OPENTERFACE_BIN="$bin_path"
        break
    fi
done

if [ -z "$OPENTERFACE_BIN" ]; then
    {
        echo "ERROR: OpenterfaceQT binary not found" >&2
        echo "Searched:" >&2
        echo "  - /usr/bin/openterfaceQT-bin" >&2
        echo "  - /usr/local/bin/openterfaceQT-bin" >&2
        echo "  - /opt/openterface/bin/openterfaceQT" >&2
        echo "  - /opt/openterface/bin/openterfaceQT-bin" >&2
    } | tee -a "$LAUNCHER_LOG"
    exit 1
fi

{
    echo ""
    echo "Executing: $OPENTERFACE_BIN $@"
    echo "Launcher log: $LAUNCHER_LOG"
    echo ""
} | tee -a "$LAUNCHER_LOG"

# Application log file
APP_LOG="/tmp/openterfaceqt-app-$(date +%s).log"
{
    echo "=== OpenterfaceQT Application Started at $(date) ===" 
    echo "Binary: $OPENTERFACE_BIN"
    echo "LD_LIBRARY_PATH (first):"
    echo "  $BUNDLED_PATHS"
    echo "LD_PRELOAD=$LD_PRELOAD"
    echo "LD_BIND_NOW=$LD_BIND_NOW"
    echo ""
} > "$APP_LOG" 2>&1

# Execute the binary
# Use exec to replace the shell process - this is CRITICAL!
# Without exec, the environment might not be properly inherited
exec "$OPENTERFACE_BIN" "$@" 2>&1 | tee -a "$APP_LOG"
