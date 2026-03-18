#!/bin/bash
# OpenterfaceQT Launcher - Sets up bundled library paths
# This script ensures bundled Qt6, FFmpeg, and GStreamer libraries are loaded with proper priority

# ============================================
# Error Handling & Logging
# ============================================
LAUNCHER_LOG="/tmp/openterfaceqt-launcher-$(date +%s).log"
CLEANUP_HANDLER_RAN=0

# Use printf to avoid tee command at startup (before environment cleanup)
/usr/bin/printf "=== OpenterfaceQT Launcher Started at %s ===\nScript PID: %s\nArguments: %s\n" "$(/bin/date)" "$$" "$*" > "$LAUNCHER_LOG" 2>/dev/null || true

# Trap errors and log them (but don't use set -e to allow graceful library lookups)
# Only trap real errors, not expected return codes from find_library function
# Use clean environment to prevent bundled library conflicts in error logging
trap 'if [ $? -ne 1 ] && [ $? -ne 143 ] && [ $? -ne 137 ]; then /usr/bin/printf "ERROR at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >> "$LAUNCHER_LOG" 2>/dev/null || true; fi' ERR

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
# This is THE KEY to avoiding "version `Qt_6.6' not found" errors.

PRELOAD_LIBS=()

# Qt6 core libraries - MUST be preloaded in correct order
# The order is critical: Core first, then Gui, then everything else
QT6_CORE_LIBS=(
    "libQt6Core"      # MUST be first
    "libQt6Gui"       # Must be before other modules
)

# Qt6 module libraries
QT6_MODULE_LIBS=(
    "libQt6Multimedia"       # CRITICAL: Must load bundled multimedia BEFORE other modules
    "libQt6MultimediaWidgets"  # Must follow Multimedia
    "libQt6Widgets"
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
    "libgstcodecs-1.0"
    "libgstcodecparsers-1.0"
    "libgstpbutils-1.0"
    "libgsttag-1.0"
    "liborc-0.4"
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

# Common/System libraries - essential support libraries
COMMON_LIBS=(
    "libusb-1.0"
    "libgudev-1.0"
    "libv4l1"
    "libv4l2"
    "libv4l2rds"
    "libv4lconvert"
    "libXv"
)

# Load common libraries
for lib in "${COMMON_LIBS[@]}"; do
    lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt")
    if [ -z "$lib_path" ]; then
        # Try gstreamer subdirectory for v4l libraries
        lib_path=$(find_library "$lib" "/usr/lib/openterfaceqt/gstreamer")
    fi
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    else
        # Log missing libraries for debugging (suppress in non-debug mode)
        if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
            echo "⚠️  Common library not found: $lib" >&2
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
if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
    # Use clean environment for debug output to prevent library conflicts
    /usr/bin/printf "========================================\nOpenterfaceQT Runtime Environment Setup\n========================================\nLD_LIBRARY_PATH=%s\nLD_PRELOAD=%s\nQT_PLUGIN_PATH=%s\nQT_QPA_PLATFORM_PLUGIN_PATH=%s\nQT_QPA_PLATFORM=%s\nQML2_IMPORT_PATH=%s\nGST_PLUGIN_PATH=%s\n========================================\n" "$LD_LIBRARY_PATH" "$LD_PRELOAD" "$QT_PLUGIN_PATH" "$QT_QPA_PLATFORM_PLUGIN_PATH" "$QT_QPA_PLATFORM" "$QML2_IMPORT_PATH" "$GST_PLUGIN_PATH" >> "$LAUNCHER_LOG" 2>/dev/null || true
else
    # Log env vars to file even in non-debug mode for troubleshooting
    /usr/bin/printf "LD_LIBRARY_PATH=%s\nLD_PRELOAD=%s\nQT_PLUGIN_PATH=%s\nQT_QPA_PLATFORM_PLUGIN_PATH=%s\nQML2_IMPORT_PATH=%s\nGST_PLUGIN_PATH=%s\n" "$LD_LIBRARY_PATH" "$LD_PRELOAD" "$QT_PLUGIN_PATH" "$QT_QPA_PLATFORM_PLUGIN_PATH" "$QML2_IMPORT_PATH" "$GST_PLUGIN_PATH" >> "$LAUNCHER_LOG" 2>/dev/null || true
fi

# ============================================
# Cleanup Handler - Force Kill on Exit
# ============================================
# This trap ensures that lingering binary processes are properly terminated
cleanup_handler() {
    # Prevent handler from running multiple times
    if [ $CLEANUP_HANDLER_RAN -eq 1 ]; then
        return
    fi
    CLEANUP_HANDLER_RAN=1
    
    local exit_code=$?
    
    # CRITICAL: Reset environment to prevent system commands from using bundled libraries
    # This fixes "libpostproc.so.57: cannot open shared object file" errors during cleanup
    unset LD_PRELOAD
    unset LD_LIBRARY_PATH
    unset QT_PLUGIN_PATH
    unset QT_QPA_PLATFORM_PLUGIN_PATH
    unset QML2_IMPORT_PATH
    unset GST_PLUGIN_PATH
    
    # Use absolute paths and clean environment for all system commands
    /usr/bin/printf "\n=== Cleanup Handler Triggered ===\nApp PID: %s\nExit Code: %s\nTime: %s\n" "$APP_PID" "$exit_code" "$(/bin/date)" >> "$LAUNCHER_LOG" 2>/dev/null || true
    
    # Kill the app if it's still running (gracefully first, then forcefully)
    if [ -n "$APP_PID" ] && /bin/kill -0 "$APP_PID" 2>/dev/null; then
        /usr/bin/printf "Application process still running (PID: %s). Attempting graceful termination...\n" "$APP_PID" >> "$LAUNCHER_LOG" 2>/dev/null || true
        
        # Try graceful termination first (SIGTERM)
        /bin/kill -TERM "$APP_PID" 2>/dev/null || true
        
        # Wait up to 3 seconds for graceful shutdown
        local wait_count=0
        while [ $wait_count -lt 30 ] && /bin/kill -0 "$APP_PID" 2>/dev/null; do
            /bin/sleep 0.1
            wait_count=$((wait_count + 1))
        done
        
        # If still running, force kill (SIGKILL)
        if /bin/kill -0 "$APP_PID" 2>/dev/null; then
            /usr/bin/printf "Process did not terminate gracefully. Force killing (SIGKILL)...\n" >> "$LAUNCHER_LOG" 2>/dev/null || true
            /bin/kill -KILL "$APP_PID" 2>/dev/null || true
            /bin/sleep 0.5
        fi
    fi
    
    # Kill any remaining child processes of the launcher
    local child_pids=$(/usr/bin/pgrep -P $$ 2>/dev/null || true)
    if [ -n "$child_pids" ]; then
        /usr/bin/printf "Killing remaining child processes: %s\n" "$child_pids" >> "$LAUNCHER_LOG" 2>/dev/null || true
        echo "$child_pids" | /usr/bin/xargs /bin/kill -KILL 2>/dev/null || true
    fi
    
    # Also try killing any openterfaceQT.bin processes that may have been orphaned
    local orphaned_pids=$(/usr/bin/pgrep -f "openterfaceQT\\.bin|openterfaceQT-bin" 2>/dev/null | /bin/grep -v "^$$" || true)
    if [ -n "$orphaned_pids" ]; then
        /usr/bin/printf "Killing orphaned openterface processes: %s\n" "$orphaned_pids" >> "$LAUNCHER_LOG" 2>/dev/null || true
        echo "$orphaned_pids" | /usr/bin/xargs /bin/kill -KILL 2>/dev/null || true
    fi
    
    /usr/bin/printf "=== Cleanup Complete ===\n" >> "$LAUNCHER_LOG" 2>/dev/null || true
    
    # Exit with the captured exit code
    exit $exit_code
}

# Set up cleanup trap for normal exit and signals
trap cleanup_handler EXIT INT TERM

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
    # Use clean environment for error reporting
    /usr/bin/printf "ERROR: OpenterfaceQT binary not found in standard locations\nSearched:\n  - /usr/local/bin/openterfaceQT.bin\n  - /usr/bin/openterfaceQT.bin\n  - /opt/openterface/bin/openterfaceQT.bin\n  - /usr/local/bin/openterfaceQT-bin\n  - /usr/bin/openterfaceQT-bin\n  - /opt/openterface/bin/openterfaceQT-bin\n\nNOTE: The actual binary should be named 'openterfaceQT.bin'\nThe launcher script should be installed as 'openterfaceQT' or 'openterfaceQT-launcher.sh'\nLauncher log: %s\n" "$LAUNCHER_LOG" >&2
    /usr/bin/printf "ERROR: OpenterfaceQT binary not found in standard locations\n" >> "$LAUNCHER_LOG" 2>/dev/null || true
    exit 1
fi

# Debug: Show what will be executed (use clean environment)
/usr/bin/printf "\nExecuting: %s %s\nLauncher log location: %s\n\n" "$OPENTERFACE_BIN" "$*" "$LAUNCHER_LOG" >> "$LAUNCHER_LOG" 2>/dev/null || true

# Capture binary output and error for debugging
APP_LOG="/tmp/openterfaceqt-app-$(date +%s).log"
# Use clean printf to create app log header (avoid tee at this stage)
/usr/bin/printf "=== OpenterfaceQT Application Started at %s ===\nBinary: %s\nEnvironment Variables:\n  LD_LIBRARY_PATH=%s\n  LD_PRELOAD=%s\n  QT_PLUGIN_PATH=%s\n  QT_QPA_PLATFORM_PLUGIN_PATH=%s\n  QML2_IMPORT_PATH=%s\n  GST_PLUGIN_PATH=%s\n\n=== Application Output ===\n" "$(/bin/date)" "$OPENTERFACE_BIN" "$LD_LIBRARY_PATH" "$LD_PRELOAD" "$QT_PLUGIN_PATH" "$QT_QPA_PLATFORM_PLUGIN_PATH" "$QML2_IMPORT_PATH" "$GST_PLUGIN_PATH" > "$APP_LOG" 2>&1

# Execute the binary with all passed arguments; use env to inject the
# modified library path only for the child process.
# Redirect output to log file only to avoid tee command issues during cleanup
env LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
    LD_PRELOAD="$LD_PRELOAD" \
    QT_PLUGIN_PATH="$QT_PLUGIN_PATH" \
    QT_QPA_PLATFORM_PLUGIN_PATH="$QT_QPA_PLATFORM_PLUGIN_PATH" \
    QML2_IMPORT_PATH="$QML2_IMPORT_PATH" \
    GST_PLUGIN_PATH="$GST_PLUGIN_PATH" \
    "$OPENTERFACE_BIN" "$@" >> "$APP_LOG" 2>&1 &
APP_PID=$!

# if this script was sourced by accident, remove bundled library environment
# to avoid leaving them set in the caller's environment
unset LD_LIBRARY_PATH
unset LD_PRELOAD
unset QT_PLUGIN_PATH
unset QT_QPA_PLATFORM_PLUGIN_PATH
unset QML2_IMPORT_PATH
unset GST_PLUGIN_PATH

# Use clean environment for logging to prevent libpostproc.so.57 errors
/usr/bin/printf "Application started with PID: %s\nApplication log: %s\n" "$APP_PID" "$APP_LOG" >> "$LAUNCHER_LOG" 2>/dev/null || true

# Wait for application to finish and capture exit code
# Use wait with error suppression since the process might be killed by cleanup handler
wait $APP_PID 2>/dev/null
APP_EXIT_CODE=$?

# Use clean environment for final logging
/usr/bin/printf "\n=== Application Exited ===\nExit Code: %s\nTime: %s\n" "$APP_EXIT_CODE" "$(/bin/date)" >> "$LAUNCHER_LOG" 2>/dev/null || true
