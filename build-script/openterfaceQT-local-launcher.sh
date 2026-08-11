#!/bin/bash
# OpenterfaceQT Local Build Launcher
# For development use only — uses system Qt/libraries directly.
# Bundled libraries at /usr/lib/openterfaceqt are for RPM distribution.

LAUNCHER_LOG="/tmp/openterfaceqt-launcher-$(date +%s).log"
{
    echo "=== OpenterfaceQT Launcher Started at $(date) ==="
    echo "Script PID: $$"
    echo "Arguments: $@"
} | tee "$LAUNCHER_LOG"

# Determine script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_BIN="${SCRIPT_DIR}/openterfaceQT"

if [ ! -f "$APP_BIN" ]; then
    echo "ERROR: openterfaceQT binary not found at $APP_BIN" | tee -a "$LAUNCHER_LOG"
    exit 1
fi

# ==================================================
# Environment: system libs + X11/Wayland platform support
# ==================================================
# System library paths
LD_LIBRARY_PATH=""
for lib_path in /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib; do
    if [ -d "$lib_path" ]; then
        if [ -z "$LD_LIBRARY_PATH" ]; then
            LD_LIBRARY_PATH="$lib_path"
        else
            LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:${lib_path}"
        fi
    fi
done
export LD_LIBRARY_PATH

# X11/XCB and Wayland system libraries for platform plugin
XCB_SUPPORT_LIBS=(
    "libxcb"
    "libX11"
    "libxcb-cursor"
    "libxcb-render"
    "libxcb-xkb"
    "libxkbcommon"
    "libxkbcommon-x11"
    "libXv"
)

WAYLAND_SUPPORT_LIBS=(
    "libwayland-client"
    "libwayland-cursor"
    "libwayland-egl"
)

PRELOAD_LIBS=()

find_library() {
    local lib_base="$1"
    for search_dir in /lib64 /usr/lib64 /usr/lib /lib /usr/local/lib; do
        local found
        found=$(find "$search_dir" -maxdepth 1 -name "${lib_base}.so.*" -type f 2>/dev/null | head -n 1)
        if [ -n "$found" ]; then
            echo "$found"
            return 0
        fi
        found=$(find "$search_dir" -maxdepth 1 -name "${lib_base}.so" -type f 2>/dev/null | head -n 1)
        if [ -n "$found" ]; then
            echo "$found"
            return 0
        fi
    done
    return 1
}

for lib in "${XCB_SUPPORT_LIBS[@]}" "${WAYLAND_SUPPORT_LIBS[@]}"; do
    lib_path=$(find_library "$lib")
    if [ -n "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    fi
done

# Preload system libva: ldconfig may have /usr/lib/openterfaceqt cached,
# which contains an older libva missing vaMapBuffer2 needed by system libavutil.
for sys_lib in /usr/lib64/libva.so.2 /usr/lib64/libva-drm.so.2 /usr/lib64/libva-x11.so.2; do
    if [ -f "$sys_lib" ]; then
        PRELOAD_LIBS=("$sys_lib" "${PRELOAD_LIBS[@]}")
    fi
done

if [ ${#PRELOAD_LIBS[@]} -gt 0 ]; then
    export LD_PRELOAD=$(IFS=':'; echo "${PRELOAD_LIBS[*]}")
fi

# Qt plugin paths
if [ -z "$QT_PLUGIN_PATH" ]; then
    export QT_PLUGIN_PATH="/usr/lib/qt6/plugins"
fi

if [ -z "$QT_QPA_PLATFORM_PLUGIN_PATH" ]; then
    export QT_QPA_PLATFORM_PLUGIN_PATH="/usr/lib/qt6/plugins/platforms"
fi

if [ -z "$QML2_IMPORT_PATH" ]; then
    export QML2_IMPORT_PATH="/usr/lib/qt6/qml:/usr/lib64/qt6/qml"
fi

# Platform detection
if [ -z "$QT_QPA_PLATFORM" ]; then
    if [ -n "$DISPLAY" ]; then
        export QT_QPA_PLATFORM="xcb"
    elif [ -n "$WAYLAND_DISPLAY" ]; then
        export QT_QPA_PLATFORM="wayland"
    else
        export QT_QPA_PLATFORM="offscreen"
    fi
fi

# ==================================================
# Execute
# ==================================================
echo "Executing: $APP_BIN $@" | tee -a "$LAUNCHER_LOG"
echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH" | tee -a "$LAUNCHER_LOG"
echo "LD_PRELOAD (first 3): $(echo "$LD_PRELOAD" | tr ':' '\n' | head -3 | tr '\n' ':')" | tee -a "$LAUNCHER_LOG"
echo "QT_QPA_PLATFORM: $QT_QPA_PLATFORM" | tee -a "$LAUNCHER_LOG"

APP_LOG="/tmp/openterfaceqt-app-$(date +%s).log"
"$APP_BIN" "$@" 2>&1 | tee -a "$APP_LOG"
exit_code=${PIPESTATUS[0]}

echo "Application exited with code: $exit_code" | tee -a "$LAUNCHER_LOG"
exit $exit_code
