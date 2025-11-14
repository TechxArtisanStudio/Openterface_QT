#!/bin/bash
# OpenterfaceQT Launcher - Fedora Qt Conflict Resolution
# Uses LD_PRELOAD to force bundled Qt libraries to load instead of system Qt

# ============================================
# Error Handling & Logging
# ============================================
LAUNCHER_LOG="/tmp/openterfaceqt-launcher-$(date +%s).log"
{
    echo "=== OpenterfaceQT Launcher Started at $(date) ==="
    echo "Script PID: $$"
    echo "Arguments: $@"
} | tee "$LAUNCHER_LOG"

# Trap errors and log them (but don't use set -e to allow graceful library lookups)
# Only trap real errors, not expected return codes from find_library function
trap 'if [ $? -ne 1 ]; then echo "ERROR at line $LINENO: $BASH_COMMAND" | tee -a "$LAUNCHER_LOG"; fi' ERR

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
# LD_PRELOAD Setup (MOST CRITICAL - Override binary's RPATH)
# ============================================
# CRITICAL: The binary was compiled with RPATH pointing to system Qt libraries.
# LD_PRELOAD must force bundled Qt libraries to load FIRST before any others.
# This is THE KEY to avoiding "version `Qt_6_PRIVATE_API' not found" errors.

PRELOAD_LIBS=()

# Qt6 core libraries - MUST be preloaded in correct order
# The order is critical: Core first, then Gui, then everything else
QT6_CORE_LIBS=(
    "libQt6Core"      # MUST be first
    "libQt6Gui"       # Must be before other modules
)

# Qt6 module libraries - COMPREHENSIVE list including all possible Qt6 libraries
# This ensures no system Qt libraries can sneak in through hidden dependencies
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
    "libQt6QmlModels"          # CRITICAL: Prevents system Qt6 QmlModels from loading
    "libQt6QmlWorkerScript"    # QML worker script support
    "libQt6Test"               # Testing support
    "libQt6Sql"                # SQL module
    "libQt6Positioning"        # Positioning/GPS
    "libQt6Location"           # Location services
    "libQt6Sensors"            # Sensor support
    "libQt6Bluetooth"          # Bluetooth support
    "libQt6WebSockets"         # WebSocket support
    "libQt6Nfc"                # NFC support
    "libQt6Pdf"                # PDF support
    "libQt6PdfWidgets"         # PDF widgets
    "libQt6Core5Compat"        # Qt5 compatibility layer
    "libQt6Accessibility"      # Accessibility support
    "libQt6ShaderTools"        # Shader compilation tools
    "libQt6Scxml"              # SCXML state machines
    "libQt6StateMachine"       # State machine framework
    "libQt6Designer"           # Designer plugin support
    "libQt6DesignerComponents"
    "libQt6VirtualKeyboard"    # Virtual keyboard
    "libQt6InputMethodSubprocess"
    # ========== CRITICAL PLATFORM PLUGINS ==========
    "libQt6XcbQpa"             # CRITICAL: X11/XCB platform support (required by xcb plugin)
    "libQt6WaylandClient"      # Wayland client support
    "libQt6WaylandEgl"         # Wayland EGL support
    "libQt6EglFsDeviceIntegration"  # EGLFS device integration
    "libQt6GlxIntegration"     # GLX integration
)

# Helper function to find library with any version suffix
find_library() {
    local lib_base="$1"
    local lib_dir="$2"
    
    if [ ! -d "$lib_dir" ]; then
        return 1
    fi
    
    # Try to find the library with various version suffixes in priority order
    # Most specific versions first (e.g., .so.6.6.3), then generic versions
    local found_lib=""
    
    # Try exact library files (prefer versioned over generic)
    for pattern in "$lib_base.so.*" "$lib_base.so"; do
        # Use find instead of ls to avoid issues with globbing and spaces
        found_lib=$(find "$lib_dir" -maxdepth 1 -name "$pattern" -type f 2>/dev/null | head -n 1)
        if [ -n "$found_lib" ]; then
            echo "$found_lib"
            return 0
        fi
    done
    
    return 1
}

# Load core libraries first
for lib in "${QT6_CORE_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/qt6")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    fi
done

# Then load module libraries
for lib in "${QT6_MODULE_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/qt6")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
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
for lib in "${GSTREAMER_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/gstreamer")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    else
        # Log missing libraries for debugging (suppress in non-debug mode)
        if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
            echo "⚠️  GStreamer library not found: $lib" >&2
        fi
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
for lib in "${FFMPEG_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/ffmpeg")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    else
        # Log missing libraries for debugging (suppress in non-debug mode)
        if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
            echo "⚠️  FFmpeg library not found: $lib" >&2
        fi
    fi
done

# ============================================
# Platform Support Libraries (CRITICAL!)
# ============================================
# These are needed by platform plugins (xcb, wayland, etc)
# Try to find them in bundled first, then system
PLATFORM_SUPPORT_LIBS=(
    "libQt6XcbQpa"             # X11/XCB support
    "libQt6WaylandClient"      # Wayland support
    "libQt6WaylandEgl"         # Wayland EGL
    "libQt6EglFsDeviceIntegration"  # EGLFS
    "libQt6GlxIntegration"     # GLX
)

# Search for platform libraries in bundled location first, then system
for lib in "${PLATFORM_SUPPORT_LIBS[@]}"; do
    # Try bundled first
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/qt6")
    
    # If not in bundled, try system location (as fallback)
    if [ -z "$lib_path" ]; then
        lib_path=$(find_library "$lib" "/lib64")
        if [ -z "$lib_path" ]; then
            lib_path=$(find_library "$lib" "/usr/lib64")
        fi
    fi
    
    # Add to preload if found
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    else
        if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
            echo "⚠️  Platform library not found: $lib" >&2
        fi
    fi
done

# ============================================
# X11/XCB Support Libraries
# ============================================
# Required by xcb platform plugin to connect to X11 display
XCB_SUPPORT_LIBS=(
    "libxcb"
    "libX11"
    "libxcb-cursor"
    "libxcb-render"
    "libxcb-xkb"
    "libxkbcommon"
    "libxkbcommon-x11"
)

# Search for XCB libraries in system locations (not bundled)
for lib in "${XCB_SUPPORT_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/lib64")
    if [ -z "$lib_path" ]; then
        lib_path=$(find_library "$lib" "/usr/lib64")
    fi
    
    # Add to preload if found
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    else
        if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
            echo "⚠️  XCB library not found: $lib" >&2
        fi
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
    
    # Always log comprehensive preload info
    {
        echo "========================================"
        echo "LD_PRELOAD Configuration (${#PRELOAD_LIBS[@]} libraries)"
        echo "========================================"
        for ((i=0; i<${#PRELOAD_LIBS[@]}; i++)); do
            echo "  [$((i+1))/${#PRELOAD_LIBS[@]}] ${PRELOAD_LIBS[$i]}"
        done
        echo "========================================"
    } | tee -a "$LAUNCHER_LOG"
else
    {
        echo "⚠️  Warning: No Qt6 libraries found for LD_PRELOAD" >&2
    } | tee -a "$LAUNCHER_LOG"
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
# Qt Platform Hints
# ============================================
# Try to detect available platform and set appropriate hint
# Common platforms: xcb (X11), wayland, offscreen, linuxfb
if [ -z "$QT_QPA_PLATFORM" ]; then
    # Check if we're in a graphical environment
    if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
        # No display - use offscreen rendering (safe fallback)
        export QT_QPA_PLATFORM="offscreen"
    else
        # Try to detect from environment
        if [ -n "$WAYLAND_DISPLAY" ]; then
            export QT_QPA_PLATFORM="wayland"
        elif [ -n "$DISPLAY" ]; then
            export QT_QPA_PLATFORM="xcb"
        fi
    fi
fi

# ============================================
# Debug Mode
# ============================================
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    {
        echo "========================================" 
        echo "OpenterfaceQT Runtime Environment Setup" 
        echo "========================================" 
        echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" 
        echo "LD_PRELOAD=$LD_PRELOAD" 
        echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" 
        echo "QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH"
        echo "QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
        echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" 
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" 
        echo "========================================"
        echo ""
        echo "System Qt6 Libraries (should NOT be used):"
        find /lib64 /usr/lib64 -name "libQt6*.so*" -type f 2>/dev/null | head -10 || echo "(none)"
        echo ""
        echo "Bundled Qt6 Libraries (should be used):"
        ls -1 /usr/lib/openterfaceqt/qt6/libQt6*.so* 2>/dev/null | head -10 || echo "(none)"
        echo ""
        echo "Special Check - libQt6QmlModels:"
        echo "  System locations: $(find /lib64 /usr/lib64 -name "*QmlModels*" 2>/dev/null | wc -l) found"
        echo "  Bundled locations: $(find /usr/lib/openterfaceqt -name "*QmlModels*" 2>/dev/null | wc -l) found"
        echo "========================================"
    } | tee -a "$LAUNCHER_LOG"
else
    # Log env vars to file even in non-debug mode for troubleshooting
    {
        echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" 
        echo "LD_PRELOAD=$LD_PRELOAD" 
        echo "QT_PLUGIN_PATH=$QT_PLUGIN_PATH" 
        echo "QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH" 
        echo "QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
        echo "QML2_IMPORT_PATH=$QML2_IMPORT_PATH" 
        echo "GST_PLUGIN_PATH=$GST_PLUGIN_PATH" 
    } >> "$LAUNCHER_LOG"
fi

# ============================================
# Debug Output
# ============================================
# ============================================
# Application Execution
# ============================================
# Locate and execute the OpenterfaceQT binary
# NOTE: The binary is at /usr/local/bin/openterfaceQT.bin (with .bin extension)
# This ensures LD_PRELOAD and environment variables are ALWAYS applied

# Try multiple locations for the binary (with fallbacks)
# NOTE: Do NOT include the launcher path itself (/usr/local/bin/openterfaceQT)
# to avoid infinite recursion. The launcher should be named openterfaceQT or openterfaceQT-launcher.sh
# and the actual binary should be named openterfaceQT.bin
OPENTERFACE_BIN=""
for bin_path in \
    "/usr/local/bin/openterfaceQT.bin" \
    "/usr/bin/openterfaceQT.bin" \
    "/opt/openterface/bin/openterfaceQT.bin" \
    "/usr/local/bin/openterfaceQT-bin" \
    "/usr/bin/openterfaceQT-bin" \
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
        echo "  - /usr/local/bin/openterfaceQT.bin" >&2
        echo "  - /usr/bin/openterfaceQT.bin" >&2
        echo "  - /opt/openterface/bin/openterfaceQT.bin" >&2
        echo "  - /usr/local/bin/openterfaceQT-bin" >&2
        echo "  - /usr/bin/openterfaceQT-bin" >&2
        echo "  - /opt/openterface/bin/openterfaceQT-bin" >&2
        echo "" >&2
        echo "NOTE: The actual binary should be named 'openterfaceQT.bin'" >&2
        echo "The launcher script should be installed as 'openterfaceQT' or 'openterfaceQT-launcher.sh'" >&2
        echo "Launcher log: $LAUNCHER_LOG" >&2
    } | tee -a "$LAUNCHER_LOG"
    exit 1
fi

# Debug: Show what will be executed
{
    echo ""
    echo "========================================" 
    echo "Launcher Execution Summary"
    echo "========================================" 
    echo "Executing: $OPENTERFACE_BIN $@"
    echo "Launcher log: $LAUNCHER_LOG"
    echo ""
    echo "LD_LIBRARY_PATH:"
    echo "  $LD_LIBRARY_PATH" | tr ':' '\n' | sed 's/^/    /'
    echo ""
    echo "LD_PRELOAD (first 5):"
    echo "$LD_PRELOAD" | tr ':' '\n' | head -5 | sed 's/^/    /'
    echo "  ... ($(echo "$LD_PRELOAD" | tr ':' '\n' | wc -l) total libraries)"
    echo ""
    echo "QT_PLUGIN_PATH: $QT_PLUGIN_PATH"
    echo "QT_QPA_PLATFORM_PLUGIN_PATH: $QT_QPA_PLATFORM_PLUGIN_PATH"
    echo "QT_QPA_PLATFORM: $QT_QPA_PLATFORM (🔑 Will use this platform)"
    echo "QML2_IMPORT_PATH: $QML2_IMPORT_PATH"
    echo "GST_PLUGIN_PATH: $GST_PLUGIN_PATH"
    echo ""
    echo "Environment Detection:"
    echo "  DISPLAY: ${DISPLAY:-'(not set - headless)'}"
    echo "  WAYLAND_DISPLAY: ${WAYLAND_DISPLAY:-'(not set)'}"
    echo "========================================"
    echo ""
    
    # ========================================
    # COMPREHENSIVE DIAGNOSTIC SECTION
    # ========================================
    echo "DIAGNOSTIC: Checking for Qt Library Conflicts"
    echo "=================================================="
    echo ""
    echo "System Qt6 Libraries in standard locations:"
    SYSTEM_QT_COUNT=$(find /lib64 /usr/lib64 -name "libQt6*.so*" -type f 2>/dev/null | wc -l)
    echo "  Total found: $SYSTEM_QT_COUNT"
    find /lib64 /usr/lib64 -name "libQt6*.so*" -type f 2>/dev/null | head -5 | sed 's/^/    /'
    if [ $SYSTEM_QT_COUNT -gt 5 ]; then
        echo "    ... and $((SYSTEM_QT_COUNT - 5)) more"
    fi
    echo ""
    
    echo "Bundled Qt6 Libraries in /usr/lib/openterfaceqt/qt6/:"
    BUNDLED_QT_COUNT=$(find /usr/lib/openterfaceqt/qt6 -name "libQt6*.so*" -type f 2>/dev/null | wc -l)
    echo "  Total found: $BUNDLED_QT_COUNT"
    find /usr/lib/openterfaceqt/qt6 -name "libQt6*.so*" -type f 2>/dev/null | head -5 | sed 's/^/    /'
    if [ $BUNDLED_QT_COUNT -gt 5 ]; then
        echo "    ... and $((BUNDLED_QT_COUNT - 5)) more"
    fi
    echo ""
    
    # Critical: Check for QmlModels - this is the known problem!
    echo "🔍 CRITICAL: libQt6QmlModels location check:"
    SYSTEM_QML=$(find /lib64 /usr/lib64 -name "*QmlModels*" 2>/dev/null | wc -l)
    BUNDLED_QML=$(find /usr/lib/openterfaceqt -name "*QmlModels*" 2>/dev/null | wc -l)
    echo "  System locations: $SYSTEM_QML"
    echo "  Bundled locations: $BUNDLED_QML"
    find /lib64 /usr/lib64 -name "*QmlModels*" 2>/dev/null | sed 's/^/    System: /'
    find /usr/lib/openterfaceqt -name "*QmlModels*" 2>/dev/null | sed 's/^/    Bundled: /'
    echo ""
    
    if [ $SYSTEM_QML -gt 0 ] && [ $BUNDLED_QML -eq 0 ]; then
        echo "⚠️  WARNING: libQt6QmlModels found only in system, not in bundled!"
        echo "    This WILL cause version conflicts. Need to either:"
        echo "    1. Add libQt6QmlModels to bundled directory, OR"
        echo "    2. Remove system Qt libraries with: sudo yum remove -y 'qt6-*'"
        echo ""
    fi
    
    echo "=================================================="
    echo ""
} | tee -a "$LAUNCHER_LOG"

# Capture binary output and error for debugging
APP_LOG="/tmp/openterfaceqt-app-$(date +%s).log"
{
    echo "=== OpenterfaceQT Application Started at $(date) ===" 
    echo "Binary: $OPENTERFACE_BIN"
    echo "Binary Type: $(file "$OPENTERFACE_BIN" 2>/dev/null || echo 'unknown')"
    echo ""
    echo "Qt Platform Configuration:"
    echo "  QT_QPA_PLATFORM: $QT_QPA_PLATFORM"
    echo "  QT_QPA_PLATFORM_PLUGIN_PATH: $QT_QPA_PLATFORM_PLUGIN_PATH"
    echo ""
    echo ""
    echo "Binary Dependencies (ldd output):"
    echo "--- Attempting to resolve dependencies ---"
    ldd "$OPENTERFACE_BIN" 2>&1 || echo "(ldd failed - may be expected)"
    echo ""
    echo "Environment Variables at Execution:"
    echo "  LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
    echo "  LD_PRELOAD=$LD_PRELOAD"
    echo "  QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
    echo "  QT_QPA_PLATFORM_PLUGIN_PATH=$QT_QPA_PLATFORM_PLUGIN_PATH"
    echo "  QML2_IMPORT_PATH=$QML2_IMPORT_PATH"
    echo "  GST_PLUGIN_PATH=$GST_PLUGIN_PATH"
    echo ""
    echo "Available Bundled Qt6 Libraries:"
    ls -1 /usr/lib/openterfaceqt/qt6/libQt6*.so* 2>/dev/null | head -10 || echo "(not found)"
    echo ""
    echo "System Qt6 Libraries (should NOT be used):"
    find /lib64 /usr/lib64 -name "libQt6*.so*" -type f 2>/dev/null | head -5 || echo "(checking...)"
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
