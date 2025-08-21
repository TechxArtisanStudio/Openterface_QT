#!/bin/bash

echo "Testing GStreamer installation..."
echo "=================================="

# Test for required header file
VIDEO_OVERLAY_HEADER="/opt/Qt6-arm64/include/gstreamer-1.0/gst/video/videooverlay.h"
if [ -f "$VIDEO_OVERLAY_HEADER" ]; then
    echo "✓ gst/video/videooverlay.h found"
else
    echo "✗ gst/video/videooverlay.h NOT found"
    echo "  Expected at: $VIDEO_OVERLAY_HEADER"
fi

# Test for required libraries
echo -e "\nChecking static libraries:"
for lib in libgstvideo-1.0.a libgstaudio-1.0.a libgstpbutils-1.0.a libgstrtp-1.0.a; do
    if [ -f "/opt/Qt6-arm64/lib/$lib" ]; then
        echo "✓ $lib found"
    else
        echo "✗ $lib NOT found"
    fi
done

# Test for GStreamer core
GSTREAMER_H="/opt/Qt6-arm64/include/gstreamer-1.0/gst/gst.h"
if [ -f "$GSTREAMER_H" ]; then
    echo "✓ GStreamer core headers found"
else
    echo "✗ GStreamer core headers NOT found"
fi

echo -e "\nDone."
