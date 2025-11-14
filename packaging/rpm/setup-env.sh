#!/bin/bash
# OpenterfaceQT Environment Setup
# This script sets up the environment to exclude system Qt libraries and use only bundled versions
# 
# Usage: 
#   source /etc/profile.d/openterfaceqt.sh
#   # or
#   exec /usr/lib/openterfaceqt/setup-env.sh /path/to/app [args...]

OPENTERFACE_QT_PATH="${OPENTERFACE_QT_PATH:-/usr/lib/openterfaceqt}"

# ============================================
# Block system Qt library paths from being searched
# ============================================

# Remove system Qt paths from LD_LIBRARY_PATH to prevent conflicts
# This is a nuclear option - it completely removes system library paths
if [ -n "$LD_LIBRARY_PATH" ]; then
    LD_LIBRARY_PATH=$(echo "$LD_LIBRARY_PATH" | tr ':' '\n' | \
        grep -v "^/lib64$" | \
        grep -v "^/lib$" | \
        grep -v "^/usr/lib64$" | \
        grep -v "^/usr/lib$" | \
        grep -v "^/lib/x86_64-linux-gnu$" | \
        grep -v "^/usr/lib/x86_64-linux-gnu$" | \
        tr '\n' ':' | \
        sed 's/:$//')
fi

# ============================================
# Use only bundled libraries
# ============================================

# Set library path to ONLY use bundled libraries (more aggressive)
export LD_LIBRARY_PATH="$OPENTERFACE_QT_PATH/qt6:$OPENTERFACE_QT_PATH/ffmpeg:$OPENTERFACE_QT_PATH/gstreamer:$OPENTERFACE_QT_PATH:$LD_LIBRARY_PATH"

# Force all symbols to be resolved at load time (catches version mismatches early)
export LD_BIND_NOW=1

# Use bundled plugins exclusively
export QT_PLUGIN_PATH="$OPENTERFACE_QT_PATH/qt6/plugins"
export QML2_IMPORT_PATH="$OPENTERFACE_QT_PATH/qt6/qml"
export GST_PLUGIN_PATH="$OPENTERFACE_QT_PATH/gstreamer"

# Try to load the Qt version wrapper if available (filters dlopen calls)
if [ -f "$OPENTERFACE_QT_PATH/qt_version_wrapper.so" ]; then
    export LD_PRELOAD="$OPENTERFACE_QT_PATH/qt_version_wrapper.so:$LD_PRELOAD"
fi

# If running as a script with arguments, execute the command
if [ $# -gt 0 ]; then
    exec "$@"
fi
