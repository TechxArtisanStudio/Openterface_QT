#!/bin/bash
# OpenterfaceQT Launcher - Simplified for Fedora Qt Conflict Resolution
# Uses Qt Version Wrapper to intercept dlopen() calls
# This replaces complex LD_PRELOAD lists with a smart dlopen() interceptor

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
# Environment Setup
# ============================================

echo "Setting up Qt6 environment..." | tee -a "$LAUNCHER_LOG"

# 1. Set library search paths (bundled libraries first)
export LD_LIBRARY_PATH="/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt:${LD_LIBRARY_PATH}"

# 2. CRITICAL: Force all symbols to be resolved at load time
#    This catches version mismatches early and gives better error messages
export LD_BIND_NOW=1

# 3. Preload the Qt Version Wrapper FIRST (before any Qt libraries)
#    This wrapper intercepts dlopen() to prevent system Qt from loading
WRAPPER_LIB="/usr/lib/openterfaceqt/qt_version_wrapper.so"
if [ -f "$WRAPPER_LIB" ]; then
    export LD_PRELOAD="$WRAPPER_LIB"
    echo "✅ Qt Version Wrapper loaded: $WRAPPER_LIB" | tee -a "$LAUNCHER_LOG"
else
    echo "⚠️  Qt Version Wrapper not found: $WRAPPER_LIB" | tee -a "$LAUNCHER_LOG"
    echo "    Application may fail on systems with system Qt6 (e.g., Fedora)" | tee -a "$LAUNCHER_LOG"
fi

# 4. Qt plugin paths (for platform integration, styles, etc.)
if [ -z "$QT_PLUGIN_PATH" ]; then
    export QT_PLUGIN_PATH="/usr/lib/openterfaceqt/qt6/plugins"
fi

# 5. QML module paths (for Quick, Controls, etc.)
if [ -z "$QML2_IMPORT_PATH" ]; then
    export QML2_IMPORT_PATH="/usr/lib/openterfaceqt/qt6/qml"
fi

# 6. GStreamer plugin paths
if [ -z "$GST_PLUGIN_PATH" ]; then
    export GST_PLUGIN_PATH="/usr/lib/openterfaceqt/gstreamer"
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
        echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" 
        echo "LD_PRELOAD=$LD_PRELOAD" 
        echo "LD_BIND_NOW=$LD_BIND_NOW" 
        echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" 
        echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" 
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" 
        echo ""
        echo "System Qt6 libraries (if any):" 
        find /lib64 /usr/lib64 -name "libQt6*.so*" 2>/dev/null | head -5 || echo "(none found)"
        echo ""
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
    echo "Environment:"
    echo "  LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    echo "  LD_PRELOAD=$LD_PRELOAD"
    echo "  LD_BIND_NOW=$LD_BIND_NOW"
    echo ""
} > "$APP_LOG" 2>&1

# Execute the binary
# Use exec to replace the shell process - ensures environment is inherited cleanly
exec "$OPENTERFACE_BIN" "$@" 2>&1 | tee -a "$APP_LOG"
