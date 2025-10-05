#!/bin/bash
#
# Simplified UI Menu Test - Testing File -> Preferences
#

DISPLAY_NUM=":100"
OUTPUT_DIR="/tmp/openterface_menu_test"
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"

echo "=== Openterface Menu Test ==="
echo ""

# Find AppImage
APPIMAGE=$(find "$BUILD_DIR" -name "*.AppImage" | head -n 1)
if [ -z "$APPIMAGE" ]; then
    echo "ERROR: No AppImage found"
    exit 1
fi
chmod +x "$APPIMAGE"

# Setup
mkdir -p "$OUTPUT_DIR"
rm -f /tmp/.X100-lock /tmp/.X11-unix/X100 2>/dev/null

# Start Xvfb
echo "Starting Xvfb..."
Xvfb $DISPLAY_NUM -screen 0 1920x1080x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2
export DISPLAY=$DISPLAY_NUM

# Start openbox
echo "Starting openbox..."
openbox >/dev/null 2>&1 &
WM_PID=$!
sleep 2

# Start app
echo "Starting application..."
"$APPIMAGE" >/dev/null 2>&1 &
APP_PID=$!
sleep 5

echo "Running test sequence..."

# Capture initial
echo "  1. Baseline"
scrot "$OUTPUT_DIR/01_baseline.png"
sleep 1

# Find window
WIN_ID=$(xdotool search --class "" | head -n 1)
echo "  Window ID: $WIN_ID"

if [ -n "$WIN_ID" ]; then
    # Focus window
    xdotool windowfocus --sync "$WIN_ID"
    xdotool windowactivate --sync "$WIN_ID"
    sleep 1
    
    # Try Alt+F
    echo "  2. Pressing Alt+F"
    xdotool key --window "$WIN_ID" alt+f
    sleep 1.5
    scrot "$OUTPUT_DIR/02_altf.png"
    
    # Try pressing P for Preferences
    echo "  3. Pressing P (Preferences)"
    xdotool key --window "$WIN_ID" p
    sleep 2
    scrot "$OUTPUT_DIR/03_preferences.png"
    
    # Close
    echo "  4. Closing (Escape x2)"
    xdotool key --window "$WIN_ID" Escape Escape
    sleep 1
    scrot "$OUTPUT_DIR/04_after_close.png"
    
    # Try mouse click method
    echo "  5. Mouse click on File menu"
    xdotool mousemove --window "$WIN_ID" 40 10
    sleep 0.5
    xdotool click --window "$WIN_ID" 1
    sleep 1.5
    scrot "$OUTPUT_DIR/05_file_click.png"
    
    # Close menu
    xdotool key Escape
    sleep 1
    scrot "$OUTPUT_DIR/06_final.png"
fi

# Cleanup
echo "Cleaning up..."
kill $APP_PID $WM_PID $XVFB_PID 2>/dev/null
wait 2>/dev/null

# Results
echo ""
echo "=== Results ==="
echo "Screenshots saved to: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"/*.png | awk '{print "  " $9 " - " $5}'

echo ""
echo "Checking for differences..."
BASELINE_MD5=$(md5sum "$OUTPUT_DIR/01_baseline.png" | cut -d' ' -f1)
for img in "$OUTPUT_DIR"/*.png; do
    IMG_MD5=$(md5sum "$img" | cut -d' ' -f1)
    NAME=$(basename "$img")
    if [ "$IMG_MD5" != "$BASELINE_MD5" ]; then
        echo "  ✓ $NAME - DIFFERENT"
    else
        echo "  ✗ $NAME - Same as baseline"
    fi
done

echo ""
echo "Done! View with: xdg-open '$OUTPUT_DIR'"
