#!/bin/bash

# Quick GStreamer plugin verification script
# This script checks if the v4l2 and other plugins were built correctly

QT_TARGET_DIR="/opt/Qt6-arm64"
WORK_DIR="${HOME}/qt-arm64-build"

echo "GStreamer Plugin Verification"
echo "============================="

# Check if Qt target directory exists
if [ ! -d "$QT_TARGET_DIR" ]; then
    echo "✗ Qt target directory $QT_TARGET_DIR not found"
    echo "  Please run the build script first"
    exit 1
fi

echo "Qt target directory: $QT_TARGET_DIR"

# Check for plugin directory
if [ -d "${QT_TARGET_DIR}/lib/gstreamer-1.0" ]; then
    echo "✓ Plugin directory exists: ${QT_TARGET_DIR}/lib/gstreamer-1.0"
    
    echo -e "\nAvailable plugin libraries:"
    ls -la "${QT_TARGET_DIR}/lib/gstreamer-1.0/"*.a 2>/dev/null | while read line; do
        plugin_name=$(echo "$line" | awk '{print $9}' | xargs basename)
        echo "  - $plugin_name"
    done
    
    # Check for specific plugins
    echo -e "\nChecking for specific plugins:"
    EXPECTED_PLUGINS="libgstv4l2.a libgstrtp.a libgstrtpmanager.a libgstrtsp.a libgstudp.a libgstvideocrop.a"
    
    for plugin in $EXPECTED_PLUGINS; do
        if [ -f "${QT_TARGET_DIR}/lib/gstreamer-1.0/$plugin" ]; then
            echo "✓ $plugin found"
        else
            echo "✗ $plugin missing"
        fi
    done
    
else
    echo "✗ Plugin directory ${QT_TARGET_DIR}/lib/gstreamer-1.0 not found"
    
    # Check if plugins are in the build directory
    if [ -d "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0" ]; then
        echo "  But found in build directory: ${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0"
        echo "  Available plugins in build directory:"
        ls -la "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/"*.a 2>/dev/null | while read line; do
            plugin_name=$(echo "$line" | awk '{print $9}' | xargs basename)
            echo "    - $plugin_name"
        done
        
        # Copy them manually
        echo -e "\n  Copying plugins to Qt target directory..."
        sudo mkdir -p "${QT_TARGET_DIR}/lib/gstreamer-1.0"
        sudo cp "${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0/"*.a "${QT_TARGET_DIR}/lib/gstreamer-1.0/" 2>/dev/null
        
        if [ $? -eq 0 ]; then
            echo "✓ Plugins copied successfully"
        else
            echo "✗ Failed to copy plugins"
        fi
    else
        echo "  Build directory also not found: ${WORK_DIR}/gstreamer_build/lib/gstreamer-1.0"
    fi
fi

# Check for v4l2 symbols in libraries
echo -e "\nSearching for v4l2src symbols..."
V4L2_FOUND=false

# Check in plugin directory first
if [ -d "${QT_TARGET_DIR}/lib/gstreamer-1.0" ]; then
    for lib in "${QT_TARGET_DIR}/lib/gstreamer-1.0/"*.a; do
        if [ -f "$lib" ] && nm "$lib" 2>/dev/null | grep -q "gst_v4l2\|v4l2src"; then
            echo "✓ v4l2 symbols found in: $(basename $lib)"
            V4L2_FOUND=true
        fi
    done
fi

# Check in main lib directory
if [ "$V4L2_FOUND" = "false" ]; then
    for lib in "${QT_TARGET_DIR}/lib/"libgst*.a; do
        if [ -f "$lib" ] && nm "$lib" 2>/dev/null | grep -q "gst_v4l2\|v4l2src"; then
            echo "✓ v4l2 symbols found in: $(basename $lib)"
            V4L2_FOUND=true
        fi
    done
fi

if [ "$V4L2_FOUND" = "false" ]; then
    echo "✗ No v4l2 symbols found in any library"
    
    # Check build configuration
    if [ -d "${WORK_DIR}/gstreamer_sources/gst-plugins-good-1.22.11/build" ]; then
        echo -e "\nChecking build configuration..."
        BUILD_LOG="${WORK_DIR}/gstreamer_sources/gst-plugins-good-1.22.11/build/meson-logs/meson-log.txt"
        if [ -f "$BUILD_LOG" ]; then
            echo "Last few lines mentioning v4l2 in build log:"
            grep -i "v4l2" "$BUILD_LOG" | tail -5 || echo "No v4l2 mentions found"
        fi
    fi
    
    echo -e "\nPossible issues:"
    echo "1. V4L2 development headers not installed"
    echo "2. v4l2 plugin build failed"
    echo "3. Plugin not copied to target directory"
    echo -e "\nTo fix:"
    echo "1. Install V4L2 headers: sudo apt-get install linux-libc-dev"
    echo "2. Rebuild gst-plugins-good with v4l2 enabled"
fi

# Check system V4L2 support
echo -e "\nSystem V4L2 support:"
if [ -f "/usr/include/linux/videodev2.h" ]; then
    echo "✓ V4L2 headers found at /usr/include/linux/videodev2.h"
elif [ -f "/usr/include/videodev2.h" ]; then
    echo "✓ V4L2 headers found at /usr/include/videodev2.h"
else
    echo "✗ V4L2 headers not found"
    echo "  Install with: sudo apt-get install linux-libc-dev"
fi

# Check for video devices
echo -e "\nVideo devices:"
if ls /dev/video* >/dev/null 2>&1; then
    ls -la /dev/video*
else
    echo "No video devices found"
fi

echo -e "\nVerification complete."

# Run the full verification script if it exists
if [ -x "${QT_TARGET_DIR}/bin/verify-gstreamer.sh" ]; then
    echo -e "\n" 
    echo "Running full GStreamer verification..."
    "${QT_TARGET_DIR}/bin/verify-gstreamer.sh"
fi
