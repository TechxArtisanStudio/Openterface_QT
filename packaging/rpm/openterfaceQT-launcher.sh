#!/bin/bash
# OpenterfaceQT Launcher - Sets up bundled library paths
# This script ensures bundled Qt6, FFmpeg, and GStreamer libraries are loaded with proper priority
set -e

# ============================================
# Library Path Setup (CRITICAL for bundled libs)
# ============================================
# Ensure bundled Qt6 and FFmpeg libraries are found FIRST (before system libraries)
# This guarantees we use our bundled versions, not system versions

# Bundled library paths in priority order
BUNDLED_LIB_PATHS=(
    "/usr/lib/openterfaceqt/qt6"
    "/usr/lib/openterfaceqt/ffmpeg"
    "/usr/lib/openterfaceqt/gstreamer"
    "/usr/lib/openterfaceqt"
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
# Qt Plugin Path Setup
# ============================================
if [ -z "$QT_PLUGIN_PATH" ]; then
    QT_PLUGIN_PATHS=()
    
    # Check bundled location first
    if [ -d "/usr/lib/openterfaceqt/qt6/plugins" ]; then
        QT_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/qt6/plugins")
    fi
    
    # Add system locations as fallback
    if [ -d "/usr/lib/qt6/plugins" ]; then
        QT_PLUGIN_PATHS+=("/usr/lib/qt6/plugins")
    fi
    
    # Join with colons
    QT_PLUGIN_PATH=$(printf '%s:' "${QT_PLUGIN_PATHS[@]}" | sed 's/:$//')
    export QT_PLUGIN_PATH="${QT_PLUGIN_PATH}"
fi

# ============================================
# Qt Platform Plugin Path Setup (CRITICAL)
# ============================================
if [ -z "$QT_QPA_PLATFORM_PLUGIN_PATH" ]; then
    QT_PLATFORM_PLUGIN_PATHS=()
    
    # Check bundled location first
    if [ -d "/usr/lib/openterfaceqt/qt6/plugins/platforms" ]; then
        QT_PLATFORM_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/qt6/plugins/platforms")
    fi
    
    # Add system locations as fallback
    if [ -d "/usr/lib/qt6/plugins/platforms" ]; then
        QT_PLATFORM_PLUGIN_PATHS+=("/usr/lib/qt6/plugins/platforms")
    fi
    
    # Join with colons
    QT_QPA_PLATFORM_PLUGIN_PATH=$(printf '%s:' "${QT_PLATFORM_PLUGIN_PATHS[@]}" | sed 's/:$//')
    export QT_QPA_PLATFORM_PLUGIN_PATH="${QT_QPA_PLATFORM_PLUGIN_PATH}"
fi

# ============================================
# QML Import Path Setup
# ============================================
if [ -z "$QML2_IMPORT_PATH" ]; then
    QML_IMPORT_PATHS=()
    
    # Check bundled location first
    if [ -d "/usr/lib/openterfaceqt/qt6/qml" ]; then
        QML_IMPORT_PATHS+=("/usr/lib/openterfaceqt/qt6/qml")
    fi
    
    # Add system locations as fallback
    if [ -d "/usr/lib/qt6/qml" ]; then
        QML_IMPORT_PATHS+=("/usr/lib/qt6/qml")
    fi
    
    # Join with colons
    QML2_IMPORT_PATH=$(printf '%s:' "${QML_IMPORT_PATHS[@]}" | sed 's/:$//')
    export QML2_IMPORT_PATH="${QML2_IMPORT_PATH}"
fi

# ============================================
# GStreamer Plugin Path Setup
# ============================================
if [ -z "$GST_PLUGIN_PATH" ]; then
    GST_PLUGIN_PATHS=()
    
    # Bundled GStreamer plugins (PRIMARY)
    if [ -d "/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0")
    fi
    
    # System GStreamer plugins (secondary)
    if [ -d "/usr/lib/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/gstreamer-1.0")
    fi
    
    # Join with colons
    GST_PLUGIN_PATH=$(printf '%s:' "${GST_PLUGIN_PATHS[@]}" | sed 's/:$//')
    export GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"
fi

# ============================================
# Debug Mode
# ============================================
# Set OPENTERFACE_DEBUG=1 to see environment variables being used
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    echo "========================================" >&2
    echo "OpenterfaceQT Runtime Environment Setup" >&2
    echo "========================================" >&2
    echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" >&2
    echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" >&2
    echo "QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH" >&2
    echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" >&2
    echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" >&2
    echo "========================================" >&2
fi

# ============================================
# Application Execution
# ============================================
# Execute the main binary with all passed arguments
exec /usr/bin/openterfaceQT "$@"
