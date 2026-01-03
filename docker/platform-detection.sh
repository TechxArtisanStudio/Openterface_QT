#!/bin/bash
# =============================================================================
# Openterface QT Platform Detection Script
# =============================================================================
#
# This script provides robust platform (Wayland vs X11/XCB) detection
# for both AppImage and system (RPM/DEB) installations.
#
# It attempts multiple detection methods and provides intelligent fallback logic:
# 1. Check running session type (most reliable when available)
# 2. Check environment variables
# 3. Check for Wayland libraries (filesystem check)
# 4. Fall back to XCB (most stable in container environments)
#
# Usage:
#   source /docker/platform-detection.sh
#   detect_platform
#
# Environment variables set:
#   DETECTED_PLATFORM: "wayland" or "xcb"
#   QT_QPA_PLATFORM: Qt platform plugin name
#   PLATFORM_DETECTION_METHOD: Detection method used
#   PLATFORM_CONFIDENCE: "high", "medium", or "low"
#
# =============================================================================

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# =============================================================================
# Platform Detection Functions
# =============================================================================

detect_platform() {
    local detection_method=""
    local confidence="low"
    local platform="xcb"  # Default fallback
    
    echo "🔍 Platform Detection: Starting comprehensive detection..."
    echo "   DISPLAY=:0"
    echo "   XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-not set}"
    echo "   LD_PRELOAD entries: $(echo "$LD_PRELOAD" | wc -w)"
    
    # =========================================================================
    # Method 1: Check systemd session (Most reliable, high confidence)
    # =========================================================================
    if command -v systemctl >/dev/null 2>&1; then
        echo "  ℹ️  Method 1 (systemd): Checking active sessions..."
        
        if systemctl --user is-active --quiet wayland-session.target 2>/dev/null; then
            echo "  ✅ Method 1 (systemd): wayland-session.target is ACTIVE"
            platform="wayland"
            detection_method="systemd_session_active"
            confidence="high"
        else
            echo "  ❌ Method 1 (systemd): wayland-session.target NOT active or systemctl unavailable"
        fi
    else
        echo "  ❌ Method 1 (systemd): systemctl unavailable"
    fi
    
    # =========================================================================
    # Method 2: Check QT_QPA_PLATFORM environment variable (High confidence)
    # =========================================================================
    if [ -z "$detection_method" ] || [ "$confidence" = "low" ]; then
        echo "  ℹ️  Method 2 (systemd env): Checking QT_QPA_PLATFORM..."
        
        if [ -n "$QT_QPA_PLATFORM" ]; then
            if [[ "$QT_QPA_PLATFORM" == *"wayland"* ]]; then
                echo "  ✅ Method 2 (systemd env): QT_QPA_PLATFORM=$QT_QPA_PLATFORM (Wayland detected)"
                platform="wayland"
                detection_method="qt_env_wayland"
                confidence="high"
            else
                echo "  ℹ️  Method 2 (systemd env): QT_QPA_PLATFORM=$QT_QPA_PLATFORM (not Wayland)"
            fi
        else
            echo "  ❌ Method 2 (systemd env): QT_QPA_PLATFORM=wayland NOT found"
        fi
    fi
    
    # =========================================================================
    # Method 3: Check XDG_SESSION_TYPE (Medium confidence)
    # =========================================================================
    if [ -z "$detection_method" ] || [ "$confidence" = "low" ]; then
        echo "  ℹ️  Method 3 (XDG): Checking XDG_SESSION_TYPE..."
        
        if [[ "${XDG_SESSION_TYPE}" == *"wayland"* ]]; then
            echo "  ✅ Method 3 (XDG): XDG_SESSION_TYPE='$XDG_SESSION_TYPE' (Wayland detected)"
            platform="wayland"
            detection_method="xdg_session_wayland"
            confidence="medium"
        else
            echo "  ❌ Method 3 (XDG): XDG_SESSION_TYPE='${XDG_SESSION_TYPE:-not set}' does NOT contain 'wayland'"
        fi
    fi
    
    # =========================================================================
    # Method 4a: Check for Wayland libraries (filesystem check, low confidence)
    # =========================================================================
    if [ -z "$detection_method" ] || [ "$confidence" = "low" ]; then
        echo "  ℹ️  Method 4a (filesystem): Checking for Wayland libraries..."
        
        if [ -f /usr/lib/libwayland-client.so.0 ] || [ -f /usr/lib64/libwayland-client.so.0 ] || \
           [ -f /usr/lib/x86_64-linux-gnu/libwayland-client.so.0 ]; then
            echo "  ✅ Method 4a (filesystem): Found /usr/lib*/libwayland-client.so.0"
            
            # Check if libwayland-egl is also available (better Wayland support)
            if [ -f /usr/lib/libwayland-egl.so.1 ] || [ -f /usr/lib64/libwayland-egl.so.1 ] || \
               [ -f /usr/lib/x86_64-linux-gnu/libwayland-egl.so.1 ]; then
                echo "       ✅ Also found libwayland-egl.so.1 (full Wayland support)"
                platform="wayland"
                detection_method="filesystem_wayland_full"
                confidence="low"
            else
                echo "       ⚠️  libwayland-egl NOT found (Wayland support incomplete)"
                echo "       Falling back to XCB for stability"
                platform="xcb"
                detection_method="filesystem_wayland_incomplete"
                confidence="low"
            fi
        else
            echo "  ❌ Method 4a (filesystem): libwayland-client.so.0 NOT found"
        fi
    fi
    
    # =========================================================================
    # Method 4b: Check for XCB libraries (filesystem check, fallback)
    # =========================================================================
    if [ -z "$detection_method" ] || [ "$confidence" = "low" ]; then
        echo "  ℹ️  Method 4b (filesystem): Checking for XCB/X11 libraries..."
        
        if [ -f /usr/lib/libxcb.so.1 ] || [ -f /usr/lib64/libxcb.so.1 ] || \
           [ -f /usr/lib/x86_64-linux-gnu/libxcb.so.1 ]; then
            echo "  ✅ Method 4b (filesystem): Found /usr/lib*/libxcb.so.1 (XCB available)"
            platform="xcb"
            detection_method="filesystem_xcb_available"
            confidence="medium"
        else
            echo "  ❌ Method 4b (filesystem): libxcb.so.1 NOT found"
        fi
    fi
    
    # =========================================================================
    # Method 5: Environment variable override check
    # =========================================================================
    if [ -n "$OPENTERFACE_FORCE_PLATFORM" ]; then
        echo "  ℹ️  Method 5 (override): OPENTERFACE_FORCE_PLATFORM set to $OPENTERFACE_FORCE_PLATFORM"
        
        if [[ "$OPENTERFACE_FORCE_PLATFORM" == "wayland" ]] || [[ "$OPENTERFACE_FORCE_PLATFORM" == "xcb" ]] || \
           [[ "$OPENTERFACE_FORCE_PLATFORM" == "auto" ]]; then
            if [ "$OPENTERFACE_FORCE_PLATFORM" != "auto" ]; then
                platform="$OPENTERFACE_FORCE_PLATFORM"
                detection_method="environment_override"
                confidence="high"
                echo "  ✅ Method 5 (override): Forcing platform to $platform"
            else
                echo "  ℹ️  Method 5 (override): Auto mode, using detection result"
            fi
        else
            echo "  ⚠️  Method 5 (override): Invalid value '$OPENTERFACE_FORCE_PLATFORM'"
        fi
    fi
    
    # =========================================================================
    # Set detected platform
    # =========================================================================
    export DETECTED_PLATFORM="$platform"
    export PLATFORM_DETECTION_METHOD="$detection_method"
    export PLATFORM_CONFIDENCE="$confidence"
    
    # Set Qt platform
    if [ "$platform" = "wayland" ]; then
        export QT_QPA_PLATFORM="wayland"
    else
        export QT_QPA_PLATFORM="xcb"
    fi
    
    # =========================================================================
    # Output results
    # =========================================================================
    echo ""
    echo "✅ Platform Detection: Complete"
    echo "   Detected Platform: $platform"
    echo "   Detection Method: $detection_method"
    echo "   Confidence: $confidence"
    echo "   QT_QPA_PLATFORM: $QT_QPA_PLATFORM"
    echo ""
}

# =============================================================================
# Installation Type Detection Functions
# =============================================================================

detect_installation_type() {
    local install_type="unknown"
    
    echo "🔍 Installation Type Detection: Starting detection..."
    
    # Check for AppImage
    if [ -n "$APPIMAGE" ]; then
        echo "  ✅ APPIMAGE environment variable set: $APPIMAGE"
        install_type="appimage"
    elif [ -L /usr/local/bin/openterfaceQT ]; then
        local target=$(readlink -f /usr/local/bin/openterfaceQT)
        if file "$target" 2>/dev/null | grep -q "AppImage"; then
            echo "  ✅ Symlink /usr/local/bin/openterfaceQT points to AppImage"
            export APPIMAGE="$target"
            install_type="appimage"
        fi
    elif [ -f /usr/local/bin/openterfaceQT ]; then
        if file /usr/local/bin/openterfaceQT 2>/dev/null | grep -q "ELF"; then
            echo "  ✅ Binary /usr/local/bin/openterfaceQT is ELF executable (system install)"
            install_type="system"
        fi
    fi
    
    # Check for RPM package
    if command -v rpm >/dev/null 2>&1; then
        if rpm -q openterfaceqt >/dev/null 2>&1; then
            echo "  ✅ RPM package 'openterfaceqt' is installed"
            install_type="rpm"
        fi
    fi
    
    # Check for DEB package
    if command -v dpkg >/dev/null 2>&1; then
        if dpkg -l | grep -q openterfaceqt; then
            echo "  ✅ DEB package 'openterfaceqt' is installed"
            install_type="deb"
        fi
    fi
    
    export DETECTED_INSTALL_TYPE="$install_type"
    echo "   Detected Installation Type: $install_type"
    echo ""
}

# =============================================================================
# Configure environment based on detection
# =============================================================================

configure_platform_environment() {
    local platform="$1"
    local install_type="$2"
    
    echo "🔧 Configuring environment for $platform platform on $install_type installation..."
    
    # Base Qt plugin paths
    local qt_plugin_dirs="/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins:/usr/lib64/qt6/plugins"
    local qml_dirs="/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml:/usr/lib64/qt6/qml"
    
    # Add AppImage-specific paths if needed
    if [ "$install_type" = "appimage" ] || [ -n "$APPIMAGE" ]; then
        qt_plugin_dirs="/usr/lib/openterfaceqt/qt6/plugins:$qt_plugin_dirs"
        qml_dirs="/usr/lib/openterfaceqt/qt6/qml:$qml_dirs"
    fi
    
    export QT_PLUGIN_PATH="$qt_plugin_dirs"
    export QML2_IMPORT_PATH="$qml_dirs"
    
    # Platform-specific configurations
    if [ "$platform" = "wayland" ]; then
        echo "   ✅ Configuring for Wayland"
        export QT_QPA_PLATFORM="wayland"
        export GDK_BACKEND="wayland"
        
        # Wayland-specific library paths
        export LD_LIBRARY_PATH="/usr/lib/openterfaceqt/wayland:/usr/lib/openterfaceqt/qt6:${LD_LIBRARY_PATH}"
    else
        echo "   ✅ Configuring for XCB/X11"
        export QT_QPA_PLATFORM="xcb"
        export GDK_BACKEND="x11"
        
        # XCB-specific library paths
        export LD_LIBRARY_PATH="/usr/lib/openterfaceqt/xcb:/usr/lib/openterfaceqt/qt6:${LD_LIBRARY_PATH}"
    fi
    
    # Common settings
    export QT_X11_NO_MITSHM=1
    export QT_DEBUG_PLUGINS=1
    export QT_LOGGING_RULES="qt.qpa.plugin=true"
    
    # Display settings
    export DISPLAY="${DISPLAY:-:0}"
    export LC_ALL=C.UTF-8
    export LANG=C.UTF-8
    
    echo "   ✅ Environment configured"
    echo ""
}

# =============================================================================
# Main execution
# =============================================================================

if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    # Script is being run directly, not sourced
    detect_platform
    detect_installation_type
    configure_platform_environment "$DETECTED_PLATFORM" "$DETECTED_INSTALL_TYPE"
    
    # Print summary
    echo "📊 Platform Detection Summary:"
    echo "=============================="
    echo "Platform: $DETECTED_PLATFORM"
    echo "Installation Type: $DETECTED_INSTALL_TYPE"
    echo "QT_QPA_PLATFORM: $QT_QPA_PLATFORM"
    echo "Detection Method: $PLATFORM_DETECTION_METHOD"
    echo "Confidence: $PLATFORM_CONFIDENCE"
fi
