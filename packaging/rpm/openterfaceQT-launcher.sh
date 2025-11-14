#!/bin/bash
# OpenterfaceQT Launcher - Sets up bundled library paths
# This script ensures bundled Qt6, FFmpeg, and GStreamer libraries are loaded with proper priority

# ============================================
# Error Handling & Logging
# ============================================
LAUNCHER_LOG="/tmp/openterfaceqt-launcher-$(date +%s).log"
{
    echo "=== OpenterfaceQT Launcher Started at $(date) ==="
    echo "Script PID: $$"
    echo "Arguments: $@"
} | tee "$LAUNCHER_LOG"

# Note: We use 'set +e' to allow graceful library lookups
# This prevents the script from exiting prematurely when libraries are not found

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
# System Library Path Filtering (Qt Version Isolation)
# ============================================
# CRITICAL FIX: On systems with system Qt6 (e.g., Fedora with Qt 6.9),
# prevent loading incompatible system Qt6 libraries that conflict with bundled Qt 6.6.3
# Use LD_BIND_NOW to catch version conflicts early
# This prevents libQt6QmlModels.so from the system pulling in incompatible symbols

export LD_BIND_NOW=1

# Optional: If system still loads conflicting Qt6, we can patch RPATH at runtime
# This tells the linker to prefer our bundled libraries over system RPATH entries
if [ -n "$LD_LIBRARY_PATH_RPATH_IGNORE" ]; then
    # Some glibc versions support this - try to use it
    export LD_LIBRARY_PATH_RPATH_IGNORE=1
fi

# ============================================
# LD_PRELOAD Setup (MOST CRITICAL - Override binary's RPATH)
# ============================================
# CRITICAL: The binary was compiled with RPATH pointing to system Qt libraries.
# LD_PRELOAD must force bundled Qt libraries to load FIRST before any others.
# This is THE KEY to avoiding "version `Qt_6.6' not found" errors.

# Step 1: Try to preload Qt version wrapper (filters system Qt versions)
WRAPPER_LIB="/usr/lib/openterfaceqt/qt_version_wrapper.so"
if [ -f "$WRAPPER_LIB" ]; then
    export LD_PRELOAD="$WRAPPER_LIB"
    if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
        echo "✅ Qt Version Wrapper loaded: $WRAPPER_LIB" >&2
    fi
else
    if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
        echo "⚠️  Qt Version Wrapper not found: $WRAPPER_LIB (system Qt libraries may conflict)" >&2
    fi
fi

PRELOAD_LIBS=()

# Qt6 core libraries - MUST be preloaded in correct order
# The order is critical: Core first, then Gui, then everything else
QT6_CORE_LIBS=(
    "libQt6Core"      # MUST be first
    "libQt6Gui"       # Must be before other modules
)

# Qt6 module libraries
QT6_MODULE_LIBS=(
    "libQt6Widgets"
    "libQt6Multimedia"
    "libQt6MultimediaWidgets"
    "libQt6SerialPort"
    "libQt6Network"
    "libQt6OpenGL"
    "libQt6Xml"
    "libQt6Concurrent"
    "libQt6DBus"
    "libQt6Svg"
    "libQt6Quick"
    "libQt6Qml"
    "libQt6QuickWidgets"
    "libQt6PrintSupport"
)

# Helper function to find library with any version suffix
find_library() {
    local lib_base="$1"
    local lib_dir="$2"
    local debug_mode="${3:-0}"
    
    if [ ! -d "$lib_dir" ]; then
        if [ "$debug_mode" = "1" ]; then
            echo "❌ Directory not found: $lib_dir (searching for $lib_base)" >&2
        fi
        return 1
    fi
    
    # Try to find the library with various version suffixes in priority order
    # Most specific versions first (e.g., .so.6.6.3), then generic versions
    local found_lib=""
    
    if [ "$debug_mode" = "1" ]; then
        echo "🔍 Searching for '$lib_base' in '$lib_dir'" >&2
    fi
    
    # Try exact library files (prefer versioned over generic)
    for pattern in "$lib_base.so.*" "$lib_base.so"; do
        if [ "$debug_mode" = "1" ]; then
            echo "   Trying pattern: $pattern" >&2
        fi
        # Use find instead of ls to avoid issues with globbing and spaces
        found_lib=$(find "$lib_dir" -maxdepth 1 -name "$pattern" -type f 2>/dev/null | head -n 1)
        if [ -n "$found_lib" ]; then
            if [ "$debug_mode" = "1" ]; then
                echo "   ✅ Found: $found_lib" >&2
            fi
            echo "$found_lib"
            return 0
        fi
    done
    
    if [ "$debug_mode" = "1" ]; then
        echo "   ⚠️  Library not found: $lib_base" >&2
    fi
    return 1
}

# Determine debug mode
DEBUG_MODE=0
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    DEBUG_MODE=1
fi

# Load core libraries first
echo "Loading Qt6 Core Libraries..." | tee -a "$LAUNCHER_LOG"
for lib in "${QT6_CORE_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/qt6" "$DEBUG_MODE")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
        echo "✅ Added to PRELOAD: $lib_path" | tee -a "$LAUNCHER_LOG"
    else
        echo "⚠️  Core library not found: $lib" | tee -a "$LAUNCHER_LOG"
    fi
done

# Then load module libraries
echo "Loading Qt6 Module Libraries..." | tee -a "$LAUNCHER_LOG"
for lib in "${QT6_MODULE_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/qt6" "$DEBUG_MODE")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
        echo "✅ Added to PRELOAD: $lib_path" | tee -a "$LAUNCHER_LOG"
    else
        echo "⚠️  Module library not found: $lib" | tee -a "$LAUNCHER_LOG"
    fi
done

# GStreamer libraries - essential for media handling
GSTREAMER_LIBS=(
    "libgstreamer-1.0"
    "libgstbase-1.0"
    "libgstapp-1.0"
    "libgstvideo-1.0"
    "libgstaudio-1.0"
    "libgstpbutils-1.0"
)

# Load GStreamer libraries
echo "Loading GStreamer Libraries..." | tee -a "$LAUNCHER_LOG"
for lib in "${GSTREAMER_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/gstreamer" "$DEBUG_MODE")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
        echo "✅ Added to PRELOAD: $lib_path" | tee -a "$LAUNCHER_LOG"
    else
        # Log missing libraries for debugging (suppress in non-debug mode)
        echo "⚠️  GStreamer library not found: $lib" | tee -a "$LAUNCHER_LOG"
    fi
done

# FFmpeg libraries - essential for video/audio encoding and decoding
FFMPEG_LIBS=(
    "libavformat"
    "libavcodec"
    "libavutil"
    "libswscale"
    "libswresample"
    "libavfilter"
    "libavdevice"
)

# Load FFmpeg libraries
echo "Loading FFmpeg Libraries..." | tee -a "$LAUNCHER_LOG"
for lib in "${FFMPEG_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/ffmpeg" "$DEBUG_MODE")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
        echo "✅ Added to PRELOAD: $lib_path" | tee -a "$LAUNCHER_LOG"
    else
        # Log missing libraries for debugging (suppress in non-debug mode)
        echo "⚠️  FFmpeg library not found: $lib" | tee -a "$LAUNCHER_LOG"
    fi
done

# Build LD_PRELOAD string with proper precedence
if [ ${#PRELOAD_LIBS[@]} -gt 0 ]; then
    PRELOAD_STR=$(IFS=':'; echo "${PRELOAD_LIBS[*]}")
    
    # PREPEND wrapper and bundled Qt6 libs to ensure our versions are used
    FINAL_PRELOAD="$PRELOAD_STR"
    
    # If wrapper exists, it must be FIRST to intercept all dlopen calls
    if [ -f "$WRAPPER_LIB" ]; then
        FINAL_PRELOAD="$WRAPPER_LIB:$FINAL_PRELOAD"
    fi
    
    # PREPEND to any existing LD_PRELOAD to ensure our libs take priority
    if [ -z "$LD_PRELOAD" ]; then
        export LD_PRELOAD="$FINAL_PRELOAD"
    else
        export LD_PRELOAD="$FINAL_PRELOAD:$LD_PRELOAD"
    fi
    
    # Debug: Show what we're preloading
    if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
        echo "LD_PRELOAD (${#PRELOAD_LIBS[@]} libs + wrapper): $LD_PRELOAD" | head -c 200 >&2
        echo "..." >&2
    fi
else
    if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
        echo "⚠️  Warning: No Qt6 libraries found for LD_PRELOAD" >&2
    fi
fi

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
# NOTE: Debug info is NOW ALWAYS logged to file (launcher.log) regardless of DEBUG flag
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    {
        echo "========================================" 
        echo "OpenterfaceQT Runtime Environment Setup" 
        echo "========================================" 
        echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" 
        echo "LD_PRELOAD=$LD_PRELOAD" 
        echo "LD_BIND_NOW=$LD_BIND_NOW" 
        echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" 
        echo "QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH" 
        echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" 
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" 
        echo ""
        echo "System Qt6 libraries (conflicts):" 
        find /lib64 /lib /usr/lib64 /usr/lib -name "libQt6*.so*" 2>/dev/null | head -20
        echo ""
        echo "Bundled Qt6 libraries (used):" 
        ls -la /usr/lib/openterfaceqt/qt6/libQt6*.so* 2>/dev/null | head -20
        echo "========================================" 
    } | tee -a "$LAUNCHER_LOG"
else
    # Log env vars to file even in non-debug mode for troubleshooting
    {
        echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" 
        echo "LD_PRELOAD=$LD_PRELOAD" 
        echo "LD_BIND_NOW=$LD_BIND_NOW" 
        echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" 
        echo "QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH" 
        echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" 
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" 
    } >> "$LAUNCHER_LOG"
fi

# ============================================
# Application Execution
# ============================================
# Locate and execute the OpenterfaceQT binary
# NOTE: The binary is at /usr/bin/openterfaceQT-bin (renamed to avoid symlink conflict)
# This ensures LD_PRELOAD and environment variables are ALWAYS applied

# Try multiple locations for the binary (with fallbacks)
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
        echo "ERROR: OpenterfaceQT binary not found in standard locations" >&2
        echo "Searched:" >&2
        echo "  - /usr/bin/openterfaceQT-bin" >&2
        echo "  - /usr/local/bin/openterfaceQT-bin" >&2
        echo "  - /opt/openterface/bin/openterfaceQT" >&2
        echo "  - /opt/openterface/bin/openterfaceQT-bin" >&2
        echo "" >&2
        echo "Launcher log: $LAUNCHER_LOG" >&2
    } | tee -a "$LAUNCHER_LOG"
    exit 1
fi

# Debug: Show what will be executed
{
    echo ""
    echo "Executing: $OPENTERFACE_BIN $@"
    echo "Launcher log location: $LAUNCHER_LOG"
    echo ""
} | tee -a "$LAUNCHER_LOG"

# Capture binary output and error for debugging
APP_LOG="/tmp/openterfaceqt-app-$(date +%s).log"
{
    echo "=== OpenterfaceQT Application Started at $(date) ===" 
    echo "Binary: $OPENTERFACE_BIN"
    echo "Environment Variables:"
    echo "  LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    echo "  LD_PRELOAD=$LD_PRELOAD"
    echo "  QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
    echo "  QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH"
    echo "  QML2_IMPORT_PATH=$QML2_IMPORT_PATH"
    echo "  GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
    echo ""
    echo "=== Application Output ===" 
} > "$APP_LOG" 2>&1

# Execute the binary with all passed arguments
# Redirect output to both log file and console for monitoring
"$OPENTERFACE_BIN" "$@" 2>&1 | tee -a "$APP_LOG" &
APP_PID=$!

{
    echo "Application started with PID: $APP_PID"
    echo "Application log: $APP_LOG"
} | tee -a "$LAUNCHER_LOG"

# Wait for application to finish and capture exit code
wait $APP_PID
APP_EXIT_CODE=$?

{
    echo ""
    echo "=== Application Exited ===" 
    echo "Exit Code: $APP_EXIT_CODE"
    echo "Time: $(date)"
} | tee -a "$LAUNCHER_LOG"

exit $APP_EXIT_CODE

