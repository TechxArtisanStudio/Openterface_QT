#!/bin/bash

# Test script to verify GStreamer installation for static linking

QT_TARGET_DIR="${QT_TARGET_DIR:-/opt/Qt6-arm64}"

echo "Testing GStreamer installation at: ${QT_TARGET_DIR}"
echo "=================================================="

# Check core GStreamer headers
echo "Checking core GStreamer headers..."
if [ -f "${QT_TARGET_DIR}/include/gstreamer-1.0/gst/gst.h" ]; then
    echo "  ✓ gst/gst.h found"
else
    echo "  ✗ gst/gst.h NOT found"
fi

if [ -f "${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/videooverlay.h" ]; then
    echo "  ✓ gst/video/videooverlay.h found"
else
    echo "  ✗ gst/video/videooverlay.h NOT found"
fi

if [ -f "${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/video.h" ]; then
    echo "  ✓ gst/video/video.h found"
else
    echo "  ✗ gst/video/video.h NOT found"
fi

# Check GLib headers
echo -e "\nChecking GLib headers..."
if [ -f "${QT_TARGET_DIR}/include/glib-2.0/glib.h" ]; then
    echo "  ✓ glib.h found"
else
    echo "  ✗ glib.h NOT found"
fi

if [ -f "${QT_TARGET_DIR}/lib/glib-2.0/include/glibconfig.h" ]; then
    echo "  ✓ glibconfig.h found"
else
    echo "  ✗ glibconfig.h NOT found"
fi

# Check static libraries
echo -e "\nChecking GStreamer static libraries..."
libs=(
    "libgstreamer-1.0.a"
    "libgstbase-1.0.a"
    "libgstvideo-1.0.a"
    "libgstaudio-1.0.a"
    "libgstapp-1.0.a"
    "libgstpbutils-1.0.a"
    "libgobject-2.0.a"
    "libglib-2.0.a"
)

for lib in "${libs[@]}"; do
    if [ -f "${QT_TARGET_DIR}/lib/${lib}" ]; then
        echo "  ✓ ${lib} found"
    else
        echo "  ✗ ${lib} NOT found"
    fi
done

# Check pkg-config files
echo -e "\nChecking pkg-config files..."
pkgconfig_files=(
    "gstreamer-1.0.pc"
    "gstreamer-base-1.0.pc"
    "gstreamer-video-1.0.pc"
    "glib-2.0.pc"
    "gobject-2.0.pc"
)

for pc in "${pkgconfig_files[@]}"; do
    if [ -f "${QT_TARGET_DIR}/lib/pkgconfig/${pc}" ]; then
        echo "  ✓ ${pc} found"
    else
        echo "  ✗ ${pc} NOT found"
    fi
done

# List all available GStreamer headers
echo -e "\nAvailable GStreamer headers:"
if [ -d "${QT_TARGET_DIR}/include/gstreamer-1.0" ]; then
    find "${QT_TARGET_DIR}/include/gstreamer-1.0" -name "*.h" | head -20
    echo "  ... (showing first 20 files)"
else
    echo "  GStreamer include directory not found"
fi

# Test compilation of a simple GStreamer program
echo -e "\nTesting compilation with static GStreamer..."
cat > /tmp/gst_test.c << 'EOF'
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

int main() {
    gst_init(NULL, NULL);
    g_print("GStreamer initialized successfully\n");
    return 0;
}
EOF

export PKG_CONFIG_PATH="${QT_TARGET_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH}"

if gcc -I"${QT_TARGET_DIR}/include/gstreamer-1.0" \
       -I"${QT_TARGET_DIR}/include/glib-2.0" \
       -I"${QT_TARGET_DIR}/lib/glib-2.0/include" \
       /tmp/gst_test.c \
       -L"${QT_TARGET_DIR}/lib" \
       -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \
       -o /tmp/gst_test 2>/dev/null; then
    echo "  ✓ GStreamer compilation test PASSED"
    rm -f /tmp/gst_test
else
    echo "  ✗ GStreamer compilation test FAILED"
    echo "    Try running this manually to see errors:"
    echo "    gcc -I${QT_TARGET_DIR}/include/gstreamer-1.0 \\"
    echo "        -I${QT_TARGET_DIR}/include/glib-2.0 \\"
    echo "        -I${QT_TARGET_DIR}/lib/glib-2.0/include \\"
    echo "        /tmp/gst_test.c \\"
    echo "        -L${QT_TARGET_DIR}/lib \\"
    echo "        -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \\"
    echo "        -o /tmp/gst_test"
fi

rm -f /tmp/gst_test.c

echo -e "\n=================================================="
echo "Test completed."
