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
    "/usr/lib/openterfaceqt/ffmpeg"     # Bundled FFmpeg libraries
    "/usr/lib/openterfaceqt/gstreamer"  # Bundled GStreamer libraries
    "/usr/lib/openterfaceqt"            # Bundled libraries (other dependencies)
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

# GStreamer libraries - essential for media handling
GSTREAMER_LIBS=(
    "libgstreamer-1.0.so.0"
    "libgstbase-1.0.so.0"
    "libgstapp-1.0.so.0"
    "libgstvideo-1.0.so.0"
    "libgstaudio-1.0.so.0"
    "libgstpbutils-1.0.so.0"
)

# Load GStreamer libraries
for lib in "${GSTREAMER_LIBS[@]}"; do
    lib_path="/usr/lib/openterfaceqt/gstreamer/$lib"
    if [ -f "$lib_path" ]; then
        PRELOAD_LIBS+=("$lib_path")
    fi
done

# FFmpeg libraries - essential for video/audio encoding and decoding
FFMPEG_LIBS=(
    "libavformat.so.61"
    "libavcodec.so.61"
    "libavutil.so.59"
    "libswscale.so.8"
    "libswresample.so.5"
    "libavfilter.so.10"
    "libavdevice.so.61"
)

# Load FFmpeg libraries
for lib in "${FFMPEG_LIBS[@]}"; do
    lib_path="/usr/lib/openterfaceqt/ffmpeg/$lib"
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
    QT_PLUGIN_PATH=$(printf '%s:' "${QT_PLUGIN_PATHS[@]}" | sed 's/:$//')
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
    QT_QPA_PLATFORM_PLUGIN_PATH=$(printf '%s:' "${QT_PLATFORM_PLUGIN_PATHS[@]}" | sed 's/:$//')
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
    QML2_IMPORT_PATH=$(printf '%s:' "${QML_IMPORT_PATHS[@]}" | sed 's/:$//')
    export QML2_IMPORT_PATH="${QML2_IMPORT_PATH}"
fi

# ============================================
# GStreamer Plugin Path Setup
# ============================================
# CRITICAL FIX: Use ONLY system GStreamer plugins if bundled plugins cause symbol errors
# Bundled plugins often have version mismatches with system GStreamer libraries,
# causing "undefined symbol" errors. The launcher prioritizes system plugins for
# compatibility with the system GStreamer core libraries.
#
# If system plugins are not found, the application will still try bundled locations
# as a last resort, but this is only recommended if both bundled plugins and
# bundled core GStreamer libraries are present and compatible.

if [ -z "$GST_PLUGIN_PATH" ]; then
    GST_PLUGIN_PATHS=()
    
    # PRIORITY 1: System GStreamer plugins (MOST IMPORTANT - ensures version compatibility)
    # System plugins are compiled against system GStreamer libraries that are likely
    # already installed as dependencies. This is the most reliable option.
    if [ -d "/usr/lib/${ARCH_DIR}/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/${ARCH_DIR}/gstreamer-1.0")
    fi
    
    if [ -d "/usr/lib/x86_64-linux-gnu/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
    fi
    
    if [ -d "/usr/lib/aarch64-linux-gnu/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/aarch64-linux-gnu/gstreamer-1.0")
    fi
    
    if [ -d "/usr/lib/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/lib/gstreamer-1.0")
    fi
    
    # PRIORITY 2: Local system locations
    if [ -d "/usr/local/lib/gstreamer-1.0" ]; then
        GST_PLUGIN_PATHS+=("/usr/local/lib/gstreamer-1.0")
    fi
    
    # PRIORITY 3: Bundled GStreamer plugins (FALLBACK - only if system plugins not available)
    # NOTE: Bundled plugins may cause "undefined symbol" errors if they were compiled
    # against a different GStreamer version. Only use as a last resort.
    if [ -d "/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0" ]; then
        # Only add bundled path if no system plugins were found yet
        if [ ${#GST_PLUGIN_PATHS[@]} -eq 0 ]; then
            if [ "${OPENTERFACE_DEBUG}" = "1" ] || [ "${OPENTERFACE_DEBUG}" = "true" ]; then
                echo "⚠️  Warning: Using bundled GStreamer plugins - system plugins not found" >&2
            fi
            GST_PLUGIN_PATHS+=("/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0")
        fi
    fi
    
    # PRIORITY 4: Qt6 build GStreamer plugins (for development/custom builds)
    if [ -d "/opt/gstreamer/lib/gstreamer-1.0" ]; then
        if [ ${#GST_PLUGIN_PATHS[@]} -eq 0 ]; then
            GST_PLUGIN_PATHS+=("/opt/gstreamer/lib/gstreamer-1.0")
        fi
    fi
    
    # Join with colons
    GST_PLUGIN_PATH=$(printf '%s:' "${GST_PLUGIN_PATHS[@]}" | sed 's/:$//')
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

# Helper function to validate GStreamer installation
validate_gstreamer_installation() {
    local gst_status=0
    
    echo "========================================" >&2
    echo "GStreamer Installation Validation" >&2
    echo "========================================" >&2
    
    # Check for core GStreamer library
    if ldconfig -p | grep -q "libgstreamer-1.0.so"; then
        echo "✅ libgstreamer-1.0 found in system" >&2
    else
        echo "❌ libgstreamer-1.0 NOT found - GStreamer may not be installed" >&2
        gst_status=1
    fi
    
    # Check for GStreamer plugin scanner
    if [ -f "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/gst-plugin-scanner" ]; then
        echo "✅ gst-plugin-scanner found at /usr/lib/x86_64-linux-gnu/gstreamer-1.0/" >&2
    elif [ -f "/usr/libexec/gstreamer-1.0/gst-plugin-scanner" ]; then
        echo "✅ gst-plugin-scanner found at /usr/libexec/gstreamer-1.0/" >&2
    else
        echo "⚠️  gst-plugin-scanner not found - plugin loading may fail" >&2
    fi
    
    # Check for common GStreamer plugins
    local plugin_dirs=()
    if [ -d "/usr/lib/${ARCH_DIR}/gstreamer-1.0" ]; then
        plugin_dirs+=("/usr/lib/${ARCH_DIR}/gstreamer-1.0")
    fi
    if [ -d "/usr/lib/x86_64-linux-gnu/gstreamer-1.0" ]; then
        plugin_dirs+=("/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
    fi
    if [ -d "/usr/lib/aarch64-linux-gnu/gstreamer-1.0" ]; then
        plugin_dirs+=("/usr/lib/aarch64-linux-gnu/gstreamer-1.0")
    fi
    
    if [ ${#plugin_dirs[@]} -gt 0 ]; then
        echo "Found plugin directories:" >&2
        for plugin_dir in "${plugin_dirs[@]}"; do
            if [ -d "$plugin_dir" ]; then
                local plugin_count=$(find "$plugin_dir" -name "libgst*.so" 2>/dev/null | wc -l)
                echo "  - $plugin_dir ($plugin_count plugins)" >&2
            fi
        done
    else
        echo "❌ No GStreamer plugin directories found" >&2
        gst_status=1
    fi
    
    # Attempt to query GStreamer version
    if command -v gst-inspect-1.0 >/dev/null 2>&1; then
        echo "GStreamer version:" >&2
        gst-inspect-1.0 --version 2>&1 | sed 's/^/  /' >&2
    else
        echo "⚠️  gst-inspect-1.0 not found - cannot verify GStreamer version" >&2
    fi
    
    echo "========================================" >&2
    return $gst_status
}

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
    echo "" >&2
    
    # Validate GStreamer installation when debug is enabled
    validate_gstreamer_installation
    echo "" >&2
fi

# ============================================
# GStreamer Diagnostics Function
# ============================================
# Comprehensive diagnostics to help identify GStreamer version mismatches

print_gstreamer_diagnostics() {
    echo "========================================" >&2
    echo "GStreamer Plugin & Library Diagnostics" >&2
    echo "========================================" >&2
    echo "" >&2
    
    # 1. Check bundled GStreamer core version
    local bundled_gst_lib="/usr/lib/openterfaceqt/gstreamer/libgstreamer-1.0.so.0"
    if [ -f "$bundled_gst_lib" ]; then
        echo "1. Bundled GStreamer Core Library:" >&2
        echo "   File: $bundled_gst_lib" >&2
        echo "   Size: $(du -h "$bundled_gst_lib" | cut -f1)" >&2
        
        # Try to get version from the bundled gst-inspect if available
        local bundled_gst_inspect="/usr/lib/openterfaceqt/gstreamer/bin/gst-inspect-1.0"
        if [ -x "$bundled_gst_inspect" ]; then
            echo "   Version: $("$bundled_gst_inspect" --version 2>&1 | head -1)" >&2
        fi
        echo "" >&2
    else
        echo "1. Bundled GStreamer Core Library:" >&2
        echo "   ✗ NOT FOUND at $bundled_gst_lib" >&2
        echo "" >&2
    fi
    
    # 2. Check system GStreamer core version
    echo "2. System GStreamer Core Library:" >&2
    if command -v gst-inspect-1.0 >/dev/null 2>&1; then
        local sys_version=$(gst-inspect-1.0 --version 2>&1 | head -1)
        echo "   Version: $sys_version" >&2
        
        # Find the actual system library
        local sys_gst_lib=$(ldconfig -p | grep "libgstreamer-1.0.so.0" | awk '{print $NF}' | head -1)
        if [ -n "$sys_gst_lib" ]; then
            echo "   File: $sys_gst_lib" >&2
            echo "   Size: $(du -h "$sys_gst_lib" 2>/dev/null | cut -f1 || echo 'unknown')" >&2
        fi
        echo "" >&2
    else
        echo "   ✗ System gst-inspect-1.0 not found - system GStreamer not installed?" >&2
        echo "" >&2
    fi
    
    # 3. Check for critical symbols in bundled core library
    echo "3. Critical GStreamer Symbols in Bundled Core:" >&2
    if [ -f "$bundled_gst_lib" ] && command -v nm >/dev/null 2>&1; then
        local critical_symbols=(
            "gst_navigation_event_new_touch_up"
            "gst_message_writable_details"
            "gst_caps_features_get_nth_id_str"
            "gst_vec_deque_free"
            "gst_structure_set_static_str"
        )
        
        local missing_count=0
        for symbol in "${critical_symbols[@]}"; do
            if nm -D "$bundled_gst_lib" 2>/dev/null | grep -q "^[0-9a-f]* T $symbol"; then
                echo "   ✓ $symbol" >&2
            else
                echo "   ✗ $symbol (MISSING - indicates version mismatch)" >&2
                missing_count=$((missing_count + 1))
            fi
        done
        
        if [ $missing_count -gt 0 ]; then
            echo "" >&2
            echo "   ⚠️  Found $missing_count missing symbols! This indicates:" >&2
            echo "      - Bundled plugins may be compiled against a newer GStreamer version" >&2
            echo "      - Bundled core libraries are older than what plugins expect" >&2
        fi
        echo "" >&2
    else
        echo "   (nm command not available or bundled library not found)" >&2
        echo "" >&2
    fi
    
    # 4. List bundled GStreamer core libraries
    echo "4. Bundled GStreamer Core Libraries:" >&2
    local bundled_libs_dir="/usr/lib/openterfaceqt/gstreamer"
    if [ -d "$bundled_libs_dir" ]; then
        local lib_count=$(find "$bundled_libs_dir" -maxdepth 1 -name "libgst*.so*" 2>/dev/null | wc -l)
        if [ $lib_count -gt 0 ]; then
            find "$bundled_libs_dir" -maxdepth 1 -name "libgst*.so*" 2>/dev/null | sort | while read lib; do
                echo "   - $(basename "$lib")" >&2
            done
        else
            echo "   (No GStreamer core libraries found)" >&2
        fi
        echo "" >&2
    else
        echo "   (Bundled GStreamer directory not found)" >&2
        echo "" >&2
    fi
    
    # 5. Check bundled GStreamer plugins
    echo "5. Bundled GStreamer Plugins:" >&2
    local bundled_plugins_dir="/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0"
    if [ -d "$bundled_plugins_dir" ]; then
        local plugin_count=$(find "$bundled_plugins_dir" -name "libgst*.so" 2>/dev/null | wc -l)
        echo "   Found $plugin_count plugins:" >&2
        find "$bundled_plugins_dir" -name "libgst*.so" 2>/dev/null | sort | while read plugin; do
            echo "   - $(basename "$plugin")" >&2
        done
        echo "" >&2
    else
        echo "   (Bundled plugins directory not found)" >&2
        echo "" >&2
    fi
    
    # 6. Check for plugin load errors
    echo "6. Plugin Load Validation:" >&2
    if [ -d "$bundled_plugins_dir" ]; then
        local bundled_gst_inspect="/usr/lib/openterfaceqt/gstreamer/bin/gst-inspect-1.0"
        if [ -x "$bundled_gst_inspect" ]; then
            local errors=$("$bundled_gst_inspect" 2>&1 | grep -i "failed to load\|undefined symbol\|cannot open" || true)
            if [ -z "$errors" ]; then
                echo "   ✓ No plugin load errors detected" >&2
            else
                echo "   ✗ Plugin Load Errors Found:" >&2
                echo "$errors" | head -10 | while read line; do
                    echo "      $line" >&2
                done
                local error_count=$(echo "$errors" | wc -l)
                if [ $error_count -gt 10 ]; then
                    echo "      ... and $((error_count - 10)) more errors" >&2
                fi
            fi
        else
            echo "   (bundled gst-inspect-1.0 not available)" >&2
        fi
        echo "" >&2
    fi
    
    # 7. Check system GStreamer plugins
    echo "7. System GStreamer Plugins:" >&2
    local system_plugin_dirs=()
    for dir in "/usr/lib/x86_64-linux-gnu/gstreamer-1.0" "/usr/lib/aarch64-linux-gnu/gstreamer-1.0" "/usr/lib/gstreamer-1.0"; do
        if [ -d "$dir" ]; then
            system_plugin_dirs+=("$dir")
        fi
    done
    
    if [ ${#system_plugin_dirs[@]} -gt 0 ]; then
        for plugin_dir in "${system_plugin_dirs[@]}"; do
            local count=$(find "$plugin_dir" -name "libgst*.so" 2>/dev/null | wc -l)
            echo "   $plugin_dir: $count plugins" >&2
        done
        echo "" >&2
    else
        echo "   (No system plugin directories found)" >&2
        echo "" >&2
    fi
    
    # 8. Summary and recommendations
    echo "8. Recommendations:" >&2
    echo "   Option A: Use bundled GStreamer" >&2
    echo "      - Set: export OPENTERFACE_USE_BUNDLED_GSTREAMER=1" >&2
    echo "      - Rebuild bundled plugins against bundled core libraries" >&2
    echo "" >&2
    echo "   Option B: Use system GStreamer (recommended for compatibility)" >&2
    echo "      - Install: sudo apt-get install gstreamer1.0-plugins-*" >&2
    echo "      - Set: export OPENTERFACE_USE_SYSTEM_GSTREAMER=1" >&2
    echo "" >&2
    echo "   Option C: Set GST_PLUGIN_PATH manually" >&2
    echo "      - Set: export GST_PLUGIN_PATH=/path/to/plugins" >&2
    echo "" >&2
    echo "========================================" >&2
}

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

# ============================================
# Display GStreamer Diagnostics at Startup
# ============================================
# Always show diagnostics to help users understand which GStreamer version is being used
# This helps identify and resolve version mismatch issues
if [ "${OPENTERFACE_SHOW_GSTREAMER_INFO}" != "0" ] && [ "${OPENTERFACE_SHOW_GSTREAMER_INFO}" != "false" ]; then
    # Show diagnostics unless explicitly disabled
    if [ -z "$OPENTERFACE_SHOW_GSTREAMER_INFO" ] || [ "${OPENTERFACE_SHOW_GSTREAMER_INFO}" = "1" ] || [ "${OPENTERFACE_SHOW_GSTREAMER_INFO}" = "true" ]; then
        echo "" >&2
        print_gstreamer_diagnostics
        echo "" >&2
    fi
fi

# Execute the binary with all passed arguments
exec "$OPENTERFACE_BIN" "$@"
