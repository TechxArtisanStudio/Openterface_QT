#!/bin/bash
# OpenterfaceQT Launcher Wrapper
# This script sets up the necessary environment variables before launching the application.
# It handles Qt plugins, QML imports, and GStreamer plugins.

set -e

# Script directory (where this wrapper is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" 2>/dev/null || SCRIPT_DIR="/usr/local/bin"

# Architecture detection
UNAME_M=$(uname -m)
case "${UNAME_M}" in
    x86_64|amd64)
        ARCH_DIR="x86_64-linux-gnu"
        ;;
    aarch64|arm64)
        ARCH_DIR="aarch64-linux-gnu"
        ;;
    *)
        ARCH_DIR="${UNAME_M}-linux-gnu"
        ;;
esac

# ============================================
# Library Path Setup (CRITICAL for bundled libs)
# ============================================
# MUST prioritize bundled libraries to override system libraries
# This is essential because the binary may have RPATH pointing to system Qt6
# We need to ensure bundled libraries are loaded FIRST

BUNDLED_LIB_PATHS=(
    "/usr/lib/openterfaceqt/qt6"        # Bundled Qt6 libraries (HIGHEST priority)
    "/usr/lib/openterfaceqt"            # Bundled libraries (FFmpeg, GStreamer, etc.)
)

# Build LD_LIBRARY_PATH with bundled libraries at the front
LD_LIBRARY_PATH_NEW=""
for lib_path in "${BUNDLED_LIB_PATHS[@]}"; do
    if [ -d "$lib_path" ]; then
        if [ -z "$LD_LIBRARY_PATH_NEW" ]; then
            LD_LIBRARY_PATH_NEW="${lib_path}"
        else
            LD_LIBRARY_PATH_NEW="${LD_LIBRARY_PATH_NEW}:${lib_path}"
        fi
    fi
done

# Append existing LD_LIBRARY_PATH (if any)
if [ -n "$LD_LIBRARY_PATH" ]; then
    LD_LIBRARY_PATH_NEW="${LD_LIBRARY_PATH_NEW}:${LD_LIBRARY_PATH}"
fi

export LD_LIBRARY_PATH="${LD_LIBRARY_PATH_NEW}"

# ============================================
# LD_PRELOAD Setup (MOST CRITICAL - Override binary's RPATH)
# ============================================
# CRITICAL: The binary was compiled with RPATH pointing to system Qt libraries.
# LD_PRELOAD must force bundled Qt libraries to load FIRST before any others.
# This is THE KEY to avoiding "version `Qt_6.6' not found" errors.

PRELOAD_LIBS=()

# Qt6 core libraries - MUST be preloaded in correct order
# The order is critical: Core first, then Gui, then everything else
QT6_CORE_LIBS=(
    "libQt6Core.so.6"      # MUST be first
    "libQt6Gui.so.6"       # Must be before other modules
)

# Qt6 module libraries
QT6_MODULE_LIBS=(
    "libQt6Widgets.so.6"
    "libQt6Multimedia.so.6"
    "libQt6MultimediaWidgets.so.6"
    "libQt6SerialPort.so.6"
    "libQt6Network.so.6"
    "libQt6OpenGL.so.6"
    "libQt6Xml.so.6"
    "libQt6Concurrent.so.6"
    "libQt6DBus.so.6"
    "libQt6Svg.so.6"
    "libQt6Quick.so.6"
    "libQt6Qml.so.6"
    "libQt6QuickWidgets.so.6"
    "libQt6PrintSupport.so.6"
)

# Load core libraries first
for lib in "${QT6_CORE_LIBS[@]}"; do
    lib_path="/usr/lib/openterfaceqt/qt6/$lib"
    if [ -f "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    fi
done

# Then load module libraries
for lib in "${QT6_MODULE_LIBS[@]}"; do
    lib_path="/usr/lib/openterfaceqt/qt6/$lib"
    if [ -f "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    fi
done

# Build LD_PRELOAD string with proper precedence
if [ ${#PRELOAD_LIBS[@]} -gt 0 ]; then
    PRELOAD_STR=$(IFS=':'; echo "${PRELOAD_LIBS[*]}")
    # PREPEND to any existing LD_PRELOAD to ensure our libs take priority
    if [ -z "$LD_PRELOAD" ]; then
        export LD_PRELOAD="$PRELOAD_STR"
    else
        export LD_PRELOAD="$PRELOAD_STR:$LD_PRELOAD"
    fi
    
    # Debug: Show what we're preloading
    if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
        echo "LD_PRELOAD (${#PRELOAD_LIBS[@]} libs): $LD_PRELOAD" >&2
    fi
else
    if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
        echo "⚠️  Warning: No Qt6 libraries found for LD_PRELOAD" >&2
    fi
fi

# ============================================
# Qt Plugin Path Setup
# ============================================
# Priority order:
# 1. User's existing QT_PLUGIN_PATH (if set)
# 2. Bundled Qt6 plugins in /usr/lib/qt6
# 3. System Qt6 plugins
# 4. Fallback to system architecture-specific path

if [ -z "$QT_PLUGIN_PATH" ]; then
    QT_PLUGIN_PATHS=()
    
    # Check bundled location first
    if [ -d "/usr/lib/openterfaceqt/qt6/plugins" ]; then
        QT_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/qt6/plugins")
    fi
    
    # Add system locations
    if [ -d "/usr/lib/${ARCH_DIR}/qt6/plugins" ]; then
        QT_PLUGIN_PATHS+=("/usr/lib/${ARCH_DIR}/qt6/plugins")
    fi
    
    if [ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins" ]; then
        QT_PLUGIN_PATHS+=("/usr/lib/x86_64-linux-gnu/qt6/plugins")
    fi
    
    if [ -d "/usr/lib/qt6/plugins" ]; then
        QT_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/qt6/plugins")
    fi
    
    # Join with colons
    IFS=':' read -ra QT_PLUGIN_PATH <<< "$(printf '%s:' "${QT_PLUGIN_PATHS[@]}" | sed 's/:$//')"
    export QT_PLUGIN_PATH="${QT_PLUGIN_PATH}"
fi

# ============================================
# Qt Platform Plugin Path Setup (CRITICAL)
# ============================================
# Explicitly set QT_QPA_PLATFORM_PLUGIN_PATH for platform plugin discovery
# This is essential for XCB and other platform plugins to load correctly

if [ -z "$QT_QPA_PLATFORM_PLUGIN_PATH" ]; then
    QT_PLATFORM_PLUGIN_PATHS=()
    
    # Check bundled location first (for installed DEB package)
    if [ -d "/usr/lib/openterfaceqt/qt6/plugins/platforms" ]; then
        QT_PLATFORM_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/qt6/plugins/platforms")
    fi
    
    # Add system locations
    if [ -d "/usr/lib/${ARCH_DIR}/qt6/plugins/platforms" ]; then
        QT_PLATFORM_PLUGIN_PATHS+=("/usr/lib/${ARCH_DIR}/qt6/plugins/platforms")
    fi
    
    if [ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms" ]; then
        QT_PLATFORM_PLUGIN_PATHS+=("/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms")
    fi
    
    if [ -d "/usr/lib/qt6/plugins/platforms" ]; then
        QT_PLATFORM_PLUGIN_PATHS+=("/usr/lib/qt6/plugins/platforms")
    fi
    
    # Join with colons
    IFS=':' read -ra QT_QPA_PLATFORM_PLUGIN_PATH <<< "$(printf '%s:' "${QT_PLATFORM_PLUGIN_PATHS[@]}" | sed 's/:$//')"
    export QT_QPA_PLATFORM_PLUGIN_PATH="${QT_QPA_PLATFORM_PLUGIN_PATH}"
fi

# ============================================
# QML Import Path Setup
# ============================================
# Priority order: bundled QML first, then system locations

if [ -z "$QML2_IMPORT_PATH" ]; then
    QML_IMPORT_PATHS=()
    
    # Check bundled location first
    if [ -d "/usr/lib/openterfaceqt/qt6/qml" ]; then
        QML_IMPORT_PATHS+=("/usr/lib/openterfaceqt/qt6/qml")
    fi
    
    # Add system locations
    if [ -d "/usr/lib/${ARCH_DIR}/qt6/qml" ]; then
        QML_IMPORT_PATHS+=("/usr/lib/${ARCH_DIR}/qt6/qml")
    fi
    
    if [ -d "/usr/lib/x86_64-linux-gnu/qt6/qml" ]; then
        QML_IMPORT_PATHS+=("/usr/lib/x86_64-linux-gnu/qt6/qml")
    fi
    
    if [ -d "/usr/lib/openterfaceqt/qt6/qml" ]; then
        QML_IMPORT_PATHS+=("/usr/lib/openterfaceqt/qt6/qml")
    fi
    
    # Join with colons
    IFS=':' read -ra QML2_IMPORT_PATH <<< "$(printf '%s:' "${QML_IMPORT_PATHS[@]}" | sed 's/:$//')"
    export QML2_IMPORT_PATH="${QML2_IMPORT_PATH}"
fi

# ============================================
# GStreamer Plugin Path Setup
# ============================================
# Critical for avoiding GStreamer symbol lookup errors
# Priority: system GStreamer plugins first

if [ -z "$GST_PLUGIN_PATH" ]; then
    GST_PLUGIN_PATHS=()
    
    # System GStreamer plugins (primary)
    if [ -d "/usr/lib/${ARCH_DIR}/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/${ARCH_DIR}/gstreamer-1.0")
    fi
    
    if [ -d "/usr/lib/x86_64-linux-gnu/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
    fi
    
    if [ -d "/usr/lib/openterfaceqt/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/gstreamer-1.0")
    fi
    
    # Bundled GStreamer plugins (secondary)
    if [ -d "/opt/gstreamer/lib/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/opt/gstreamer/lib/gstreamer-1.0")
    fi
    
    # Join with colons
    IFS=':' read -ra GST_PLUGIN_PATH <<< "$(printf '%s:' "${GST_PLUGIN_PATHS[@]}" | sed 's/:$//')"
    export GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"
fi

# ============================================
# GStreamer Codec Path Setup
# ============================================
# For GStreamer codec discovery

if [ -z "$GST_SCANNER_PATH" ]; then
    if [ -f "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/gst-plugin-scanner" ]; then
        export GST_SCANNER_PATH="/usr/lib/x86_64-linux-gnu/gstreamer-1.0/gst-plugin-scanner"
    elif [ -f "/usr/libexec/gstreamer-1.0/gst-plugin-scanner" ]; then
        export GST_SCANNER_PATH="/usr/libexec/gstreamer-1.0/gst-plugin-scanner"
    fi
fi

# ============================================
# Optional: Debug Mode
# ============================================
# Set OPENTERFACE_DEBUG=1 to see the environment variables being used

if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    echo "========================================" >&2
    echo "OpenterfaceQT Runtime Environment Setup" >&2
    echo "========================================" >&2
    echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" >&2
    echo "LD_PRELOAD=$LD_PRELOAD" >&2
    echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" >&2
    echo "QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH" >&2
    echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" >&2
    echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" >&2
    if [ -n "$GST_SCANNER_PATH" ]; then
        echo "GST_SCANNER_PATH=$GST_SCANNER_PATH" >&2
    fi
    echo "ARCH: $UNAME_M ($ARCH_DIR)" >&2
    echo "========================================" >&2
fi

# ============================================
# Application Execution
# ============================================
# Locate and execute the OpenterfaceQT binary
# NOTE: The binary is renamed to openterfaceQT.bin and wrapped by this script
# This ensures LD_PRELOAD and environment variables are ALWAYS applied

# Try multiple locations for the binary (prioritize the wrapped version)
OPENTERFACE_BIN=""
for bin_path in \
    "/usr/local/bin/openterfaceQT.bin" \
    "/usr/local/bin/openterfaceQT" \
    "/usr/bin/openterfaceQT" \
    "/opt/openterface/bin/openterfaceQT" \
    "${SCRIPT_DIR}/openterfaceQT.bin" \
    "${SCRIPT_DIR}/openterfaceQT"; do
    if [ -f "$bin_path" ] && [ -x "$bin_path" ]; then
        OPENTERFACE_BIN="$bin_path"
        break
    fi
done

if [ -z "$OPENTERFACE_BIN" ]; then
    echo "Error: OpenterfaceQT binary not found in standard locations" >&2
    echo "Searched:" >&2
    echo "  - /usr/local/bin/openterfaceQT.bin (wrapped version)" >&2
    echo "  - /usr/local/bin/openterfaceQT" >&2
    echo "  - /usr/bin/openterfaceQT" >&2
    echo "  - /opt/openterface/bin/openterfaceQT" >&2
    echo "  - ${SCRIPT_DIR}/openterfaceQT.bin" >&2
    echo "  - ${SCRIPT_DIR}/openterfaceQT" >&2
    exit 1
fi

# Debug: Show what will be preloaded
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    echo "Executing: $OPENTERFACE_BIN" >&2
fi

# Execute the binary with all passed arguments
exec "$OPENTERFACE_BIN" "$@"
