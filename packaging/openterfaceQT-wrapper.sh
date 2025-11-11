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
# Prioritize bundled libraries to avoid symbol conflicts with system libraries
# This prevents errors like: "undefined symbol: _gst_meta_tag_memory_reference"

BUNDLED_LIB_PATHS=(
    "/usr/lib/openterfaceqt/qt6"        # Bundled Qt6 libraries (highest priority)
    "/usr/lib/openterfaceqt"            # Bundled libraries (FFmpeg, GStreamer, etc.)
)

# Add bundled library paths first (highest priority)
if [ -z "$LD_LIBRARY_PATH" ]; then
    # Build initial path from bundled paths
    LD_LIBRARY_PATH=""
    for lib_path in "${BUNDLED_LIB_PATHS[@]}"; do
        if [ -d "$lib_path" ]; then
            if [ -z "$LD_LIBRARY_PATH" ]; then
                LD_LIBRARY_PATH="${lib_path}"
            else
                LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${lib_path}"
            fi
        fi
    done
else
    # Prepend bundled paths to existing LD_LIBRARY_PATH (in reverse order for correct priority)
    for ((i=${#BUNDLED_LIB_PATHS[@]}-1; i>=0; i--)); do
        lib_path="${BUNDLED_LIB_PATHS[$i]}"
        if [ -d "$lib_path" ]; then
            LD_LIBRARY_PATH="${lib_path}:${LD_LIBRARY_PATH}"
        fi
    done
fi

export LD_LIBRARY_PATH

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

# Try multiple locations for the binary
OPENTERFACE_BIN=""
for bin_path in \
    "/usr/local/bin/openterfaceQT" \
    "/usr/bin/openterfaceQT" \
    "/opt/openterface/bin/openterfaceQT" \
    "${SCRIPT_DIR}/openterfaceQT"; do
    if [ -f "$bin_path" ] && [ -x "$bin_path" ]; then
        OPENTERFACE_BIN="$bin_path"
        break
    fi
done

if [ -z "$OPENTERFACE_BIN" ]; then
    echo "Error: OpenterfaceQT binary not found in standard locations" >&2
    echo "Searched:" >&2
    echo "  - /usr/local/bin/openterfaceQT" >&2
    echo "  - /usr/bin/openterfaceQT" >&2
    echo "  - /opt/openterface/bin/openterfaceQT" >&2
    echo "  - ${SCRIPT_DIR}/openterfaceQT" >&2
    exit 1
fi

# Execute the binary with all passed arguments
exec "$OPENTERFACE_BIN" "$@"
